#include "HazardDetector.hpp"
#include <cmath>
#include <algorithm>

namespace solver {

bool HazardDetector::isHazardObject(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
        return true;
    }
    if (obj->m_slopeIsHazard) {
        return true;
    }
    return false;
}

bool HazardDetector::isSolidObject(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    if (type == GameObjectType::Solid || type == GameObjectType::Slope) {
        return true;
    }
    return false;
}

bool HazardDetector::isInteractableOrbOrPad(GameObject* obj) {
    if (!obj) return false;
    return isOrb(obj) || isPad(obj);
}

bool HazardDetector::isOrb(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

bool HazardDetector::isPad(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    switch (type) {
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::SpiderPad:
            return true;
        default:
            return false;
    }
}

bool HazardDetector::isPortal(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    switch (type) {
        case GameObjectType::CubePortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::GravityTogglePortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::TeleportPortal:
            return true;
        default:
            return false;
    }
}

bool HazardDetector::isGamemodePortal(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    switch (type) {
        case GameObjectType::CubePortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
            return true;
        default:
            return false;
    }
}

bool HazardDetector::isGravityPortal(GameObject* obj) {
    if (!obj) return false;
    auto type = obj->m_objectType;
    return (type == GameObjectType::NormalGravityPortal ||
            type == GameObjectType::InverseGravityPortal ||
            type == GameObjectType::GravityTogglePortal);
}

void HazardDetector::buildIndex(cocos2d::CCArray* objects) {
    clearIndex();
    if (!objects) return;

    for (unsigned int i = 0; i < objects->count(); ++i) {
        auto obj = geode::cast::typeinfo_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        float x = obj->getPositionX();
        int bucket = static_cast<int>(std::floor(x / BUCKET_WIDTH));

        if (isHazardObject(obj)) {
            s_hazardBuckets[bucket].push_back(obj);
        } else if (isSolidObject(obj)) {
            s_solidBuckets[bucket].push_back(obj);
        }

        if (isOrb(obj)) {
            s_orbBuckets[bucket].push_back(obj);
        }

        if (isPortal(obj)) {
            s_portalBuckets[bucket].push_back(obj);
        }
    }
    s_hasIndex = true;
}

void HazardDetector::clearIndex() {
    s_hazardBuckets.clear();
    s_solidBuckets.clear();
    s_orbBuckets.clear();
    s_portalBuckets.clear();
    s_hasIndex = false;
}

float HazardDetector::calculateClearance(
    const cocos2d::CCPoint& playerPos,
    bool isUpsideDown,
    cocos2d::CCArray* objects,
    size_t& nearbyObstacleCountOut
) {
    nearbyObstacleCountOut = 0;
    float minHazardDistSq = MAX_CLEARANCE * MAX_CLEARANCE;
    float minSolidDistSq = MAX_CLEARANCE * MAX_CLEARANCE;

    auto checkList = [&](const std::vector<GameObject*>& list, bool isHazard) {
        for (GameObject* obj : list) {
            if (!obj) continue;
            cocos2d::CCPoint objPos = obj->getPosition();
            float dxCenter = objPos.x - playerPos.x;
            float dyCenter = objPos.y - playerPos.y;

            if (std::abs(dxCenter) <= EVALUATION_RADIUS_X && std::abs(dyCenter) <= EVALUATION_RADIUS_Y) {
                if (isHazard) {
                    if (dxCenter >= -25.0f) {
                        nearbyObstacleCountOut++;
                    }
                    float dx = std::max(0.0f, std::abs(dxCenter) - 15.0f);
                    float dy = std::max(0.0f, std::abs(dyCenter) - 15.0f);
                    float distSq = (dx * dx + dy * dy) * 0.85f;
                    if (distSq < minHazardDistSq) {
                        minHazardDistSq = distSq;
                    }
                } else {
                    // Solid blocks
                    // Exclude the supporting surface directly underneath player (or above if upside-down)
                    if (!isUpsideDown && dyCenter <= -8.0f && std::abs(dxCenter) <= 18.0f) {
                        continue; // Standing on supporting floor
                    }
                    if (isUpsideDown && dyCenter >= 8.0f && std::abs(dxCenter) <= 18.0f) {
                        continue; // Running on supporting ceiling
                    }

                    // Count solid walls ahead or crush ceilings
                    if (dxCenter >= -10.0f) {
                        nearbyObstacleCountOut++;
                    }
                    float dx = std::max(0.0f, std::abs(dxCenter) - 15.0f);
                    float dy = std::max(0.0f, std::abs(dyCenter) - 15.0f);
                    float distSq = dx * dx + dy * dy;
                    if (distSq < minSolidDistSq) {
                        minSolidDistSq = distSq;
                    }
                }
            }
        }
    };

    if (s_hasIndex) {
        int minBucket = static_cast<int>(std::floor((playerPos.x - EVALUATION_RADIUS_X) / BUCKET_WIDTH));
        int maxBucket = static_cast<int>(std::floor((playerPos.x + EVALUATION_RADIUS_X) / BUCKET_WIDTH));

        for (int b = minBucket; b <= maxBucket; ++b) {
            auto itH = s_hazardBuckets.find(b);
            if (itH != s_hazardBuckets.end()) {
                checkList(itH->second, true);
            }
            auto itS = s_solidBuckets.find(b);
            if (itS != s_solidBuckets.end()) {
                checkList(itS->second, false);
            }
        }
        float minOverallSq = std::min(minHazardDistSq, minSolidDistSq);
        return std::sqrt(minOverallSq);
    }

    if (!objects) return MAX_CLEARANCE;

    for (unsigned int i = 0; i < objects->count(); ++i) {
        auto obj = geode::cast::typeinfo_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        bool isHaz = isHazardObject(obj);
        bool isSol = isSolidObject(obj);
        if (isHaz || isSol) {
            std::vector<GameObject*> singleList = { obj };
            checkList(singleList, isHaz);
        }
    }

    float minOverallSq = std::min(minHazardDistSq, minSolidDistSq);
    return std::sqrt(minOverallSq);
}

std::vector<GameObject*> HazardDetector::getOrbsInWindow(
    float minX,
    float maxX,
    cocos2d::CCArray* objects
) {
    std::vector<GameObject*> result;
    if (s_hasIndex) {
        int minBucket = static_cast<int>(std::floor(minX / BUCKET_WIDTH));
        int maxBucket = static_cast<int>(std::floor(maxX / BUCKET_WIDTH));

        for (int b = minBucket; b <= maxBucket; ++b) {
            auto it = s_orbBuckets.find(b);
            if (it == s_orbBuckets.end()) continue;

            for (GameObject* obj : it->second) {
                if (!obj) continue;
                float ox = obj->getPositionX();
                if (ox >= minX && ox <= maxX) {
                    result.push_back(obj);
                }
            }
        }
        return result;
    }

    if (!objects) return result;
    for (unsigned int i = 0; i < objects->count(); ++i) {
        auto obj = geode::cast::typeinfo_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;
        if (isOrb(obj)) {
            float ox = obj->getPositionX();
            if (ox >= minX && ox <= maxX) {
                result.push_back(obj);
            }
        }
    }
    return result;
}

std::vector<GameObject*> HazardDetector::getInteractablesInWindow(
    float minX,
    float maxX,
    cocos2d::CCArray* objects
) {
    return getOrbsInWindow(minX, maxX, objects);
}

std::vector<GameObject*> HazardDetector::getPortalsInWindow(
    float minX,
    float maxX,
    cocos2d::CCArray* objects
) {
    std::vector<GameObject*> result;
    if (s_hasIndex) {
        int minBucket = static_cast<int>(std::floor(minX / BUCKET_WIDTH));
        int maxBucket = static_cast<int>(std::floor(maxX / BUCKET_WIDTH));

        for (int b = minBucket; b <= maxBucket; ++b) {
            auto it = s_portalBuckets.find(b);
            if (it == s_portalBuckets.end()) continue;

            for (GameObject* obj : it->second) {
                if (!obj) continue;
                float ox = obj->getPositionX();
                if (ox >= minX && ox <= maxX) {
                    result.push_back(obj);
                }
            }
        }
        return result;
    }

    if (!objects) return result;
    for (unsigned int i = 0; i < objects->count(); ++i) {
        auto obj = geode::cast::typeinfo_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;
        if (isPortal(obj)) {
            float ox = obj->getPositionX();
            if (ox >= minX && ox <= maxX) {
                result.push_back(obj);
            }
        }
    }
    return result;
}

} // namespace solver

