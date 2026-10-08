#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../core/CheatAPIIntegrator.hpp"

using namespace geode::prelude;

class $modify(SolverPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::playerDestroyed", geode::Priority::First);
    }

    void playerDestroyed(bool noEffects) {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            this->m_isDead = true;
            return;
        }
        PlayerObject::playerDestroyed(noEffects);
    }
};

class $modify(SolverBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick && MacroManager::get().isReplayActive()) {
            if (auto pl = typeinfo_cast<PlayLayer*>(this)) {
                if (!pl->m_inResetDelay && pl->m_started && !pl->m_playerDied && pl->m_player1 && !pl->m_player1->m_isDead) {
                    MacroManager::get().stepReplay(pl);
                }
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

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

        if (SwarmSolver::get().isHeadlessSimulating()) {
            // Do not register headless simulation layers as active gameplay PlayLayer
            return true;
        }

        MacroManager::get().onLevelEntered(this);
        return true;
    }

    void onQuit() {
        CheatAPIIntegrator::notifyCheatEnded();
        MacroManager::get().onLevelExited();
        PlayLayer::onQuit();
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

            // Real collision death with hazard or solid block/ceiling/floor
            if (player) {
                player->m_isDead = true;
            }
            this->m_playerDied = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
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
};



