#include "SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../core/CheatAPIIntegrator.hpp"
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <algorithm>

using namespace geode::prelude;

SwarmSolver::~SwarmSolver() {
    cleanupHeadless();
}

void SwarmSolver::cleanupHeadless() {
    CheatAPIIntegrator::notifyCheatEnded();
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
            auto str = glm->getMainLevelString(level->m_levelID.value());
            if (!str.empty()) {
                level->m_levelString = str;
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
    m_headlessPlayLayer->m_isPracticeMode = true;
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
    float maxObjX = 0.0f;
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

    float robtopEnd = m_headlessPlayLayer ? m_headlessPlayLayer->getEndPosition().x : 1065.0f;
    m_trueLevelLength = std::max({maxObjX, robtopEnd, 1065.0f});
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

    // 1. Idle (no input)
    addBot({ { startTick, false, 1, false } });

    // 2. Full hold
    addBot({ { startTick, true, 1, false } });

    // 3. Release sweeps if starts held
    if (startsHeld) {
        for (uint32_t t = 1; t < horizon - 1; ++t) {
            addBot({ { startTick + t, false, 1, false } });
        }
    }

    // 4. Single jumps at every 2 ticks with varied durations
    for (uint32_t t = 0; t < horizon; t += 2) {
        for (uint32_t dur : { 2u, 4u, 8u, 14u, 24u, 36u }) {
            std::vector<Action> acts;
            acts.push_back({ startTick + t, true, 1, false });
            if (t + dur < horizon) {
                acts.push_back({ startTick + t + dur, false, 1, false });
            }
            addBot(acts);
        }
    }

    // 5. Double taps
    for (uint32_t t1 = 0; t1 < horizon / 2; t1 += 4) {
        for (uint32_t gap : { 6u, 12u, 18u }) {
            uint32_t t2 = t1 + gap;
            if (t2 + 4 < horizon) {
                std::vector<Action> acts;
                acts.push_back({ startTick + t1, true, 1, false });
                acts.push_back({ startTick + t1 + 3, false, 1, false });
                acts.push_back({ startTick + t2, true, 1, false });
                acts.push_back({ startTick + t2 + 3, false, 1, false });
                addBot(acts);
            }
        }
    }

    // 6. Targeted mutations from previous wave attempts
    if (m_waveAttempt > 0 && !m_currentWaveSurvivors.empty()) {
        for (const auto& parent : m_currentWaveSurvivors) {
            if (swarm.size() >= static_cast<size_t>(m_populationSize)) break;
            if (parent.deathTick > startTick) {
                uint32_t deathLocal = parent.deathTick - startTick;
                for (uint32_t lead : { 1u, 2u, 4u, 7u, 12u, 18u, 25u }) {
                    if (deathLocal >= lead) {
                        uint32_t flipT = deathLocal - lead;
                        std::vector<Action> flipped = parent.actions;
                        flipped.erase(std::remove_if(flipped.begin(), flipped.end(),
                            [startTick, flipT](const Action& a) { return a.tick >= startTick + flipT; }), flipped.end());
                        flipped.push_back({ startTick + flipT, true, 1, false });
                        if (flipT + 8 < horizon) {
                            flipped.push_back({ startTick + flipT + 8, false, 1, false });
                        }
                        addBot(flipped);
                    }
                }
            }
        }
    }

    // 7. Randomized fill to saturate population
    std::uniform_int_distribution<uint32_t> tickDist(0, horizon - 1);
    std::uniform_int_distribution<int> boolDist(0, 1);
    while (swarm.size() < static_cast<size_t>(m_populationSize)) {
        std::vector<Action> rndActs;
        uint32_t num = 1 + (m_rng() % 3);
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
            bot.finalX = m_headlessPlayLayer->m_player1->getPositionX();
            bot.landedSafely = false;
            m_headlessSimulating = false;
            return;
        }

        if (m_headlessPlayLayer->m_hasCompletedLevel || m_headlessPlayLayer->m_player1->getPositionX() >= m_trueLevelLength) {
            bot.completed = true;
            bot.finalX = m_headlessPlayLayer->m_player1->getPositionX();
            bot.landedSafely = true;
            m_headlessSimulating = false;
            return;
        }
    }

    bot.finalX = m_headlessPlayLayer->m_player1->getPositionX();
    bool isFlying = m_headlessPlayLayer->m_player1->m_isShip ||
                    m_headlessPlayLayer->m_player1->m_isBird ||
                    m_headlessPlayLayer->m_player1->m_isDart ||
                    m_headlessPlayLayer->m_player1->m_isSwing;
    bot.landedSafely = isFlying || m_headlessPlayLayer->m_player1->m_isOnGround;
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
        m_telemetry.totalBots = m_populationSize;
        m_telemetry.aliveBots = m_populationSize;
    }

    // Step candidates within frame budget
    while (m_currentCandidateIdx < m_activeCandidates.size()) {
        auto& bot = m_activeCandidates[m_currentCandidateIdx++];
        simulateCandidate(bot, m_frontierTick, m_horizonTicks);

        if (bot.died) {
            if (m_telemetry.aliveBots > 0) m_telemetry.aliveBots--;
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
                if (bot.finalX > bestFitness) {
                    bestFitness = bot.finalX;
                    bestIdx = static_cast<int>(i);
                }
            }
        }

        if (bestIdx >= 0) {
            // Frontier Advancement
            const auto& winner = m_activeCandidates[bestIdx];

            m_frontierTick += m_horizonTicks;
            m_currentX = winner.finalX;

            for (const auto& act : winner.actions) {
                m_verifiedPrefix.push_back(act);
            }

            AnchorPoint newAnchor;
            newAnchor.tick = m_frontierTick;
            newAnchor.x = m_currentX;
            newAnchor.p1Snapshot.capture(m_headlessPlayLayer->m_player1);
            if (m_headlessPlayLayer->m_player2) {
                newAnchor.p2Snapshot.capture(m_headlessPlayLayer->m_player2);
                newAnchor.hasPlayer2 = true;
            }
            newAnchor.actionPrefixCount = m_verifiedPrefix.size();
            m_anchors.push_back(newAnchor);

            m_temperature = 1.0f;
            m_waveAttempt = 0;
            m_activeCandidates.clear();

            if (winner.completed || m_currentX >= m_trueLevelLength || m_headlessPlayLayer->m_hasCompletedLevel) {
                m_isCompleted = true;
                m_isSolving = false;
                m_telemetry.status = SolverStatus::Solved;
                m_telemetry.isVerified = true;
                m_telemetry.detailMessage = "Level Solved (100% Verified)!";
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
    if (m_headlessPlayLayer && m_headlessPlayLayer->m_player1) {
        auto p = m_headlessPlayLayer->m_player1;
        if (p->m_isDart) m_telemetry.activeMode = "Wave";
        else if (p->m_isShip) m_telemetry.activeMode = "Ship";
        else if (p->m_isBird) m_telemetry.activeMode = "UFO";
        else if (p->m_isBall) m_telemetry.activeMode = "Ball";
        else if (p->m_isRobot) m_telemetry.activeMode = "Robot";
        else if (p->m_isSpider) m_telemetry.activeMode = "Spider";
        else if (p->m_isSwing) m_telemetry.activeMode = "Swing";
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

