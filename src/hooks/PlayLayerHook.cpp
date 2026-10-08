#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../core/CheatAPIIntegrator.hpp"

using namespace geode::prelude;

class $modify(SolverBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (MacroManager::get().isReplayActive()) {
            if (!MacroManager::get().isInjectingInput()) {
                // Block all human user clicks / keystrokes during bot replay!
                return;
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(SolverPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        MacroManager::get().onLevelEntered(this);
        return true;
    }

    void onQuit() {
        CheatAPIIntegrator::notifyCheatEnded();
        MacroManager::get().onLevelExited();
        PlayLayer::onQuit();
    }

    void update(float dt) {
        if (MacroManager::get().isReplayActive()) {
            MacroManager::get().stepReplay(this);
        }
        PlayLayer::update(dt);
    }

    void resetLevel() {
        bool isHeadless = SwarmSolver::get().isHeadlessSimulating();
        bool isReplaying = MacroManager::get().isReplayActive();

        if (isHeadless || isReplaying) {
            int prevAttempts = this->m_attempts;
            int prevLvlAttempts = this->m_level ? this->m_level->m_attempts.value() : 0;

            PlayLayer::resetLevel();

            // Freeze attempts so bots / headless waves never inflate attempt stats
            this->m_attempts = prevAttempts;
            if (this->m_level) {
                this->m_level->m_attempts = prevLvlAttempts;
            }

            if (isReplaying) {
                MacroManager::get().resetPlayback();
            }
            return;
        }

        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            // 1. Ignore spawn anti-cheat spike
            if (object && (object == this->m_anticheatSpike || (object->m_objectID == 8 && object->getPositionX() <= 30.0f))) {
                return;
            }

            // 2. Ignore camera culling / off-screen boundary deaths (object == nullptr) unless fallen into void
            if (!object) {
                if (player && player->getPositionY() < -50.0f) {
                    player->m_isDead = true;
                    this->m_playerDied = true;
                }
                return;
            }

            // 3. Real hazard collision death
            if (player) {
                player->m_isDead = true;
            }
            this->m_playerDied = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            this->m_hasCompletedLevel = true;
            return;
        }
        if (MacroManager::get().isReplayActive()) {
            CheatAPIIntegrator::notifyCheatStarted();
        }
        PlayLayer::levelComplete();
    }

    void showEndLayer() {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            this->m_hasCompletedLevel = true;
            return;
        }
        if (MacroManager::get().isReplayActive()) {
            CheatAPIIntegrator::notifyCheatStarted();
        }
        PlayLayer::showEndLayer();
    }

    void playEndAnimationToPos(cocos2d::CCPoint position) {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            this->m_hasCompletedLevel = true;
            return;
        }
        PlayLayer::playEndAnimationToPos(position);
    }

    void playPlatformerEndAnimationToPos(cocos2d::CCPoint position, bool instant) {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            this->m_hasCompletedLevel = true;
            return;
        }
        PlayLayer::playPlatformerEndAnimationToPos(position, instant);
    }

    void checkForEnd() {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            if (this->m_player1 && this->m_player1->getPositionX() >= this->getEndPosition().x - 10.0f) {
                this->m_hasCompletedLevel = true;
                return;
            }
        }
        PlayLayer::checkForEnd();
    }

};


