#pragma once
#include <Geode/Geode.hpp>
#include "../core/Types.hpp"

class HeadlessEngine {
public:
    static bool stepCandidate(PlayLayer* playLayer, BotCandidate& bot, uint32_t startTick, uint32_t horizon, float trueLevelLength) {
        if (!playLayer || !playLayer->m_player1) return false;

        auto player = playLayer->m_player1;
        size_t actionIdx = 0;
        const float fixedDt = 1.0f / 240.0f;

        for (uint32_t tick = startTick; tick < startTick + horizon; ++tick) {
            // Apply actions scheduled for this tick
            while (actionIdx < bot.actions.size() && bot.actions[actionIdx].tick == tick) {
                const auto& act = bot.actions[actionIdx];
                playLayer->handleButton(act.down, act.button, !act.player2);
                actionIdx++;
            }

            // Headless physics step without Cocos2d-x UI overhead
            playLayer->update(fixedDt);

            // Check if player died
            if (player->m_isDead) {
                bot.died = true;
                bot.deathTick = tick;
                bot.finalX = player->getPositionX();
                bot.landedSafely = false;
                return false;
            }

            // Check if player completed level
            if (playLayer->m_hasCompletedLevel || player->getPositionX() >= trueLevelLength) {
                bot.completed = true;
                bot.finalX = player->getPositionX();
                bot.landedSafely = true;
                return true;
            }
        }

        bot.finalX = player->getPositionX();
        bot.landedSafely = player->m_isOnGround;
        return true;
    }
};
