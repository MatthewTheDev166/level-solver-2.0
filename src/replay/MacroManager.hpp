#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"
#include <vector>
#include <string>
#include <unordered_map>

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

    bool hasMacroForLevel(int levelID, const std::string& levelName) const;
    bool loadMacroForLevel(int levelID, const std::string& levelName);
    void saveMacroForLevel(int levelID, const std::string& levelName, const std::vector<Action>& actions);

    void startReplay(GJGameLevel* level = nullptr);
    void stopReplay();
    bool isReplayActive() const { return m_replayActive; }
    bool isInjectingInput() const { return m_isInjectingInput; }
    void stepReplay(PlayLayer* playLayer);

    void resetPlayback() {
        m_currentActionIndex = 0;
        m_replayTick = 0;
    }

    std::string exportActiveMacro(GJGameLevel* level = nullptr);

    PlayLayer* getActivePlayLayer();

private:
    MacroManager() = default;

    PlayLayer* m_playLayer = nullptr;
    std::vector<Action> m_actions;
    size_t m_currentActionIndex = 0;
    uint32_t m_replayTick = 0;
    bool m_replayActive = false;
    bool m_isInjectingInput = false;
    bool m_hasRecordedMacro = false;

    std::unordered_map<int, std::vector<Action>> m_levelMacros;
    std::unordered_map<std::string, std::vector<Action>> m_nameMacros;
};

