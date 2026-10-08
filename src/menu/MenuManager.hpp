#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"

class MenuManager {
public:
    static MenuManager& get() {
        static MenuManager instance;
        return instance;
    }

    bool isInAllowedMenu() const;
    MenuLevelInfo getActiveLevelInfo() const;

    void setEditLevelLayer(EditLevelLayer* layer);
    void clearEditLevelLayer(EditLevelLayer* layer);

    void setLevelInfoLayer(LevelInfoLayer* layer);
    void clearLevelInfoLayer(LevelInfoLayer* layer);

    void setLevelSelectLayer(LevelSelectLayer* layer);
    void clearLevelSelectLayer(LevelSelectLayer* layer);

private:
    MenuManager() = default;

    EditLevelLayer* m_editLevelLayer = nullptr;
    LevelInfoLayer* m_levelInfoLayer = nullptr;
    LevelSelectLayer* m_levelSelectLayer = nullptr;
};
