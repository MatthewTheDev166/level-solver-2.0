#include "MacroManager.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>

void MacroManager::onLevelEntered(PlayLayer* playLayer) {
    m_playLayer = playLayer;
    m_replayActive = false;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::onLevelExited() {
    m_playLayer = nullptr;
    m_replayActive = false;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::setMacro(const std::vector<Action>& actions) {
    m_actions = actions;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::startReplay(GJGameLevel* level) {
    if (m_actions.empty()) {
        geode::Notification::create("No macro recorded yet!", geode::NotificationIcon::Warning)->show();
        return;
    }

    m_replayActive = true;
    m_currentActionIndex = 0;
    m_replayTick = 0;

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
    m_replayActive = false;
    m_currentActionIndex = 0;
    m_replayTick = 0;
}

void MacroManager::stepReplay(PlayLayer* playLayer) {
    if (!m_replayActive || !playLayer) return;

    // Apply inputs for current tick
    while (m_currentActionIndex < m_actions.size() && m_actions[m_currentActionIndex].tick <= m_replayTick) {
        const auto& act = m_actions[m_currentActionIndex];
        playLayer->handleButton(act.down, act.button, !act.player2);
        m_currentActionIndex++;
    }

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

    std::string levelID = "custom";
    if (m_playLayer && m_playLayer->m_level) {
        levelID = std::to_string(m_playLayer->m_level->m_levelID.value());
    } else if (level) {
        levelID = std::to_string(level->m_levelID.value());
    }

    // Build Mega Hack compatible JSON
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"fps\": 240.0,\n";
    ss << "  \"inputs\": [\n";

    for (size_t i = 0; i < m_actions.size(); ++i) {
        const auto& act = m_actions[i];
        ss << "    { \"frame\": " << act.tick
           << ", \"down\": " << (act.down ? "true" : "false")
           << ", \"p2\": " << (act.player2 ? "true" : "false")
           << ", \"btn\": " << act.button << " }";
        if (i + 1 < m_actions.size()) {
            ss << ",";
        }
        ss << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";

    std::string jsonStr = ss.str();

    // Determine output directory
    std::filesystem::path exportDir = geode::dirs::getGameDir() / "geode" / "macros";
    std::error_code ec;
    std::filesystem::create_directories(exportDir, ec);

    std::filesystem::path exportPath = exportDir / ("macro_level_" + levelID + ".json");
    std::ofstream outFile(exportPath);
    if (outFile.is_open()) {
        outFile << jsonStr;
        outFile.close();
        geode::Notification::create("Exported to " + exportPath.filename().string(), geode::NotificationIcon::Success)->show();
    } else {
        geode::Notification::create("Failed to write macro file!", geode::NotificationIcon::Error)->show();
    }

    return jsonStr;
}
