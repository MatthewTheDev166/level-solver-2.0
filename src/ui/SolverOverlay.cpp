#include "SolverOverlay.hpp"
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../menu/MenuManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlatformToolbox.hpp>
#include <imgui.h>
#include <imgui-cocos.hpp>

using namespace geode::prelude;

static bool s_showOverlay = false;

void SolverOverlay::setup() {
    ImGuiCocos::get().setup([] {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }).draw([] {
        SolverOverlay::render();
    });
}

void SolverOverlay::toggleVisibility() {
    s_showOverlay = !s_showOverlay;
    ImGuiCocos::get().setVisible(s_showOverlay);
    if (s_showOverlay) {
        PlatformToolbox::showCursor();
        if (auto view = cocos2d::CCEGLView::sharedOpenGLView()) {
            view->showCursor(true);
        }
    }
}

bool SolverOverlay::isVisible() {
    return s_showOverlay;
}

void SolverOverlay::setVisible(bool visible) {
    s_showOverlay = visible;
    ImGuiCocos::get().setVisible(visible);
    if (visible) {
        PlatformToolbox::showCursor();
        if (auto view = cocos2d::CCEGLView::sharedOpenGLView()) {
            view->showCursor(true);
        }
    }
}

void SolverOverlay::render() {
    if (!s_showOverlay) return;

    // Guard: Only render when inside one of the 3 allowed menus
    if (!MenuManager::get().isInAllowedMenu()) {
        s_showOverlay = false;
        ImGuiCocos::get().setVisible(false);
        return;
    }

    // Ensure mouse cursor is visible and interactive
    PlatformToolbox::showCursor();
    if (auto view = cocos2d::CCEGLView::sharedOpenGLView()) {
        view->showCursor(true);
    }

    // Advance headless swarm simulation batch
    if (SwarmSolver::get().isSolving()) {
        SwarmSolver::get().stepSwarmBatch(SwarmSolver::get().getTimeBudgetMs());
    }

    auto levelInfo = MenuManager::get().getActiveLevelInfo();
    const auto& tel = SwarmSolver::get().getTelemetry();

    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level Solver 2.0 - Grounded Swarm Engine###LevelSolver2", &s_showOverlay, ImGuiWindowFlags_NoCollapse)) {
        // 1. Level Information Header Card
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "%s", levelInfo.levelName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("by %s (ID: %d | Stars: %d)", levelInfo.creatorName.c_str(), levelInfo.levelID, levelInfo.stars);
        ImGui::Separator();

        // 2. Status Badge
        ImVec4 statusColor;
        const char* statusText = "UNKNOWN";
        switch (tel.status) {
            case SolverStatus::Idle:
                statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                statusText = "IDLE";
                break;
            case SolverStatus::Searching:
                statusColor = ImVec4(0.0f, 0.9f, 0.9f, 1.0f);
                statusText = "SEARCHING (SWARM ACTIVE)";
                break;
            case SolverStatus::Paused:
                statusColor = ImVec4(1.0f, 0.8f, 0.1f, 1.0f);
                statusText = "PAUSED";
                break;
            case SolverStatus::Solved:
                statusColor = ImVec4(0.1f, 1.0f, 0.4f, 1.0f);
                statusText = "SOLVED (100% VERIFIED)";
                break;
            case SolverStatus::Failed:
                statusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                statusText = "FAILED / BLOCKED";
                break;
        }

        ImGui::Text("Engine Status: ");
        ImGui::SameLine();
        ImGui::TextColored(statusColor, "[%s]", statusText);
        if (!tel.detailMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", tel.detailMessage.c_str());
        }

        ImGui::Spacing();
        // 3. Dual Real-time Telemetry Bars
        // Level Completion Progress
        std::string progStr = fmt::format("Level Progress: {:.1f}% (X: {:.0f} / {:.0f})",
            tel.progressPercent * 100.0f, tel.currentX, tel.targetEndX);
        ImGui::ProgressBar(tel.progressPercent, ImVec2(-1, 24), progStr.c_str());

        // Alive Bots Ratio
        float aliveRatio = tel.totalBots > 0 ? static_cast<float>(tel.aliveBots) / static_cast<float>(tel.totalBots) : 0.0f;
        ImVec4 botColor = aliveRatio > 0.5f ? ImVec4(0.1f, 0.9f, 0.2f, 1.0f) : (aliveRatio > 0.2f ? ImVec4(1.0f, 0.8f, 0.1f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        std::string botStr = fmt::format("Alive Swarm Bots: {} / {} ({:.1f}%)", tel.aliveBots, tel.totalBots, aliveRatio * 100.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, botColor);
        ImGui::ProgressBar(aliveRatio, ImVec2(-1, 20), botStr.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // 4. Detailed Metrics 2-Column Table
        if (ImGui::BeginTable("MetricsGrid", 2, ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextColumn();
            ImGui::Text("Frontier Tick: %u (%.2fs)", tel.frontierTick, tel.frontierTick / 240.0f);
            ImGui::Text("Wave: #%u (Attempt: %u)", tel.activeWave, tel.waveAttempt);
            ImGui::Text("Simulation Speed: %.0f ticks/sec", tel.ticksPerSecond);

            ImGui::TableNextColumn();
            ImGui::Text("Grounded Anchors: %u checkpoints", tel.groundedAnchors);
            ImGui::Text("Timeline Rewinds: %u times", tel.rewindCount);
            ImGui::Text("Mutation Temperature: %.2fx", tel.temperature);

            ImGui::EndTable();
        }

        ImGui::Separator();

        // 5. Action Control Bar (Big prominent buttons)
        ImGui::Spacing();
        if (tel.status == SolverStatus::Searching) {
            if (ImGui::Button("Pause Solver", ImVec2(140, 36))) {
                SwarmSolver::get().pause();
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop & Reset", ImVec2(140, 36))) {
                SwarmSolver::get().reset();
            }
        } else if (tel.status == SolverStatus::Paused) {
            if (ImGui::Button("Resume Solver", ImVec2(140, 36))) {
                SwarmSolver::get().resume();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Solver", ImVec2(140, 36))) {
                SwarmSolver::get().reset();
            }
        } else {
            if (ImGui::Button("Start Solver", ImVec2(160, 36))) {
                SwarmSolver::get().start(levelInfo.level);
            }
        }

        ImGui::SameLine();

        bool hasMacro = MacroManager::get().hasMacro() || (tel.status == SolverStatus::Solved);
        if (!hasMacro) ImGui::BeginDisabled();
        if (ImGui::Button("Replay in GD", ImVec2(130, 36))) {
            s_showOverlay = false;
            ImGuiCocos::get().setVisible(false);
            MacroManager::get().startReplay(levelInfo.level);
        }
        if (!hasMacro) ImGui::EndDisabled();

        ImGui::SameLine();
        if (!hasMacro) ImGui::BeginDisabled();
        if (ImGui::Button("Export to Mega Hack", ImVec2(150, 36))) {
            MacroManager::get().exportActiveMacro(levelInfo.level);
        }
        if (!hasMacro) ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();

        // 6. Detailed Settings & Mod Info Tabs
        if (ImGui::BeginTabBar("SolverTabs")) {
            if (ImGui::BeginTabItem("Swarm Settings")) {
                static int popSize = 160;
                if (ImGui::SliderInt("Population Size", &popSize, 40, 320, "%d bots")) {
                    SwarmSolver::get().setPopulationSize(popSize);
                }

                static int horizon = 60;
                if (ImGui::SliderInt("Horizon Window", &horizon, 20, 120, "%d ticks (0.25s)")) {
                    SwarmSolver::get().setHorizonTicks(horizon);
                }

                static int budget = 12;
                if (ImGui::SliderInt("Frame Time Budget", &budget, 4, 24, "%d ms")) {
                    SwarmSolver::get().setTimeBudgetMs(budget);
                }

                ImGui::BulletText("Grounded Anchor Rule: Checkpoints are only created on safe ground.");
                ImGui::BulletText("Chronological Time-Rollback: Rewinds before bad jump on wave death.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("About Mod")) {
                ImGui::BulletText("Mod: Level Solver 2.0");
                ImGui::BulletText("Version: v2.0.0");
                ImGui::BulletText("Author: Matthew");
                ImGui::BulletText("Engine: Grounded Time-Rollback Swarm at 240 TPS");
                ImGui::BulletText("Toggle Hotkey: Right Shift (or F8)");
                ImGui::BulletText("Permitted Menus: EditLevelLayer, LevelInfoLayer, LevelSelectLayer");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

