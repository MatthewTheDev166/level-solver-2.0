#include "MacroManager.hpp"
#include "../core/CheatAPIIntegrator.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <matjson.hpp>

static std::string sanitizeLevelName(const std::string& name) {
    std::string clean = name;
    for (char& c : clean) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (clean.empty()) clean = "Unnamed";
    return clean;
}

void MacroManager::onLevelEntered(PlayLayer* playLayer) {
    m_playLayer = playLayer;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::onLevelExited() {
    stopReplay();
    m_playLayer = nullptr;
}

void MacroManager::setMacro(const std::vector<Action>& actions) {
    m_actions = actions;
    std::sort(m_actions.begin(), m_actions.end(), [](const Action& a, const Action& b) {
        return a.tick < b.tick;
    });
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::saveMacroForLevel(int levelID, const std::string& levelName, const std::vector<Action>& actions) {
    if (levelID > 0) {
        m_levelMacros[levelID] = actions;
    }
    if (!levelName.empty()) {
        m_nameMacros[levelName] = actions;
    }
    setMacro(actions);
}

bool MacroManager::hasMacroForLevel(int levelID, const std::string& levelName) const {
    if (levelID > 0) {
        auto it = m_levelMacros.find(levelID);
        if (it != m_levelMacros.end() && !it->second.empty()) return true;
    }
    if (!levelName.empty()) {
        auto it = m_nameMacros.find(levelName);
        if (it != m_nameMacros.end() && !it->second.empty()) return true;
    }

    std::string clean = sanitizeLevelName(levelName);
    std::filesystem::path p1 = geode::dirs::getGameDir() / "replays" / (clean + "-macro.json");
    if (std::filesystem::exists(p1)) return true;

    std::filesystem::path p2 = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Geometry Dash\\replays\\" + clean + "-macro.json";
    if (std::filesystem::exists(p2)) return true;

    return false;
}

bool MacroManager::loadMacroForLevel(int levelID, const std::string& levelName) {
    if (levelID > 0) {
        auto it = m_levelMacros.find(levelID);
        if (it != m_levelMacros.end() && !it->second.empty()) {
            setMacro(it->second);
            return true;
        }
    }
    if (!levelName.empty()) {
        auto it = m_nameMacros.find(levelName);
        if (it != m_nameMacros.end() && !it->second.empty()) {
            setMacro(it->second);
            return true;
        }
    }

    std::string clean = sanitizeLevelName(levelName);
    std::filesystem::path p = geode::dirs::getGameDir() / "replays" / (clean + "-macro.json");
    if (!std::filesystem::exists(p)) {
        p = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Geometry Dash\\replays\\" + clean + "-macro.json";
    }

    if (std::filesystem::exists(p)) {
        std::ifstream f(p);
        if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            auto res = matjson::parse(content);
            if (res.isOk()) {
                auto obj = res.unwrap();
                if (obj.contains("inputs") && obj["inputs"].isArray()) {
                    std::vector<Action> acts;
                    for (auto& item : obj["inputs"].asArray().unwrap()) {
                        Action a;
                        a.tick = item["frame"].as<uint32_t>().unwrapOrDefault();
                        a.down = item["down"].as<bool>().unwrapOrDefault();
                        a.button = item.contains("button") ? item["button"].as<int>().unwrapOrDefault() : (item.contains("btn") ? item["btn"].as<int>().unwrapOrDefault() : 1);
                        a.player2 = item.contains("player2") ? item["player2"].as<bool>().unwrapOrDefault() : (item.contains("p2") ? item["p2"].as<bool>().unwrapOrDefault() : false);
                        acts.push_back(a);
                    }
                    if (!acts.empty()) {
                        saveMacroForLevel(levelID, levelName, acts);
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

void MacroManager::startReplay(GJGameLevel* level) {
    if (m_actions.empty()) {
        geode::Notification::create("No macro recorded yet!", geode::NotificationIcon::Warning)->show();
        return;
    }

    m_replayActive = true;
    m_currentActionIndex = 0;
    m_replayTick = 0;

    CheatAPIIntegrator::notifyCheatStarted();

    if (m_playLayer) {
        m_playLayer->resetLevel();
        geode::Notification::create("Replaying solution in GD...", geode::NotificationIcon::Info)->show();
    } else if (level) {
        geode::Notification::create("Launching replay...", geode::NotificationIcon::Info)->show();
        auto scene = PlayLayer::scene(level, false, false);
        cocos2d::CCDirector::sharedDirector()->replaceScene(scene);
    }
}

void MacroManager::stopReplay() {
    CheatAPIIntegrator::notifyCheatEnded();
    m_replayActive = false;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::stepReplay(PlayLayer* playLayer) {
    if (!m_replayActive || !playLayer) return;

    // Apply inputs for current tick with injection guard
    m_isInjectingInput = true;
    while (m_currentActionIndex < m_actions.size() && m_actions[m_currentActionIndex].tick <= m_replayTick) {
        const auto& act = m_actions[m_currentActionIndex];
        playLayer->handleButton(act.down, act.button, !act.player2);
        m_currentActionIndex++;
    }
    m_isInjectingInput = false;

    m_replayTick++;

    // Check completion
    if (playLayer->m_hasCompletedLevel) {
        stopReplay();
        geode::Notification::create("Replay Complete!", geode::NotificationIcon::Success)->show();
    } else if (m_currentActionIndex >= m_actions.size() && playLayer->m_player1 && playLayer->m_player1->m_isDead) {
        stopReplay();
    }
}

std::string MacroManager::exportActiveMacro(GJGameLevel* level) {
    if (m_actions.empty()) {
        geode::Notification::create("No macro available to export!", geode::NotificationIcon::Warning)->show();
        return "";
    }

    int levelID = 0;
    std::string levelName = "Unnamed";
    if (level) {
        levelID = level->m_levelID.value();
        levelName = level->m_levelName;
    } else if (m_playLayer && m_playLayer->m_level) {
        levelID = m_playLayer->m_level->m_levelID.value();
        levelName = m_playLayer->m_level->m_levelName;
    }

    std::string cleanName = sanitizeLevelName(levelName);
    float duration = m_actions.empty() ? 0.0f : static_cast<float>(m_actions.back().tick) / 240.0f;

    // Build standard Mega Hack v8/v9 ReplayBot compatible JSON
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"author\": \"LevelSolver\",\n";
    ss << "  \"description\": \"Solved by LevelSolver Autonomous AI\",\n";
    ss << "  \"duration\": " << duration << ",\n";
    ss << "  \"gameVersion\": 22081,\n";
    ss << "  \"framerate\": 240,\n";
    ss << "  \"seed\": 1337,\n";
    ss << "  \"coins\": 0,\n";
    ss << "  \"ldm\": false,\n";
    ss << "  \"platformer\": false,\n";
    ss << "  \"bot\": {\n";
    ss << "    \"name\": \"LevelSolver\",\n";
    ss << "    \"version\": \"2.0\"\n";
    ss << "  },\n";
    ss << "  \"level\": {\n";
    ss << "    \"id\": " << levelID << ",\n";
    ss << "    \"name\": \"" << cleanName << "\"\n";
    ss << "  },\n";
    ss << "  \"inputs\": [\n";

    for (size_t i = 0; i < m_actions.size(); ++i) {
        const auto& act = m_actions[i];
        ss << "    { \"frame\": " << act.tick
           << ", \"button\": " << act.button
           << ", \"player2\": " << (act.player2 ? "true" : "false")
           << ", \"down\": " << (act.down ? "true" : "false") << " }";
        if (i + 1 < m_actions.size()) {
            ss << ",";
        }
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";

    std::string jsonStr = ss.str();

    // Determine target replays directory (C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\replays)
    std::filesystem::path exportDir = geode::dirs::getGameDir() / "replays";
    std::error_code ec;
    std::filesystem::create_directories(exportDir, ec);

    if (!std::filesystem::exists(exportDir)) {
        exportDir = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Geometry Dash\\replays";
        std::filesystem::create_directories(exportDir, ec);
    }

    std::filesystem::path exportPath = exportDir / (cleanName + "-macro.json");
    std::ofstream outFile(exportPath);
    if (outFile.is_open()) {
        outFile << jsonStr;
        outFile.close();
        geode::Notification::create("Exported to " + exportPath.filename().string(), geode::NotificationIcon::Success)->show();
    } else {
        geode::Notification::create("Failed to write macro to replays folder!", geode::NotificationIcon::Error)->show();
    }

    return jsonStr;
}

