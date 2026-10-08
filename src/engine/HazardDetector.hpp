#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <unordered_map>
#include <vector>

namespace solver {

class HazardDetector {
public:
    static constexpr float EVALUATION_RADIUS_X = 100.0f;
    static constexpr float EVALUATION_RADIUS_Y = 150.0f;
    static constexpr float MAX_CLEARANCE = 100.0f;
    static constexpr float BUCKET_WIDTH = 200.0f;

    static bool isHazardObject(GameObject* obj);
    static bool isSolidObject(GameObject* obj);
    static bool isInteractableOrbOrPad(GameObject* obj);
    static bool isOrb(GameObject* obj);
    static bool isPad(GameObject* obj);
    static bool isPortal(GameObject* obj);
    static bool isGamemodePortal(GameObject* obj);
    static bool isGravityPortal(GameObject* obj);

    static void buildIndex(cocos2d::CCArray* objects);
    static void clearIndex();

    static float calculateClearance(
        const cocos2d::CCPoint& playerPos,
        bool isUpsideDown,
        cocos2d::CCArray* objects,
        size_t& nearbyObstacleCountOut
    );

    static std::vector<GameObject*> getOrbsInWindow(
        float minX,
        float maxX,
        cocos2d::CCArray* objects
    );

    static std::vector<GameObject*> getInteractablesInWindow(
        float minX,
        float maxX,
        cocos2d::CCArray* objects
    );

    static std::vector<GameObject*> getPortalsInWindow(
        float minX,
        float maxX,
        cocos2d::CCArray* objects
    );

private:
    static inline std::unordered_map<int, std::vector<GameObject*>> s_hazardBuckets;
    static inline std::unordered_map<int, std::vector<GameObject*>> s_solidBuckets;
    static inline std::unordered_map<int, std::vector<GameObject*>> s_orbBuckets;
    static inline std::unordered_map<int, std::vector<GameObject*>> s_portalBuckets;
    static inline bool s_hasIndex = false;
};

} // namespace solver
