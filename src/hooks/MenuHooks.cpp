#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
#include "../menu/MenuManager.hpp"
#include "../ui/SolverOverlay.hpp"

using namespace geode::prelude;

class $modify(SolverEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        MenuManager::get().setEditLevelLayer(this);
        return true;
    }

    void onExit() {
        MenuManager::get().clearEditLevelLayer(this);
        EditLevelLayer::onExit();
    }
};

class $modify(SolverLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        MenuManager::get().setLevelInfoLayer(this);
        return true;
    }

    void onExit() {
        MenuManager::get().clearLevelInfoLayer(this);
        LevelInfoLayer::onExit();
    }
};

class $modify(SolverLevelSelectLayer, LevelSelectLayer) {
    bool init(int page) {
        if (!LevelSelectLayer::init(page)) return false;
        MenuManager::get().setLevelSelectLayer(this);
        return true;
    }

    void onExit() {
        MenuManager::get().clearLevelSelectLayer(this);
        LevelSelectLayer::onExit();
    }
};

$execute {
    KeyboardInputEvent().listen([](auto& evt) {
        if (evt.action == KeyboardInputData::Action::Press) {
            if (evt.key == cocos2d::KEY_RightShift || evt.key == cocos2d::KEY_F8) {
                if (MenuManager::get().isInAllowedMenu()) {
                    SolverOverlay::toggleVisibility();
                    return ListenerResult::Stop;
                }
            }
        }
        return ListenerResult::Propagate;
    }).leak();
}
