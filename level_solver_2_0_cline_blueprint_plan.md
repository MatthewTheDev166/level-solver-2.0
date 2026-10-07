# Implementation Plan: Level Solver 2.0 (Dear ImGui & Grounded Swarm Engine)

**Target Repository**: `https://github.com/MatthewTheDev166/level-solver-2.0.git`  
**Target Platform**: Geometry Dash 2.2 (Geode v5.10.x, Windows x64 MSVC)  
**Primary Engine**: Dear ImGui via `gd-imgui-cocos` + Headless Physics Runner + Grounded Time-Rollback Swarm

---

## 1. Goal Description

Build **Level Solver 2.0** from scratch as a clean, unified Geode mod that completely replaces the fragile Cocos2d-x UI with **Dear ImGui**, and replaces flawed spatial backtracking with a **Grounded Time-Rollback Swarm Engine**:
1. **Dear ImGui In-Game Overlay**: A sleek, toggleable floating window (hotkey `Right Shift` or `F8`) rendering at 60 FPS directly via DirectX/OpenGL with:
   - Dynamic real-time bot counter: `Alive Bots: 160 / 160` (drops on death, resets on wave respawn).
   - Real-time progress bar with level distance: `X: 4,210 / 10,000 | 42.1%`.
   - Action controls: `[Start Solver]`, `[Pause]`, `[Replay in GD]`, `[Export to Mega Hack]`.
   - Mode & Swarm settings drawer (population, mutation variance, horizon steps).
2. **Grounded Time-Rollback Backtracking**:
   - **Grounded Anchor Rule**: Checkpoints are **ONLY** created when `m_isOnGround == true` on safe surfaces. Zero checkpoints in midair.
   - **Chronological Time Rewind**: When a wave fails, rewind along the input timeline to *before* the bad takeoff jump was pressed ($T \leftarrow \max(0, T - 35)$ ticks), instead of teleporting through $(X, Y)$ blocks.
   - **Survival-Only Advancement**: A checkpoint is only advanced if at least one bot survives the full wave. Never place a checkpoint where a bot died.
3. **Headless Verification Guarantee**:
   - Verification simulates all the way to the true physical end of the level (`m_hasCompletedLevel == true`). Never prematurely truncate a macro.
4. **Fast Windows-Only CI**:
   - Strip Android and macOS jobs from GitHub Actions workflow to cut build times down to ~90–120 seconds.

---

## 2. User Review Required & Critical Rules for Cline

> [!IMPORTANT]
> **Windows-Only CI Build Matrix**:
> In `.github/workflows/build.yml`, **disable or remove Android, macOS, and iOS builds**. Keep only `windows-2022` x64 MSVC. This speeds up CI compiles drastically and saves action runner minutes.

> [!IMPORTANT]
> **Zero Cocos2d-x UI in Headless Physics**:
> Never call `playLayer->updateVisibility()`, `removeAllComponents()`, or touch Cocos2d node trees during headless simulation loops. All UI is handled strictly in Dear ImGui rendering callbacks.

> [!WARNING]
> **True Level Length in Editor Levels**:
> In custom/editor levels, `playLayer->getEndPosition().x` defaults to `1065.0f`. Always compute `trueLevelLength` by iterating over `playLayer->m_objects` to find `max(obj->getPositionX() + 90.0f)` across all solids, hazards, and triggers.

---

## 3. Project Structure

```
level-solver-2.0/
├── .github/
│   └── workflows/
│       └── build.yml               # Windows-only fast CI workflow
├── CMakeLists.txt                  # Geode CMake with gd-imgui-cocos via CPM
├── mod.json                        # Mod metadata (ID: matthew.level-solver-2)
└── src/
    ├── main.cpp                    # Mod loaded callback & ImGui setup
    ├── core/
    │   └── Types.hpp               # Compact types: Action, Bot, Snapshot, WaveResult
    ├── ui/
    │   ├── SolverOverlay.hpp       # Dear ImGui overlay header
    │   └── SolverOverlay.cpp       # Dear ImGui rendering & interactive controls
    ├── solver/
    │   ├── SwarmSolver.hpp         # Grounded Time-Rollback Swarm solver header
    │   └── SwarmSolver.cpp         # Population generator, simulation loop, verification
    ├── engine/
    │   ├── ActiveLayerScope.hpp    # Scoped RAII PlayLayer activator
    │   └── HeadlessEngine.hpp      # Fast headless physics stepper
    ├── hooks/
    │   └── PlayLayerHook.cpp       # Level entry/exit detection, replay injection, input blocking
    └── replay/
        ├── MacroManager.hpp        # In-memory macro storage & playback controller
        └── MacroManager.cpp        # GDR / GDR2 / Mega Hack JSON exporter
```

---

## 4. Proposed Changes & Implementation Details

### Component 1: Build System & Fast Windows-Only CI

#### [NEW] `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(level-solver-2 VERSION 2.0.0)

# CPM Package Manager setup
include(cmake/CPM.cmake OPTIONAL)
if (NOT COMMAND CPMAddPackage)
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.7/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    )
    include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)
endif()

# Include gd-imgui-cocos
CPMAddPackage("gh:matcool/gd-imgui-cocos#v1.4.3")

# Find Geode SDK
find_package(Geode REQUIRED)

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.hpp")

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE imgui-cocos)

setup_geode_mod(${PROJECT_NAME})
```

#### [NEW] `.github/workflows/build.yml`
```yaml
name: Build Geode Mod (Windows Fast)

on:
  push:
    branches: ["main"]
  workflow_dispatch:

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        config:
          - name: Windows
            os: windows-2022
            extra-flags: ""

    name: ${{ matrix.config.name }}
    runs-on: ${{ matrix.config.os }}

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup Geode CLI
        uses: geode-sdk/setup-geode@v3

      - name: Build Mod
        uses: geode-sdk/build-geode-mod@v3
        with:
          bindings: "geode-sdk/bindings"
          combine: true
          target: ${{ matrix.config.name }}
          extra-flags: ${{ matrix.config.extra-flags }}

      - name: Upload Artifact
        uses: actions/upload-artifact@v4
        with:
          name: LevelSolver2-Windows
          path: build/*.geode
```

#### [NEW] `mod.json`
```json
{
  "geode": "4.0.0",
  "version": "v2.0.0",
  "id": "matthew.level-solver-2",
  "name": "Level Solver 2.0",
  "developer": "Matthew",
  "description": "Autonomous Level Solver with Dear ImGui overlay and Grounded Swarm Engine.",
  "dependencies": []
}
```

---

### Component 2: Core Data Structures (`src/core/Types.hpp`)

#### [NEW] `src/core/Types.hpp`
```cpp
#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>

struct Action {
    uint32_t tick = 0;
    bool down = false;
    int button = 1; // 1 = Jump
    bool player2 = false;
};

struct PlayerSnapshot {
    cocos2d::CCPoint position = {0, 0};
    double yAccel = 0.0;
    double xAccel = 0.0;
    bool isOnGround = false;
    bool isDead = false;
    float rotation = 0.0f;
    bool isUpsideDown = false;
    bool isShip = false;
    bool isBird = false;
    bool isDart = false;
    bool isRobot = false;
    bool isSpider = false;
    bool isSwing = false;
    bool isDashing = false;

    void capture(PlayerObject* player) {
        if (!player) return;
        position = player->getPosition();
        yAccel = player->m_yAccel;
        xAccel = player->m_xAccel;
        isOnGround = player->m_isOnGround;
        isDead = player->m_isDead;
        rotation = player->getRotation();
        isUpsideDown = player->m_isUpsideDown;
        isShip = player->m_isShip;
        isBird = player->m_isBird;
        isDart = player->m_isDart;
        isRobot = player->m_isRobot;
        isSpider = player->m_isSpider;
        isSwing = player->m_isSwing;
        isDashing = player->m_isDashing;
    }

    void restore(PlayerObject* player) const {
        if (!player) return;
        player->setPosition(position);
        player->m_yAccel = yAccel;
        player->m_xAccel = xAccel;
        player->m_isOnGround = isOnGround;
        player->m_isDead = isDead;
        player->setRotation(rotation);
        player->m_isUpsideDown = isUpsideDown;
        player->m_isShip = isShip;
        player->m_isBird = isBird;
        player->m_isDart = isDart;
        player->m_isRobot = isRobot;
        player->m_isSpider = isSpider;
        player->m_isSwing = isSwing;
        player->m_isDashing = isDashing;
    }
};

struct BotCandidate {
    uint32_t id = 0;
    std::vector<Action> actions;
    float finalX = 0.0f;
    uint32_t deathTick = 0;
    bool died = false;
    bool completed = false;
    bool landedSafely = false;
    float fitnessScore = 0.0f;
};
```

---

### Component 3: Dear ImGui Overlay (`src/ui/`)

#### [NEW] `src/ui/SolverOverlay.hpp`
```cpp
#pragma once

namespace SolverOverlay {
    void setup();
    void render();
    void toggleVisibility();
    bool isVisible();
    void setVisible(bool visible);
}
```

#### [NEW] `src/ui/SolverOverlay.cpp`
```cpp
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

    ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
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
        ImGui::ProgressBar(progress, ImVec2(-1, 0), fmt::format("{:.1f}% (X: {:.0f} / {:.0f})", progress * 100.0f, currentX, targetX).c_str());

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
```

---

### Component 4: Grounded Time-Rollback Swarm (`src/solver/`)

#### Key Algorithmic Requirements:
1. **Frontier Timeline ($T$)**:
   - `m_verifiedPrefix`: List of proven inputs from tick 0 up to tick $T$.
   - Exploration window: $[T, T + H]$, where $H = 60$ ticks.
2. **Grounded Anchor Rule**:
   - A candidate is only eligible to extend the frontier $T$ if it is **ALIVE**, reached further $X$, and satisfies `m_isOnGround == true` on safe surfaces.
   - If a bot flew through the air and died on a spike, its midair coordinates are **REJECTED**.
3. **Time-Rollback on Wave Failure**:
   - If all 160 bots die in a wave:
     - Rewind frontier tick $T \leftarrow \max(0, T - 35)$ ticks.
     - Prune actions in `m_verifiedPrefix` occurring after the new $T$.
     - Increment mutation temperature $\sigma$ to try earlier or longer jumps.
4. **Fast Headless Execution**:
   - Stepped at 240 TPS via `playLayer->update(1.0f / 240.0f)`.
   - All 160 bots simulated in batch without Cocos2d-x rendering overhead.

---

### Component 5: Light-Weight Hooks (`src/hooks/PlayLayerHook.cpp`)

#### [NEW] `src/hooks/PlayLayerHook.cpp`
```cpp
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/UILayer.hpp>
#include "../solver/SwarmSolver.hpp"
#include "../replay/MacroManager.hpp"
#include "../ui/SolverOverlay.hpp"

class $modify(SolverPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        SwarmSolver::get().onLevelEntered(this);
        MacroManager::get().onLevelEntered(this);
        return true;
    }

    void onQuit() {
        SwarmSolver::get().onLevelExited();
        MacroManager::get().onLevelExited();
        PlayLayer::onQuit();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (MacroManager::get().isReplayActive()) {
            MacroManager::get().stepReplay(this);
        }
    }
};

class $modify(SolverUILayer, UILayer) {
    bool keyDown(cocos2d::enumKeyCodes key) {
        // Use Right Shift or F8 (Tab is avoided to prevent conflicting with Mega Hack)
        if (key == cocos2d::KEY_RightShift || key == cocos2d::KEY_F8) {
            SolverOverlay::toggleVisibility();
            return true;
        }
        return UILayer::keyDown(key);
    }
};
```

---

## 5. Verification Plan

### Automated CI Verification
1. Push to `main` branch of `https://github.com/MatthewTheDev166/level-solver-2.0.git`.
2. Inspect GitHub Actions run:
   - Must only execute the Windows x64 MSVC job.
   - Build duration must complete under ~2 minutes.
   - Output artifact: `LevelSolver2-Windows` containing `matthew.level-solver-2.geode`.

### In-Game Verification
1. Launch Geometry Dash and press `Right Shift` (or `F8`):
   - Verify the Dear ImGui window appears smoothly without lag (and does not conflict with Mega Hack's `Tab` key).
2. Enter `Stereo Madness` or `Jumper`:
   - Observe level length, ID, and start position reported accurately.
3. Click **Start Solver**:
   - Verify the alive bot counter starts at `160 / 160`, decrements as bots hit spikes, and resets cleanly to `160` on respawn.
   - Verify that when a spike is hit, bots rewind in time (before takeoff) and try earlier/later jumps, clearing the obstacle.
4. Verify Replay and Export:
   - Click **Replay Solution in GD**: icon plays the run from spawn to finish.
   - Export macro and load into Mega Hack ReplayBot to verify 100% course completion.
