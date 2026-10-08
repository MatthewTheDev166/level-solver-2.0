#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <cstdint>
#include <string>

enum class SolverStatus : uint8_t {
    Idle = 0,
    Searching = 1,
    Paused = 2,
    Solved = 3,
    Failed = 4
};

enum class MenuContext : uint8_t {
    None = 0,
    EditLevel = 1,
    LevelInfo = 2,
    LevelSelect = 3
};

struct MenuLevelInfo {
    GJGameLevel* level = nullptr;
    std::string levelName = "No Level Selected";
    std::string creatorName = "-";
    int levelID = 0;
    int stars = 0;
    bool isCustom = false;
    MenuContext context = MenuContext::None;
};

struct SwarmTelemetry {
    SolverStatus status = SolverStatus::Idle;
    std::string detailMessage = "Ready to solve";
    int aliveBots = 160;
    int totalBots = 160;
    float currentX = 0.0f;
    float targetEndX = 1000.0f;
    float progressPercent = 0.0f;
    uint32_t activeWave = 0;
    uint32_t waveAttempt = 0;
    uint32_t frontierTick = 0;
    uint32_t groundedAnchors = 0;
    uint32_t rewindCount = 0;
    float ticksPerSecond = 0.0f;
    float temperature = 1.0f;
    bool isVerified = false;
    std::string activeMode = "Cube";
};


struct Action {
    uint32_t tick = 0;
    bool down = false;
    int button = 1; // 1 = Jump
    bool player2 = false;
};

struct PlayerSnapshot {
    cocos2d::CCPoint position = {0, 0};
    double yVelocity = 0.0;
    double fallSpeed = 0.0;
    float rotation = 0.0f;
    bool isOnGround = false;
    bool isDead = false;
    bool isUpsideDown = false;
    bool isShip = false;
    bool isBird = false;
    bool isBall = false;
    bool isDart = false;
    bool isRobot = false;
    bool isSpider = false;
    bool isSwing = false;
    bool isDashing = false;
    float playerSpeed = 1.0f;

    void capture(PlayerObject* player) {
        if (!player) return;
        position = player->getPosition();
        yVelocity = player->m_yVelocity;
        fallSpeed = player->m_fallSpeed;
        rotation = player->getRotation();
        isOnGround = player->m_isOnGround;
        isDead = player->m_isDead;
        isUpsideDown = player->m_isUpsideDown;
        isShip = player->m_isShip;
        isBird = player->m_isBird;
        isBall = player->m_isBall;
        isDart = player->m_isDart;
        isRobot = player->m_isRobot;
        isSpider = player->m_isSpider;
        isSwing = player->m_isSwing;
        isDashing = player->m_isDashing;
        playerSpeed = player->m_playerSpeed;
    }

    void restore(PlayerObject* player) const {
        if (!player) return;
        player->setPosition(position);
        player->m_yVelocity = yVelocity;
        player->m_fallSpeed = fallSpeed;
        player->setRotation(rotation);
        player->m_isOnGround = isOnGround;
        player->m_isDead = isDead;
        player->m_isUpsideDown = isUpsideDown;
        player->m_isShip = isShip;
        player->m_isBird = isBird;
        player->m_isBall = isBall;
        player->m_isDart = isDart;
        player->m_isRobot = isRobot;
        player->m_isSpider = isSpider;
        player->m_isSwing = isSwing;
        player->m_isDashing = isDashing;
        player->m_playerSpeed = playerSpeed;
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

struct AnchorPoint {
    uint32_t tick = 0;
    float x = 0.0f;
    PlayerSnapshot p1Snapshot;
    PlayerSnapshot p2Snapshot;
    bool hasPlayer2 = false;
    size_t actionPrefixCount = 0;
};
