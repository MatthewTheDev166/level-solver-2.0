#include "MenuManager.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>

using namespace geode::prelude;

bool MenuManager::isInAllowedMenu() const {
    if (m_editLevelLayer || m_levelInfoLayer || m_levelSelectLayer) {
        return true;
    }

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;

    if (scene->getChildByType<EditLevelLayer>(0)) return true;
    if (scene->getChildByType<LevelInfoLayer>(0)) return true;
    if (scene->getChildByType<LevelSelectLayer>(0)) return true;

    return false;
}

MenuLevelInfo MenuManager::getActiveLevelInfo() const {
    MenuLevelInfo info;
    auto scene = CCDirector::sharedDirector()->getRunningScene();

    // 1. EditLevelLayer
    auto edit = m_editLevelLayer ? m_editLevelLayer : (scene ? scene->getChildByType<EditLevelLayer>(0) : nullptr);
    if (edit && edit->m_level) {
        info.level = edit->m_level;
        info.levelName = edit->m_level->m_levelName;
        info.creatorName = edit->m_level->m_creatorName.empty() ? "You" : edit->m_level->m_creatorName;
        info.levelID = edit->m_level->m_levelID.value();
        info.stars = edit->m_level->m_stars.value();
        info.isCustom = true;
        info.context = MenuContext::EditLevel;
        return info;
    }

    // 2. LevelInfoLayer
    auto lvlInfo = m_levelInfoLayer ? m_levelInfoLayer : (scene ? scene->getChildByType<LevelInfoLayer>(0) : nullptr);
    if (lvlInfo && lvlInfo->m_level) {
        info.level = lvlInfo->m_level;
        info.levelName = lvlInfo->m_level->m_levelName;
        info.creatorName = lvlInfo->m_level->m_creatorName.empty() ? "-" : lvlInfo->m_level->m_creatorName;
        info.levelID = lvlInfo->m_level->m_levelID.value();
        info.stars = lvlInfo->m_level->m_stars.value();
        info.isCustom = false;
        info.context = MenuContext::LevelInfo;
        return info;
    }

    // 3. LevelSelectLayer
    auto select = m_levelSelectLayer ? m_levelSelectLayer : (scene ? scene->getChildByType<LevelSelectLayer>(0) : nullptr);
    if (select) {
        int page = 0;
        if (select->m_scrollLayer) {
            page = select->m_scrollLayer->m_page;
        }
        auto level = GameLevelManager::sharedState()->getMainLevel(page + 1, false);
        if (level) {
            info.level = level;
            info.levelName = level->m_levelName;
            info.creatorName = "RobTop";
            info.levelID = level->m_levelID.value();
            info.stars = level->m_stars.value();
            info.isCustom = false;
            info.context = MenuContext::LevelSelect;
            return info;
        }
    }

    return info;
}

void MenuManager::setEditLevelLayer(EditLevelLayer* layer) { m_editLevelLayer = layer; }
void MenuManager::clearEditLevelLayer(EditLevelLayer* layer) { if (m_editLevelLayer == layer) m_editLevelLayer = nullptr; }

void MenuManager::setLevelInfoLayer(LevelInfoLayer* layer) { m_levelInfoLayer = layer; }
void MenuManager::clearLevelInfoLayer(LevelInfoLayer* layer) { if (m_levelInfoLayer == layer) m_levelInfoLayer = nullptr; }

void MenuManager::setLevelSelectLayer(LevelSelectLayer* layer) { m_levelSelectLayer = layer; }
void MenuManager::clearLevelSelectLayer(LevelSelectLayer* layer) { if (m_levelSelectLayer == layer) m_levelSelectLayer = nullptr; }
