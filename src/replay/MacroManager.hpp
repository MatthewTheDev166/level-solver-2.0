#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"
#include <vector>
#include <string>

class MacroManager {
public:
    static MacroManager& get() {
        static MacroManager instance;
        return instance;
    }

    void onLevelEntered(PlayLayer* playLayer);
    void onLevelExited();

    void setMacro(const std::vector<Action>& actions);
    const std::vector<Action>& getMacro() const { return m_actions; }
    bool hasMacro() const { return !m_actions.empty(); }

    void startReplay(GJGameLevel* level = nullptr);
    void stopReplay();
    bool isReplayActive() const { return m_replayActive; }
    void stepReplay(PlayLayer* playLayer);

    std::string exportActiveMacro(GJGameLevel* level = nullptr);

private:
    MacroManager() = default;

    PlayLayer* m_playLayer = nullptr;
    std::vector<Action> m_actions;
    size_t m_currentActionIndex = 0;
    uint32_t m_replayTick = 0;
    bool m_replayActive = false;
};
