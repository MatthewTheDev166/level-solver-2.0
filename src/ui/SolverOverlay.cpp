#include "SolverOverlay.hpp"
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include <imgui.h>
#include <imgui-cocos.hpp>

static bool s_showOverlay = true;

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
}

bool SolverOverlay::isVisible() {
    return s_showOverlay;
}

void SolverOverlay::setVisible(bool visible) {
    s_showOverlay = visible;
}

void SolverOverlay::render() {
    if (!s_showOverlay) return;
    if (!SwarmSolver::get().isInLevel()) return;

    ImGui::SetNextWindowSize(ImVec2(360, 340), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Level Solver 2.0", &s_showOverlay, ImGuiWindowFlags_NoCollapse)) {
        // 1. Live Bot Telemetry
        int alive = SwarmSolver::get().getAliveBots();
        int total = SwarmSolver::get().getPopulationSize();
        ImGui::TextColored(alive > 0 ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Alive Bots: %d / %d", alive, total);

        // 2. Real-Time Progress Bar
        float progress = SwarmSolver::get().getProgressPercent();
        float currentX = SwarmSolver::get().getCurrentX();
        float targetX = SwarmSolver::get().getLevelLength();
        std::string progressText = fmt::format("{:.1f}% (X: {:.0f} / {:.0f})", progress * 100.0f, currentX, targetX);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), progressText.c_str());

        // 3. Status Information
        ImGui::Text("Frontier Tick: %u (Rewinds: %u)", 
            SwarmSolver::get().getFrontierTick(), 
            SwarmSolver::get().getRewindCount());

        ImGui::Separator();

        // 4. Primary Controls
        if (SwarmSolver::get().isSolving()) {
            if (ImGui::Button("Pause Solver", ImVec2(-1, 32))) {
                SwarmSolver::get().pause();
            }
        } else {
            if (ImGui::Button("Start Solver", ImVec2(-1, 32))) {
                SwarmSolver::get().start();
            }
        }

        if (ImGui::Button("Replay Solution in GD", ImVec2(-1, 30))) {
            SwarmSolver::get().replay();
        }

        if (ImGui::Button("Export to Mega Hack", ImVec2(-1, 30))) {
            MacroManager::get().exportActiveMacro();
        }

        ImGui::Separator();

        // 5. Settings Collapsible
        if (ImGui::CollapsingHeader("Solver Settings")) {
            static int popSize = 160;
            if (ImGui::SliderInt("Population", &popSize, 40, 320)) {
                SwarmSolver::get().setPopulationSize(popSize);
            }

            static int horizon = 60;
            if (ImGui::SliderInt("Horizon Ticks", &horizon, 20, 120)) {
                SwarmSolver::get().setHorizonTicks(horizon);
            }
        }
    }
    ImGui::End();
}
