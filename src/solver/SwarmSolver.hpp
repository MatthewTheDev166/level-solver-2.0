#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"
#include <vector>
#include <string>
#include <random>
#include <chrono>

class SwarmSolver {
public:
    static SwarmSolver& get() {
        static SwarmSolver instance;
        return instance;
    }

    void start(GJGameLevel* level);
    void pause();
    void resume();
    void stop();
    void reset();

    void stepSwarmBatch(uint32_t msBudget = 12);

    bool isSolving() const { return m_isSolving; }
    bool isCompleted() const { return m_isCompleted; }
    bool isHeadlessSimulating() const { return m_headlessSimulating; }

    const SwarmTelemetry& getTelemetry() const { return m_telemetry; }
    const std::vector<Action>& getResolvedMacro() const { return m_verifiedPrefix; }
    GJGameLevel* getActiveLevel() const { return m_activeLevel; }

    int getPopulationSize() const { return m_populationSize; }
    void setPopulationSize(int size) { m_populationSize = std::clamp(size, 20, 400); }

    int getHorizonTicks() const { return m_horizonTicks; }
    void setHorizonTicks(int horizon) { m_horizonTicks = std::clamp(horizon, 15, 180); }

    int getTimeBudgetMs() const { return m_timeBudgetMs; }
    void setTimeBudgetMs(int ms) { m_timeBudgetMs = std::clamp(ms, 2, 30); }

private:
    SwarmSolver() = default;
    ~SwarmSolver();

    void cleanupHeadless();
    void calculateTrueLevelLength();
    void captureInitialAnchor();
    std::vector<BotCandidate> generateSwarm(uint32_t startTick, uint32_t horizon, bool startsHeld);
    void simulateCandidate(BotCandidate& bot, uint32_t startTick, uint32_t horizon);

    GJGameLevel* m_activeLevel = nullptr;
    cocos2d::CCScene* m_headlessScene = nullptr;
    PlayLayer* m_headlessPlayLayer = nullptr;

    bool m_isSolving = false;
    bool m_isCompleted = false;
    bool m_headlessSimulating = false;

    int m_populationSize = 160;
    int m_horizonTicks = 60;
    int m_timeBudgetMs = 12;

    uint32_t m_frontierTick = 0;
    uint32_t m_rewindCount = 0;
    uint32_t m_activeWave = 0;
    uint32_t m_waveAttempt = 0;
    float m_temperature = 1.0f;

    float m_startX = 0.0f;
    float m_currentX = 0.0f;
    float m_trueLevelLength = 1000.0f;

    std::vector<Action> m_verifiedPrefix;
    std::vector<AnchorPoint> m_anchors;

    // Active wave batch evaluation state
    std::vector<BotCandidate> m_activeCandidates;
    size_t m_currentCandidateIdx = 0;
    std::vector<BotCandidate> m_currentWaveSurvivors;
    AnchorPoint m_waveAnchor;

    SwarmTelemetry m_telemetry;
    std::mt19937 m_rng{1337};

    std::chrono::high_resolution_clock::time_point m_speedTimer;
    uint32_t m_ticksSampleCount = 0;
};
