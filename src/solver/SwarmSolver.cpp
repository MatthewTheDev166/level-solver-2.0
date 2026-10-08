#include "SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../core/CheatAPIIntegrator.hpp"
#include "../engine/HazardDetector.hpp"
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <algorithm>

using namespace geode::prelude;

SwarmSolver::~SwarmSolver() {
    cleanupHeadless();
}

void SwarmSolver::cleanupHeadless() {
    CheatAPIIntegrator::notifyCheatEnded();
    solver::HazardDetector::clearIndex();
    m_isSolving = false;
    if (m_headlessPlayLayer) {
        m_headlessPlayLayer->removeFromParentAndCleanup(true);
        m_headlessPlayLayer->release();
        m_headlessPlayLayer = nullptr;
    }
    if (m_headlessScene) {
        m_headlessScene->release();
        m_headlessScene = nullptr;
    }
    m_headlessSimulating = false;
}

void SwarmSolver::reset() {
    cleanupHeadless();
    CheatAPIIntegrator::notifyCheatEnded();
    solver::HazardDetector::clearIndex();
    m_isSolving = false;
    m_isCompleted = false;
    m_activeLevel = nullptr;
    m_frontierTick = 0;
    m_rewindCount = 0;
    m_activeWave = 0;
    m_waveAttempt = 0;
    m_temperature = 1.0f;
    m_startX = 0.0f;
    m_currentX = 0.0f;
    m_trueLevelLength = 1000.0f;
    m_verifiedPrefix.clear();
    m_anchors.clear();
    m_activeCandidates.clear();
    m_currentWaveSurvivors.clear();
    m_currentWaveFailures.clear();
    m_currentCandidateIdx = 0;
    m_telemetry = SwarmTelemetry{};
}

void SwarmSolver::stop() {
    m_isSolving = false;
    CheatAPIIntegrator::notifyCheatEnded();
    m_telemetry.status = SolverStatus::Paused;
    m_telemetry.detailMessage = "Solving stopped";
}

void SwarmSolver::pause() {
    m_isSolving = false;
    CheatAPIIntegrator::notifyCheatEnded();
    m_telemetry.status = SolverStatus::Paused;
    m_telemetry.detailMessage = "Solving paused";
}

void SwarmSolver::resume() {
    if (m_headlessPlayLayer && !m_isCompleted) {
        CheatAPIIntegrator::notifyCheatStarted();
        m_isSolving = true;
        m_telemetry.status = SolverStatus::Searching;
        m_telemetry.detailMessage = "Solving resumed...";
    }
}

void SwarmSolver::start(GJGameLevel* level) {
    if (!level) {
        m_telemetry.status = SolverStatus::Failed;
        m_telemetry.detailMessage = "No level selected!";
        return;
    }

    if (level->isPlatformer()) {
        m_telemetry.status = SolverStatus::Failed;
        m_telemetry.detailMessage = "Platformer levels unsupported";
        geode::Notification::create("Platformer levels are not supported by Level Solver.", geode::NotificationIcon::Warning)->show();
        return;
    }

    if (level->m_twoPlayerMode) {
        m_telemetry.status = SolverStatus::Failed;
        m_telemetry.detailMessage = "2-Player levels unsupported";
        geode::Notification::create("2-Player levels are not supported by Level Solver.", geode::NotificationIcon::Warning)->show();
        return;
    }

    // Ensure level string is loaded before PlayLayer::create to prevent ZipUtils crash in other mods
    if (level->m_levelString.empty()) {
        if (auto llm = LocalLevelManager::sharedState()) {
            auto str = llm->getMainLevelString(level->m_levelID.value());
            if (!str.empty()) {
                level->m_levelString = str;
            }
        }
    }
    if (level->m_levelString.empty()) {
        if (auto glm = GameLevelManager::sharedState()) {
            if (auto mainLvl = glm->getMainLevel(level->m_levelID.value(), false)) {
                if (!mainLvl->m_levelString.empty()) {
                    level->m_levelString = mainLvl->m_levelString;
                }
            }
        }
    }

    if (level->m_levelString.empty()) {
        m_telemetry.status = SolverStatus::Failed;
        m_telemetry.detailMessage = "Level data empty! Open/play level once first.";
        geode::Notification::create("Please open or play this level once to load its data before solving!", geode::NotificationIcon::Warning)->show();
        return;
    }

    reset();
    CheatAPIIntegrator::notifyCheatStarted();
    m_activeLevel = level;

    // Create off-screen headless simulation environment
    m_headlessScene = cocos2d::CCScene::create();
    m_headlessScene->retain();

    m_headlessSimulating = true;
    m_headlessPlayLayer = PlayLayer::create(level, false, false);
    if (!m_headlessPlayLayer) {
        cleanupHeadless();
        m_telemetry.status = SolverStatus::Failed;
        m_telemetry.detailMessage = "Failed to initialize PlayLayer";
        return;
    }

    m_headlessPlayLayer->retain();
    m_headlessScene->addChild(m_headlessPlayLayer);
    m_headlessPlayLayer->m_isSilent = true;
    m_headlessPlayLayer->m_isPracticeMode = false;
    m_headlessPlayLayer->setKeypadEnabled(false);
    m_headlessPlayLayer->setTouchEnabled(false);
    m_headlessPlayLayer->setMouseEnabled(false);

    // Synchronously process object creation
    int safetyLimit = 10000;
    while (m_headlessPlayLayer->m_loadingProgress < 1.0f && --safetyLimit > 0) {
        m_headlessPlayLayer->processCreateObjectsFromSetup();
    }

    m_headlessPlayLayer->resetLevel();
    m_headlessPlayLayer->startGame();
    m_headlessPlayLayer->m_isPaused = false;
    m_headlessPlayLayer->m_hasCompletedLevel = false;
    solver::HazardDetector::buildIndex(m_headlessPlayLayer->m_objects);


    calculateTrueLevelLength();
    captureInitialAnchor();

    m_isSolving = true;
    m_telemetry.status = SolverStatus::Searching;
    m_telemetry.detailMessage = "Swarm engine searching...";
    m_telemetry.totalBots = m_populationSize;
    m_telemetry.aliveBots = m_populationSize;
    m_telemetry.targetEndX = m_trueLevelLength;
    m_speedTimer = std::chrono::high_resolution_clock::now();
    m_ticksSampleCount = 0;
}

void SwarmSolver::calculateTrueLevelLength() {
    float maxObjX = m_startX;
    if (m_headlessPlayLayer && m_headlessPlayLayer->m_objects) {
        for (unsigned int i = 0; i < m_headlessPlayLayer->m_objects->count(); ++i) {
            if (auto obj = geode::cast::typeinfo_cast<GameObject*>(m_headlessPlayLayer->m_objects->objectAtIndex(i))) {
                float right = obj->getPositionX() + 90.0f;
                if (right > maxObjX) {
                    maxObjX = right;
                }
            }
        }
    }

    float robtopEnd = m_headlessPlayLayer ? m_headlessPlayLayer->getEndPosition().x : 0.0f;
    float trueLength = m_startX + 300.0f;
    if (maxObjX > m_startX + 50.0f) {
        trueLength = std::max(trueLength, maxObjX);
    }
    if (robtopEnd > m_startX + 50.0f) {
        if (maxObjX > m_startX + 50.0f) {
            trueLength = std::max(trueLength, robtopEnd);
        } else {
            trueLength = robtopEnd;
        }
    }
    m_trueLevelLength = trueLength;
}

void SwarmSolver::captureInitialAnchor() {
    if (!m_headlessPlayLayer || !m_headlessPlayLayer->m_player1) return;

    m_startX = m_headlessPlayLayer->m_player1->getPositionX();
    m_currentX = m_startX;

    AnchorPoint initial;
    initial.tick = 0;
    initial.x = m_startX;
    initial.p1Snapshot.capture(m_headlessPlayLayer->m_player1);
    if (m_headlessPlayLayer->m_player2) {
        initial.p2Snapshot.capture(m_headlessPlayLayer->m_player2);
        initial.hasPlayer2 = true;
    }
    initial.actionPrefixCount = 0;

    m_anchors.clear();
    m_anchors.push_back(initial);
}

std::vector<BotCandidate> SwarmSolver::generateSwarm(uint32_t startTick, uint32_t horizon, bool startsHeld) {
    std::vector<BotCandidate> swarm;
    swarm.reserve(m_populationSize);

    auto addBot = [&](const std::vector<Action>& acts) {
        if (swarm.size() >= static_cast<size_t>(m_populationSize)) return;
        BotCandidate b;
        b.id = static_cast<uint32_t>(swarm.size());
        b.actions = acts;
        swarm.push_back(std::move(b));
    };

    // Mode and speed detection from latest verified anchor
    bool isDart = false;
    bool isShip = false;
    bool isBird = false;
    bool isBall = false;
    bool isRobot = false;
    bool isSpider = false;
    bool isSwing = false;
    bool isUpsideDown = false;
    float pSpeed = 1.0f;

    if (!m_anchors.empty()) {
        const auto& snap = m_anchors.back().p1Snapshot;
        isDart = snap.isDart;
        isShip = snap.isShip;
        isBird = snap.isBird;
        isBall = snap.isBall;
        isRobot = snap.isRobot;
        isSpider = snap.isSpider;
        isSwing = snap.isSwing;
        isUpsideDown = snap.isUpsideDown;
        if (snap.playerSpeed > 0.1f) pSpeed = snap.playerSpeed;
    } else if (m_headlessPlayLayer && m_headlessPlayLayer->m_player1) {
        auto p = m_headlessPlayLayer->m_player1;
        isDart = p->m_isDart;
        isShip = p->m_isShip;
        isBird = p->m_isBird;
        isBall = p->m_isBall;
        isRobot = p->m_isRobot;
        isSpider = p->m_isSpider;
        isSwing = p->m_isSwing;
        isUpsideDown = p->m_isUpsideDown;
        if (p->m_playerSpeed > 0.1f) pSpeed = p->m_playerSpeed;
    }

    auto makeSingleTap = [startTick, horizon, startsHeld](uint32_t pressOffset, uint32_t dur) -> std::vector<Action> {
        std::vector<Action> acts;
        if (startsHeld && pressOffset > 0) {
            acts.push_back({ startTick, false, 1, false });
        }
        uint32_t pTick = startTick + pressOffset;
        acts.push_back({ pTick, true, 1, false });
        if (pressOffset + dur < horizon) {
            acts.push_back({ pTick + dur, false, 1, false });
        }
        return acts;
    };

    auto makeDoubleTap = [startTick, horizon, startsHeld](uint32_t off1, uint32_t dur1, uint32_t off2, uint32_t dur2) -> std::vector<Action> {
        std::vector<Action> acts;
        if (startsHeld && off1 > 0) {
            acts.push_back({ startTick, false, 1, false });
        }
        uint32_t t1 = startTick + off1;
        acts.push_back({ t1, true, 1, false });
        if (off1 + dur1 < horizon) {
            acts.push_back({ t1 + dur1, false, 1, false });
        }
        uint32_t t2 = startTick + off2;
        if (off2 < horizon) {
            acts.push_back({ t2, true, 1, false });
            if (off2 + dur2 < horizon) {
                acts.push_back({ t2 + dur2, false, 1, false });
            }
        }
        return acts;
    };

    // 1. Baseline actions (~5%)
    if (startsHeld) {
        // Continue holding
        addBot({});
        // Immediate release
        addBot({ { startTick, false, 1, false } });
        // Staggered releases
        for (uint32_t r : { 2u, 4u, 6u, 8u, 12u, 16u, 22u, 30u, 40u, 50u }) {
            if (r < horizon) {
                addBot({ { startTick + r, false, 1, false } });
            }
        }
    } else {
        // Idle
        addBot({});
        // Full hold
        addBot({ { startTick, true, 1, false } });
        // Staggered releases after hold
        for (uint32_t r : { 2u, 4u, 6u, 8u, 12u, 16u, 22u, 30u, 40u, 50u }) {
            if (r < horizon) {
                addBot({ { startTick, true, 1, false }, { startTick + r, false, 1, false } });
            }
        }
    }

    // 2. Gamemode-specific flight & movement patterns (25-35%)
    if (isDart) {
        // Wave: symmetric zig-zag frequencies
        for (uint32_t freq : { 2u, 3u, 4u, 5u, 6u, 8u, 10u, 12u, 16u }) {
            for (uint32_t offset : { 0u, 1u, 2u }) {
                std::vector<Action> waveActs1;
                if (startsHeld) waveActs1.push_back({ startTick, false, 1, false });
                bool state = false;
                for (uint32_t t = offset; t < horizon; t += freq) {
                    state = !state;
                    waveActs1.push_back({ startTick + t, state, 1, false });
                }
                addBot(waveActs1);

                std::vector<Action> waveActs2;
                if (!startsHeld) waveActs2.push_back({ startTick, true, 1, false });
                state = true;
                for (uint32_t t = offset; t < horizon; t += freq) {
                    state = !state;
                    waveActs2.push_back({ startTick + t, state, 1, false });
                }
                addBot(waveActs2);
            }
        }

        // Asymmetric climbing biases (hold > release)
        for (auto [h, r] : std::vector<std::pair<uint32_t, uint32_t>>{
            {4, 2}, {6, 3}, {8, 4}, {10, 4}, {12, 5}, {14, 6}, {18, 8}
        }) {
            std::vector<Action> climbActs;
            if (startsHeld) climbActs.push_back({ startTick, false, 1, false });
            uint32_t cur = 0;
            while (cur < horizon) {
                climbActs.push_back({ startTick + cur, true, 1, false });
                cur += h;
                if (cur < horizon) {
                    climbActs.push_back({ startTick + cur, false, 1, false });
                    cur += r;
                }
            }
            addBot(climbActs);
        }

        // Asymmetric diving biases (hold < release)
        for (auto [h, r] : std::vector<std::pair<uint32_t, uint32_t>>{
            {2, 4}, {3, 6}, {4, 8}, {4, 10}, {5, 12}, {6, 14}, {8, 18}
        }) {
            std::vector<Action> diveActs;
            if (startsHeld) diveActs.push_back({ startTick, false, 1, false });
            uint32_t cur = 0;
            while (cur < horizon) {
                diveActs.push_back({ startTick + cur, true, 1, false });
                cur += h;
                if (cur < horizon) {
                    diveActs.push_back({ startTick + cur, false, 1, false });
                    cur += r;
                }
            }
            addBot(diveActs);
        }
    } else if (isShip) {
        // Ship: micro-flutter and pulse gliding
        for (uint32_t pulse : { 1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u }) {
            for (uint32_t gap : { 1u, 2u, 3u, 4u, 6u, 8u, 12u }) {
                for (uint32_t offset : { 0u, 1u, 2u, 4u }) {
                    std::vector<Action> shipActs;
                    if (startsHeld && offset > 0) shipActs.push_back({ startTick, false, 1, false });
                    uint32_t cur = offset;
                    while (cur < horizon) {
                        shipActs.push_back({ startTick + cur, true, 1, false });
                        cur += pulse;
                        if (cur < horizon) {
                            shipActs.push_back({ startTick + cur, false, 1, false });
                            cur += gap;
                        }
                    }
                    addBot(shipActs);
                }
            }
        }
    } else if (isBird) {
        // UFO: periodic discrete impulse hops (duration 2 ticks)
        for (uint32_t interval : { 6u, 8u, 10u, 12u, 14u, 16u, 18u, 22u, 26u, 30u }) {
            for (uint32_t offset : { 0u, 2u, 4u, 6u }) {
                std::vector<Action> ufoActs;
                if (startsHeld && offset > 0) ufoActs.push_back({ startTick, false, 1, false });
                uint32_t cur = offset;
                while (cur < horizon) {
                    ufoActs.push_back({ startTick + cur, true, 1, false });
                    if (cur + 2 < horizon) {
                        ufoActs.push_back({ startTick + cur + 2, false, 1, false });
                    }
                    cur += interval;
                }
                addBot(ufoActs);
            }
        }
        for (uint32_t t1 = 0; t1 < horizon / 2; t1 += 6) {
            addBot(makeDoubleTap(t1, 2, t1 + 6, 2));
        }
    } else if (isBall) {
        // Ball: surface gravity flips
        for (uint32_t t = 0; t < horizon; t += 2) {
            addBot(makeSingleTap(t, 3));
        }
        for (uint32_t t1 = 0; t1 < horizon / 2; t1 += 4) {
            for (uint32_t gap : { 6u, 10u, 14u, 20u }) {
                if (t1 + gap < horizon) {
                    addBot(makeDoubleTap(t1, 3, t1 + gap, 3));
                }
            }
        }
    } else if (isRobot) {
        // Robot: variable hold jumps
        for (uint32_t t = 0; t < horizon; t += 3) {
            for (uint32_t dur : { 2u, 4u, 6u, 8u, 12u, 16u, 20u, 26u, 32u, 38u }) {
                if (t + dur <= horizon + 10) {
                    addBot(makeSingleTap(t, dur));
                }
            }
        }
    } else if (isSpider) {
        // Spider: instant surface teleportation
        for (uint32_t t = 0; t < horizon; t += 2) {
            addBot(makeSingleTap(t, 2));
        }
        for (uint32_t t1 = 0; t1 < horizon / 2; t1 += 4) {
            for (uint32_t gap : { 4u, 8u, 12u, 18u }) {
                if (t1 + gap < horizon) {
                    addBot(makeDoubleTap(t1, 2, t1 + gap, 2));
                }
            }
        }
    } else if (isSwing) {
        // Swing: discrete gravity toggles
        for (uint32_t interval : { 8u, 12u, 16u, 20u, 24u, 28u, 34u, 40u }) {
            for (uint32_t offset : { 0u, 2u, 4u, 6u }) {
                std::vector<Action> swingActs;
                if (startsHeld && offset > 0) swingActs.push_back({ startTick, false, 1, false });
                uint32_t cur = offset;
                while (cur < horizon) {
                    swingActs.push_back({ startTick + cur, true, 1, false });
                    if (cur + 2 < horizon) {
                        swingActs.push_back({ startTick + cur + 2, false, 1, false });
                    }
                    cur += interval;
                }
                addBot(swingActs);
            }
        }
    }

    // 3. Generic jumps & taps across horizon (Cube and universal coverage)
    for (uint32_t t = 0; t < horizon; t += 2) {
        for (uint32_t dur : { 2u, 4u, 8u, 14u, 24u, 36u }) {
            addBot(makeSingleTap(t, dur));
        }
    }

    for (uint32_t t1 = 0; t1 < horizon / 2; t1 += 4) {
        for (uint32_t gap : { 6u, 12u, 18u }) {
            if (t1 + gap < horizon) {
                addBot(makeDoubleTap(t1, 3, t1 + gap, 3));
            }
        }
    }

    // 4. Orbs (Jump rings) detection
    float horizonDist = horizon * 1.5f * pSpeed;
    auto orbs = solver::HazardDetector::getOrbsInWindow(
        m_currentX - 10.0f, m_currentX + horizonDist,
        m_headlessPlayLayer ? m_headlessPlayLayer->m_objects : nullptr
    );
    float speedRatio = 1.3f * pSpeed;
    if (speedRatio < 0.2f) speedRatio = 1.3f;

    for (auto obj : orbs) {
        if (swarm.size() >= static_cast<size_t>(m_populationSize)) break;
        float dist = obj->getPositionX() - m_currentX;
        if (dist >= 0.0f) {
            uint32_t approxTick = startTick + static_cast<uint32_t>(dist / speedRatio);
            for (int delta : { -3, -2, -1, 0, 1, 2, 3 }) {
                int target = static_cast<int>(approxTick) + delta;
                if (target >= static_cast<int>(startTick) && static_cast<uint32_t>(target) < startTick + horizon) {
                    uint32_t tapTick = static_cast<uint32_t>(target);
                    uint32_t off = tapTick - startTick;
                    addBot(makeSingleTap(off, 4));

                    if (obj->m_objectType == GameObjectType::DashRing || obj->m_objectType == GameObjectType::GravityDashRing) {
                        addBot(makeSingleTap(off, 24));
                    }
                }
            }
        }
    }

    // 5. Targeted mutations from previous wave failures
    if (m_waveAttempt > 0 && !m_currentWaveFailures.empty()) {
        std::vector<BotCandidate> failures = m_currentWaveFailures;
        std::sort(failures.begin(), failures.end(), [](const BotCandidate& a, const BotCandidate& b) {
            return a.deathTick > b.deathTick;
        });

        size_t count = std::min(failures.size(), static_cast<size_t>(10));
        for (size_t i = 0; i < count; ++i) {
            if (swarm.size() >= static_cast<size_t>(m_populationSize)) break;
            const auto& parent = failures[i];
            if (parent.deathTick > startTick) {
                uint32_t deathLocal = parent.deathTick - startTick;
                for (uint32_t lead : { 1u, 2u, 4u, 7u, 12u, 18u, 25u }) {
                    if (deathLocal >= lead) {
                        uint32_t flipT = deathLocal - lead;
                        int jitter = 0;
                        if (m_temperature > 1.2f) {
                            std::uniform_int_distribution<int> jitDist(-static_cast<int>(m_temperature), static_cast<int>(m_temperature));
                            jitter = jitDist(m_rng);
                        }
                        int mutatedFlipT = std::clamp(static_cast<int>(flipT) + jitter, 0, static_cast<int>(horizon - 1));

                        std::vector<Action> flipped = parent.actions;
                        flipped.erase(std::remove_if(flipped.begin(), flipped.end(),
                            [startTick, mutatedFlipT](const Action& a) { return a.tick >= startTick + static_cast<uint32_t>(mutatedFlipT); }), flipped.end());

                        flipped.push_back({ startTick + static_cast<uint32_t>(mutatedFlipT), true, 1, false });
                        if (mutatedFlipT + 8 < static_cast<int>(horizon)) {
                            flipped.push_back({ startTick + static_cast<uint32_t>(mutatedFlipT + 8), false, 1, false });
                        }
                        addBot(flipped);
                    }
                }
            }
        }
    }

    // 6. Stochastic temperature-scaled fill
    std::uniform_int_distribution<uint32_t> tickDist(0, horizon - 1);
    std::uniform_int_distribution<int> boolDist(0, 1);
    while (swarm.size() < static_cast<size_t>(m_populationSize)) {
        std::vector<Action> rndActs;
        if (startsHeld) {
            rndActs.push_back({ startTick, false, 1, false });
        }
        uint32_t num = 1 + (m_rng() % 4);
        for (uint32_t i = 0; i < num; ++i) {
            rndActs.push_back({ startTick + tickDist(m_rng), boolDist(m_rng) == 1, 1, false });
        }
        std::sort(rndActs.begin(), rndActs.end(), [](const Action& a, const Action& b) {
            return a.tick < b.tick;
        });
        addBot(rndActs);
    }

    return swarm;
}


void SwarmSolver::simulateCandidate(BotCandidate& bot, uint32_t startTick, uint32_t horizon) {
    if (!m_headlessPlayLayer || !m_headlessPlayLayer->m_player1) return;

    m_headlessSimulating = true;
    m_headlessPlayLayer->resetLevel();

    // Restore root spawn state
    if (!m_anchors.empty()) {
        m_anchors.front().p1Snapshot.restore(m_headlessPlayLayer->m_player1);
        if (m_anchors.front().hasPlayer2 && m_headlessPlayLayer->m_player2) {
            m_anchors.front().p2Snapshot.restore(m_headlessPlayLayer->m_player2);
        }
    }

    m_headlessPlayLayer->m_started = true;
    m_headlessPlayLayer->m_inResetDelay = false;
    m_headlessPlayLayer->m_playerDied = false;
    m_headlessPlayLayer->m_player1->m_isDead = false;
    if (m_headlessPlayLayer->m_player2) m_headlessPlayLayer->m_player2->m_isDead = false;
    m_headlessPlayLayer->m_isPaused = false;
    m_headlessPlayLayer->m_hasCompletedLevel = false;
    m_headlessPlayLayer->m_resumeTimer = 0;
    m_headlessPlayLayer->m_queuedButtons.clear();
    m_headlessPlayLayer->moveCameraToPos(m_headlessPlayLayer->m_player1->getPosition());

    size_t prefixIdx = 0;
    size_t candIdx = 0;
    const float fixedDt = 1.0f / 240.0f;
    uint32_t totalSimulationTicks = startTick + horizon;

    for (uint32_t step = 0; step < totalSimulationTicks; ++step) {
        if (step < startTick) {
            while (prefixIdx < m_verifiedPrefix.size() && m_verifiedPrefix[prefixIdx].tick <= step) {
                const auto& act = m_verifiedPrefix[prefixIdx++];
                m_headlessPlayLayer->handleButton(act.down, act.button, !act.player2);
            }
        } else {
            while (candIdx < bot.actions.size() && bot.actions[candIdx].tick <= step) {
                const auto& act = bot.actions[candIdx++];
                m_headlessPlayLayer->handleButton(act.down, act.button, !act.player2);
            }
        }

        m_headlessPlayLayer->update(fixedDt);
        m_ticksSampleCount++;

        if (m_headlessPlayLayer->m_player1->m_isDead) {
            bot.died = true;
            bot.deathTick = step;
            bot.finalPos = m_headlessPlayLayer->m_player1->getPosition();
            bot.finalX = bot.finalPos.x;
            bot.landedSafely = false;
            m_headlessSimulating = false;
            return;
        }

        bool reachedEndPos = (m_headlessPlayLayer->getEndPosition().x > m_startX + 50.0f &&
                              m_headlessPlayLayer->m_player1->getPositionX() >= m_headlessPlayLayer->getEndPosition().x - 10.0f);

        if (m_headlessPlayLayer->m_hasCompletedLevel || m_headlessPlayLayer->m_player1->getPositionX() >= m_trueLevelLength || reachedEndPos) {
            bot.completed = true;
            bot.finalPos = m_headlessPlayLayer->m_player1->getPosition();
            bot.finalX = bot.finalPos.x;
            bot.landedSafely = true;
            bot.actualEndTick = step;
            bot.p1Snapshot.capture(m_headlessPlayLayer->m_player1);
            if (m_headlessPlayLayer->m_player2) {
                bot.p2Snapshot.capture(m_headlessPlayLayer->m_player2);
                bot.hasPlayer2 = true;
            }
            bot.clearance = solver::HazardDetector::MAX_CLEARANCE;
            bot.fitnessScore = bot.finalX * 10.0f + bot.clearance + 100000.0f;
            m_headlessSimulating = false;
            return;
        }
    }

    auto p1 = m_headlessPlayLayer->m_player1;
    bool isFlying = p1->m_isShip || p1->m_isBird || p1->m_isDart || p1->m_isSwing;
    uint32_t actualEnd = totalSimulationTicks;

    if (!isFlying && !p1->m_isOnGround) {
        // Elastic landing extension: step up to 40 un-inputted ticks until landed or dead
        const uint32_t maxExtension = 40;
        for (uint32_t ext = 0; ext < maxExtension; ++ext) {
            m_headlessPlayLayer->update(fixedDt);
            actualEnd++;
            m_ticksSampleCount++;

            if (p1->m_isDead) {
                bot.died = true;
                bot.deathTick = actualEnd;
                bot.finalPos = p1->getPosition();
                bot.finalX = bot.finalPos.x;
                bot.landedSafely = false;
                m_headlessSimulating = false;
                return;
            }

            bool reachedEndPosExt = (m_headlessPlayLayer->getEndPosition().x > m_startX + 50.0f &&
                                     p1->getPositionX() >= m_headlessPlayLayer->getEndPosition().x - 10.0f);

            if (m_headlessPlayLayer->m_hasCompletedLevel || p1->getPositionX() >= m_trueLevelLength || reachedEndPosExt) {
                bot.completed = true;
                bot.finalPos = p1->getPosition();
                bot.finalX = bot.finalPos.x;
                bot.landedSafely = true;
                bot.actualEndTick = actualEnd;
                bot.p1Snapshot.capture(p1);
                if (m_headlessPlayLayer->m_player2) {
                    bot.p2Snapshot.capture(m_headlessPlayLayer->m_player2);
                    bot.hasPlayer2 = true;
                }
                bot.clearance = solver::HazardDetector::MAX_CLEARANCE;
                bot.fitnessScore = bot.finalX * 10.0f + bot.clearance + 100000.0f;
                m_headlessSimulating = false;
                return;
            }

            if (p1->m_isOnGround) {
                break;
            }
        }
    }

    bot.actualEndTick = actualEnd;
    bot.finalPos = p1->getPosition();
    bot.finalX = bot.finalPos.x;
    bot.p1Snapshot.capture(p1);
    if (m_headlessPlayLayer->m_player2) {
        bot.p2Snapshot.capture(m_headlessPlayLayer->m_player2);
        bot.hasPlayer2 = true;
    }

    size_t nearbyObstacles = 0;
    bot.clearance = solver::HazardDetector::calculateClearance(
        bot.finalPos,
        m_headlessPlayLayer->m_objects,
        nearbyObstacles
    );

    bool reachedEndPosFinal = (m_headlessPlayLayer->getEndPosition().x > m_startX + 50.0f &&
                               bot.finalX >= m_headlessPlayLayer->getEndPosition().x - 10.0f);

    if (m_headlessPlayLayer->m_hasCompletedLevel || bot.finalX >= m_trueLevelLength || reachedEndPosFinal) {
        bot.completed = true;
        bot.landedSafely = true;
    } else if (isFlying) {
        bot.landedSafely = (bot.clearance >= 5.0f);
    } else {
        bot.landedSafely = p1->m_isOnGround;
    }

    bot.fitnessScore = bot.finalX * 10.0f + bot.clearance;
    if (bot.landedSafely) bot.fitnessScore += 500.0f;
    if (bot.completed) bot.fitnessScore += 100000.0f;

    m_headlessSimulating = false;
}


void SwarmSolver::stepSwarmBatch(uint32_t msBudget) {
    if (!m_isSolving || m_isCompleted || !m_headlessPlayLayer || m_anchors.empty()) return;

    auto startTime = std::chrono::high_resolution_clock::now();
    const auto budget = std::chrono::milliseconds(msBudget);

    // Initialize new wave if needed
    if (m_activeCandidates.empty() || m_currentCandidateIdx >= m_activeCandidates.size()) {
        m_activeWave++;
        m_waveAttempt++;
        m_waveAnchor = m_anchors.back();

        bool startsHeld = !m_verifiedPrefix.empty() && m_verifiedPrefix.back().down;
        m_activeCandidates = generateSwarm(m_frontierTick, m_horizonTicks, startsHeld);
        m_currentCandidateIdx = 0;
        m_currentWaveSurvivors.clear();
        m_currentWaveFailures.clear();
        m_telemetry.totalBots = m_populationSize;
        m_telemetry.aliveBots = m_populationSize;
    }

    // Step candidates within frame budget
    while (m_currentCandidateIdx < m_activeCandidates.size()) {
        auto& bot = m_activeCandidates[m_currentCandidateIdx++];
        simulateCandidate(bot, m_frontierTick, m_horizonTicks);

        if (bot.died) {
            if (m_telemetry.aliveBots > 0) m_telemetry.aliveBots--;
            m_currentWaveFailures.push_back(bot);
        } else {
            m_currentWaveSurvivors.push_back(bot);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - startTime
        );
        if (elapsed >= budget) {
            break;
        }
    }

    // Check if entire wave has finished evaluating
    if (m_currentCandidateIdx >= m_activeCandidates.size()) {
        int bestIdx = -1;
        float bestFitness = -1.0f;

        for (size_t i = 0; i < m_activeCandidates.size(); ++i) {
            const auto& bot = m_activeCandidates[i];
            if (!bot.died && (bot.landedSafely || bot.completed)) {
                if (bot.fitnessScore > bestFitness) {
                    bestFitness = bot.fitnessScore;
                    bestIdx = static_cast<int>(i);
                }
            }
        }

        if (bestIdx >= 0) {
            // Frontier Advancement
            const auto& winner = m_activeCandidates[bestIdx];

            m_frontierTick = winner.actualEndTick > 0 ? winner.actualEndTick : (m_frontierTick + m_horizonTicks);
            m_currentX = winner.finalX;

            for (const auto& act : winner.actions) {
                m_verifiedPrefix.push_back(act);
            }

            AnchorPoint newAnchor;
            newAnchor.tick = m_frontierTick;
            newAnchor.x = m_currentX;
            newAnchor.p1Snapshot = winner.p1Snapshot;
            newAnchor.p2Snapshot = winner.p2Snapshot;
            newAnchor.hasPlayer2 = winner.hasPlayer2;
            newAnchor.actionPrefixCount = m_verifiedPrefix.size();
            m_anchors.push_back(newAnchor);

            m_temperature = 1.0f;
            m_waveAttempt = 0;
            m_currentWaveFailures.clear();
            m_activeCandidates.clear();

            bool isWinnerDone = winner.completed ||
                                m_currentX >= m_trueLevelLength ||
                                m_headlessPlayLayer->m_hasCompletedLevel ||
                                (m_headlessPlayLayer->getEndPosition().x > m_startX + 50.0f &&
                                 m_currentX >= m_headlessPlayLayer->getEndPosition().x - 10.0f);

            if (isWinnerDone) {
                m_isCompleted = true;
                m_isSolving = false;
                m_telemetry.status = SolverStatus::Solved;
                m_telemetry.isVerified = true;
                m_telemetry.detailMessage = "Level Solved (100% Verified)!";
                int lvlID = m_activeLevel ? m_activeLevel->m_levelID.value() : 0;
                std::string lvlName = m_activeLevel ? m_activeLevel->m_levelName : "";
                MacroManager::get().saveMacroForLevel(lvlID, lvlName, m_verifiedPrefix);
                MacroManager::get().setMacro(m_verifiedPrefix);
                geode::Notification::create("Level Solved! 100% Verified!", geode::NotificationIcon::Success)->show();
            }
        } else {
            // Wave Failure: Chronological Time Rewind
            m_rewindCount++;

            uint32_t targetTick = m_frontierTick > 35 ? m_frontierTick - 35 : 0;
            size_t bestAnchorIdx = 0;

            for (size_t i = 0; i < m_anchors.size(); ++i) {
                if (m_anchors[i].tick <= targetTick) {
                    bestAnchorIdx = i;
                } else {
                    break;
                }
            }

            m_anchors.resize(bestAnchorIdx + 1);
            const auto& rollbackAnchor = m_anchors[bestAnchorIdx];

            m_frontierTick = rollbackAnchor.tick;
            m_currentX = rollbackAnchor.x;
            m_verifiedPrefix.resize(rollbackAnchor.actionPrefixCount);

            m_temperature = std::min(5.0f, m_temperature + 0.6f);
            m_activeCandidates.clear();
        }
    }

    // Update Telemetry Metrics
    m_telemetry.currentX = m_currentX;
    m_telemetry.targetEndX = m_trueLevelLength;
    if (m_trueLevelLength > m_startX) {
        float pct = (m_currentX - m_startX) / (m_trueLevelLength - m_startX);
        m_telemetry.progressPercent = std::clamp(pct, 0.0f, 1.0f);
    }
    m_telemetry.activeWave = m_activeWave;
    m_telemetry.waveAttempt = m_waveAttempt;
    m_telemetry.frontierTick = m_frontierTick;
    m_telemetry.groundedAnchors = static_cast<uint32_t>(m_anchors.size());
    m_telemetry.rewindCount = m_rewindCount;
    m_telemetry.temperature = m_temperature;

    if (!m_anchors.empty()) {
        const auto& snap = m_anchors.back().p1Snapshot;
        if (snap.isDart) m_telemetry.activeMode = "Wave";
        else if (snap.isShip) m_telemetry.activeMode = "Ship";
        else if (snap.isBird) m_telemetry.activeMode = "UFO";
        else if (snap.isBall) m_telemetry.activeMode = "Ball";
        else if (snap.isRobot) m_telemetry.activeMode = "Robot";
        else if (snap.isSpider) m_telemetry.activeMode = "Spider";
        else if (snap.isSwing) m_telemetry.activeMode = "Swing";
        else m_telemetry.activeMode = "Cube";
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto durSec = std::chrono::duration<float>(now - m_speedTimer).count();
    if (durSec >= 0.5f) {
        m_telemetry.ticksPerSecond = static_cast<float>(m_ticksSampleCount) / durSec;
        m_ticksSampleCount = 0;
        m_speedTimer = now;
    }
}

