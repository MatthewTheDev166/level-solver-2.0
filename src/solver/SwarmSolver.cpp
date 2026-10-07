#include "SwarmSolver.hpp"
#include "../engine/HeadlessEngine.hpp"
#include "../replay/MacroManager.hpp"
#include <algorithm>

using namespace geode::prelude;

void SwarmSolver::onLevelEntered(PlayLayer* playLayer) {
    m_playLayer = playLayer;
    m_inLevel = true;
    m_isSolving = false;
    m_headlessSimulating = false;
    m_frontierTick = 0;
    m_rewindCount = 0;
    m_temperature = 1.0f;
    m_verifiedPrefix.clear();
    m_anchors.clear();

    calculateTrueLevelLength();
    captureInitialAnchor();
}

void SwarmSolver::onLevelExited() {
    m_playLayer = nullptr;
    m_inLevel = false;
    m_isSolving = false;
    m_headlessSimulating = false;
    m_verifiedPrefix.clear();
    m_anchors.clear();
}

void SwarmSolver::calculateTrueLevelLength() {
    float maxObjX = 0.0f;
    if (m_playLayer && m_playLayer->m_objects) {
        for (unsigned int i = 0; i < m_playLayer->m_objects->count(); ++i) {
            if (auto obj = geode::cast::typeinfo_cast<GameObject*>(m_playLayer->m_objects->objectAtIndex(i))) {
                float right = obj->getPositionX() + 90.0f;
                if (right > maxObjX) {
                    maxObjX = right;
                }
            }
        }
    }

    if (maxObjX > 100.0f) {
        m_trueLevelLength = maxObjX;
    } else if (m_playLayer && m_playLayer->m_levelLength > 0.0f) {
        m_trueLevelLength = m_playLayer->m_levelLength;
    } else {
        m_trueLevelLength = 1065.0f;
    }
}

void SwarmSolver::captureInitialAnchor() {
    if (!m_playLayer || !m_playLayer->m_player1) return;

    m_startX = m_playLayer->m_player1->getPositionX();
    m_currentX = m_startX;

    AnchorPoint initial;
    initial.tick = 0;
    initial.x = m_startX;
    initial.p1Snapshot.capture(m_playLayer->m_player1);
    if (m_playLayer->m_player2) {
        initial.p2Snapshot.capture(m_playLayer->m_player2);
        initial.hasPlayer2 = true;
    }
    initial.actionPrefixCount = 0;

    m_anchors.clear();
    m_anchors.push_back(initial);
}

void SwarmSolver::start() {
    if (!m_inLevel || !m_playLayer) {
        geode::Notification::create("Enter a level to start solving!", geode::NotificationIcon::Warning)->show();
        return;
    }

    if (m_anchors.empty()) {
        captureInitialAnchor();
    }

    m_isSolving = true;
    geode::Notification::create("Solver Started", geode::NotificationIcon::Info)->show();
}

void SwarmSolver::pause() {
    m_isSolving = false;
    geode::Notification::create("Solver Paused", geode::NotificationIcon::Info)->show();
}

void SwarmSolver::replay() {
    if (m_verifiedPrefix.empty()) {
        geode::Notification::create("No solved macro to replay!", geode::NotificationIcon::Warning)->show();
        return;
    }

    pause();
    MacroManager::get().setMacro(m_verifiedPrefix);
    MacroManager::get().startReplay();
}

float SwarmSolver::getProgressPercent() const {
    if (m_trueLevelLength <= m_startX) return 0.0f;
    float pct = (m_currentX - m_startX) / (m_trueLevelLength - m_startX);
    return std::clamp(pct, 0.0f, 1.0f);
}

void SwarmSolver::update(PlayLayer* playLayer) {
    if (!m_isSolving || !playLayer || !m_inLevel) return;

    runWave();
}

std::vector<BotCandidate> SwarmSolver::generateSwarm() {
    std::vector<BotCandidate> swarm;
    swarm.reserve(m_populationSize);

    // Bot 0: No action (coast)
    BotCandidate coastBot;
    coastBot.id = 0;
    swarm.push_back(coastBot);

    std::uniform_int_distribution<int> offsetDist(0, std::max(0, m_horizonTicks - 5));
    std::uniform_int_distribution<int> durDist(2, std::min(40, m_horizonTicks));
    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);

    for (int i = 1; i < m_populationSize; ++i) {
        BotCandidate bot;
        bot.id = i;

        int offset = offsetDist(m_rng);
        int duration = std::clamp(durDist(m_rng), 2, std::max(2, m_horizonTicks - offset));

        uint32_t pressTick = m_frontierTick + offset;
        uint32_t releaseTick = pressTick + duration;

        bot.actions.push_back({pressTick, true, 1, false});
        bot.actions.push_back({releaseTick, false, 1, false});

        // Chance for secondary jump with higher temperature
        if (probDist(m_rng) < (0.2f * m_temperature) && (offset + duration + 6 < m_horizonTicks)) {
            int secondOffset = offset + duration + 3;
            int secondDur = std::clamp(durDist(m_rng), 2, std::max(2, m_horizonTicks - secondOffset));
            uint32_t press2 = m_frontierTick + secondOffset;
            uint32_t rel2 = press2 + secondDur;

            bot.actions.push_back({press2, true, 1, false});
            bot.actions.push_back({rel2, false, 1, false});
        }

        // Dual mode jump mutation
        if (m_playLayer && m_playLayer->m_player2 && probDist(m_rng) < 0.3f) {
            bot.actions.push_back({pressTick, true, 1, true});
            bot.actions.push_back({releaseTick, false, 1, true});
        }

        swarm.push_back(bot);
    }

    return swarm;
}

void SwarmSolver::runWave() {
    if (!m_playLayer || !m_playLayer->m_player1 || m_anchors.empty()) return;

    AnchorPoint currentAnchor = m_anchors.back();
    auto candidates = generateSwarm();

    m_headlessSimulating = true;
    m_aliveBots = m_populationSize;

    // Simulate all bot candidates headlessly
    for (auto& bot : candidates) {
        // Restore player state to current frontier anchor
        currentAnchor.p1Snapshot.restore(m_playLayer->m_player1);
        if (currentAnchor.hasPlayer2 && m_playLayer->m_player2) {
            currentAnchor.p2Snapshot.restore(m_playLayer->m_player2);
        }

        bool survived = HeadlessEngine::stepCandidate(m_playLayer, bot, m_frontierTick, m_horizonTicks, m_trueLevelLength);
        if (!survived || bot.died) {
            m_aliveBots--;
        }
    }

    m_headlessSimulating = false;

    // Find best candidate satisfying Grounded Anchor Rule
    int bestIdx = -1;
    float bestFitness = -1.0f;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& bot = candidates[i];
        if (!bot.died && (bot.landedSafely || bot.completed)) {
            if (bot.finalX > bestFitness) {
                bestFitness = bot.finalX;
                bestIdx = static_cast<int>(i);
            }
        }
    }

    if (bestIdx >= 0) {
        // Frontier Advancement
        const auto& winner = candidates[bestIdx];

        // Step winner again to reach exact state at T + H
        currentAnchor.p1Snapshot.restore(m_playLayer->m_player1);
        if (currentAnchor.hasPlayer2 && m_playLayer->m_player2) {
            currentAnchor.p2Snapshot.restore(m_playLayer->m_player2);
        }

        HeadlessEngine::stepCandidate(m_playLayer, const_cast<BotCandidate&>(winner), m_frontierTick, m_horizonTicks, m_trueLevelLength);

        m_frontierTick += m_horizonTicks;
        m_currentX = m_playLayer->m_player1->getPositionX();

        for (const auto& act : winner.actions) {
            m_verifiedPrefix.push_back(act);
        }

        AnchorPoint newAnchor;
        newAnchor.tick = m_frontierTick;
        newAnchor.x = m_currentX;
        newAnchor.p1Snapshot.capture(m_playLayer->m_player1);
        if (m_playLayer->m_player2) {
            newAnchor.p2Snapshot.capture(m_playLayer->m_player2);
            newAnchor.hasPlayer2 = true;
        }
        newAnchor.actionPrefixCount = m_verifiedPrefix.size();
        m_anchors.push_back(newAnchor);

        m_temperature = 1.0f;

        // Check if level completed
        if (winner.completed || m_currentX >= m_trueLevelLength || m_playLayer->m_hasCompletedLevel) {
            m_isSolving = false;
            MacroManager::get().setMacro(m_verifiedPrefix);
            geode::Notification::create("Level Solved! 100% Completed!", geode::NotificationIcon::Success)->show();
        }
    } else {
        // Wave Failure: Grounded Time-Rollback
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

        rollbackAnchor.p1Snapshot.restore(m_playLayer->m_player1);
        if (rollbackAnchor.hasPlayer2 && m_playLayer->m_player2) {
            rollbackAnchor.p2Snapshot.restore(m_playLayer->m_player2);
        }

        m_temperature = std::min(5.0f, m_temperature + 0.6f);
    }
}

