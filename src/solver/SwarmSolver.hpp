#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"
#include <vector>
#include <random>

class SwarmSolver {
public:
    static SwarmSolver& get() {
        static SwarmSolver instance;
        return instance;
    }

    void onLevelEntered(PlayLayer* playLayer);
    void onLevelExited();

    void start();
    void pause();
    void replay();
    void update(PlayLayer* playLayer);

    bool isInLevel() const { return m_inLevel && m_playLayer != nullptr; }
    bool isSolving() const { return m_isSolving; }
    bool isHeadlessSimulating() const { return m_headlessSimulating; }

    int getAliveBots() const { return m_aliveBots; }
    int getPopulationSize() const { return m_populationSize; }
    void setPopulationSize(int size) { m_populationSize = std::clamp(size, 20, 500); }

    int getHorizonTicks() const { return m_horizonTicks; }
    void setHorizonTicks(int horizon) { m_horizonTicks = std::clamp(horizon, 15, 240); }

    uint32_t getFrontierTick() const { return m_frontierTick; }
    uint32_t getRewindCount() const { return m_rewindCount; }

    float getCurrentX() const { return m_currentX; }
    float getLevelLength() const { return m_trueLevelLength; }
    float getProgressPercent() const;

private:
    SwarmSolver() = default;

    void calculateTrueLevelLength();
    void captureInitialAnchor();
    void runWave();
    std::vector<BotCandidate> generateSwarm();

    PlayLayer* m_playLayer = nullptr;
    bool m_inLevel = false;
    bool m_isSolving = false;
    bool m_headlessSimulating = false;

    int m_populationSize = 160;
    int m_aliveBots = 160;
    int m_horizonTicks = 60;

    uint32_t m_frontierTick = 0;
    uint32_t m_rewindCount = 0;
    float m_temperature = 1.0f;

    float m_startX = 0.0f;
    float m_currentX = 0.0f;
    float m_trueLevelLength = 1000.0f;

    std::vector<Action> m_verifiedPrefix;
    std::vector<AnchorPoint> m_anchors;

    std::mt19937 m_rng{std::random_device{}()};
};
