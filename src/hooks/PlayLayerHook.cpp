#include <Geode/modify/PlayLayer.hpp>
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"

using namespace geode::prelude;

class $modify(SolverPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        MacroManager::get().onLevelEntered(this);
        return true;
    }

    void onQuit() {
        MacroManager::get().onLevelExited();
        PlayLayer::onQuit();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (MacroManager::get().isReplayActive()) {
            MacroManager::get().stepReplay(this);
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

