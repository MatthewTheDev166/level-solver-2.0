#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/UILayer.hpp>
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../ui/SolverOverlay.hpp"

class $modify(SolverPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        SwarmSolver::get().onLevelEntered(this);
        MacroManager::get().onLevelEntered(this);
        return true;
    }

    void onQuit() {
        SwarmSolver::get().onLevelExited();
        MacroManager::get().onLevelExited();
        PlayLayer::onQuit();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (MacroManager::get().isReplayActive()) {
            MacroManager::get().stepReplay(this);
        } else if (SwarmSolver::get().isSolving()) {
            SwarmSolver::get().update(this);
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (SwarmSolver::get().isHeadlessSimulating()) {
            if (player) {
                player->m_isDead = true;
            }
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(SolverUILayer, UILayer) {
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        if (key == cocos2d::KEY_RightShift || key == cocos2d::KEY_F8) {
            SolverOverlay::toggleVisibility();
            return;
        }
        UILayer::keyDown(key, timestamp);
    }
};
