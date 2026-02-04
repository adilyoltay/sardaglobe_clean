#include "tile_lod_selector.h"
#include "tile_math.h"
#include <cmath>
#include <algorithm>

namespace earth {

// Use constants from tile_math.h
using earth::GLOBE_RADIUS;
using earth::PI_VAL;
constexpr double M_PI_VAL = PI_VAL;  // Alias for backward compatibility

// ============================================================================
// FRUSTUM
// ============================================================================

Frustum Frustum::FromMVP(const glm::mat4& mvp) {
    Frustum f;
    
    // Extract frustum planes from MVP matrix
    // Left
    f.planes[0] = glm::vec4(
        mvp[0][3] + mvp[0][0],
        mvp[1][3] + mvp[1][0],
        mvp[2][3] + mvp[2][0],
        mvp[3][3] + mvp[3][0]
    );
    // Right
    f.planes[1] = glm::vec4(
        mvp[0][3] - mvp[0][0],
        mvp[1][3] - mvp[1][0],
        mvp[2][3] - mvp[2][0],
        mvp[3][3] - mvp[3][0]
    );
    // Bottom
    f.planes[2] = glm::vec4(
        mvp[0][3] + mvp[0][1],
        mvp[1][3] + mvp[1][1],
        mvp[2][3] + mvp[2][1],
        mvp[3][3] + mvp[3][1]
    );
    // Top
    f.planes[3] = glm::vec4(
        mvp[0][3] - mvp[0][1],
        mvp[1][3] - mvp[1][1],
        mvp[2][3] - mvp[2][1],
        mvp[3][3] - mvp[3][1]
    );
    // Near
    f.planes[4] = glm::vec4(
        mvp[0][3] + mvp[0][2],
        mvp[1][3] + mvp[1][2],
        mvp[2][3] + mvp[2][2],
        mvp[3][3] + mvp[3][2]
    );
    // Far
    f.planes[5] = glm::vec4(
        mvp[0][3] - mvp[0][2],
        mvp[1][3] - mvp[1][2],
        mvp[2][3] - mvp[2][2],
        mvp[3][3] - mvp[3][2]
    );
    
    // Normalize planes
    for (auto& plane : f.planes) {
        float len = glm::length(glm::vec3(plane));
        if (len > 0.0001f) {
            plane /= len;
        }
    }
    
    return f;
}

bool Frustum::ContainsSphere(const glm::vec3& center, float radius) const {
    for (const auto& plane : planes) {
        float dist = glm::dot(glm::vec3(plane), center) + plane.w;
        if (dist < -radius) {
            return false; // Completely outside
        }
    }
    return true;
}

bool Frustum::IntersectsSphere(const glm::vec3& center, float radius) const {
    for (const auto& plane : planes) {
        float dist = glm::dot(glm::vec3(plane), center) + plane.w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// TILE LOD SELECTOR
// ============================================================================

TileLodSelector::TileLodSelector() : config_() {}

TileLodSelector::TileLodSelector(const Config& config) : config_(config) {}

glm::vec3 TileLodSelector::TileCenterNormal(int z, int x, int y) {
    return earth::TileCenterNormal(z, x, y);
}

float TileLodSelector::TileAngularRadius(int z, int x, int y) {
    return earth::TileAngularRadius(z, x, y);
}

float TileLodSelector::TileBoundingRadius(int z, int x, int y) {
    return earth::TileBoundingRadius(z, x, y);
}

// Google Earth style geometric error calculation (meters)
// Returns the real-world size of one pixel at this LOD
double TileLodSelector::TileGeometricError(int z, double /*unused*/) {
    // Earth circumference at equator / (2^level * tile_size)
    constexpr double EARTH_CIRCUMFERENCE = 40075017.0;  // meters
    constexpr int TILE_SIZE = 256;
    return EARTH_CIRCUMFERENCE / (static_cast<double>(1 << z) * TILE_SIZE);
}

int TileLodSelector::CalculateLodFromAltitude(double altitudeKm, double globeRadius) {
    // JS parity: lod = 22 - log2(distance * 256 / R)
    double distance = altitudeKm + globeRadius;
    double lodExact = 22.0 - std::log2(distance * 256.0 / globeRadius);
    return static_cast<int>(std::round(std::clamp(lodExact, 2.0, 22.0)));
}

double TileLodSelector::CalculateSSE(const TileKey& key, const glm::vec3& cameraPos,
                                      double windowHeight, double fovY) const {
    glm::vec3 tileCenter = TileCenterNormal(key.level, key.x, key.y) * static_cast<float>(GLOBE_RADIUS);
    float tileRadius = TileBoundingRadius(key.level, key.x, key.y);
    
    // FIX: Use distance to closest point on bounding sphere (like legacy system)
    double distance = glm::length(tileCenter - cameraPos);
    distance = std::max(1.0, distance - static_cast<double>(tileRadius));
    
    // Convert normalized distance to meters (GLOBE_RADIUS is in km)
    constexpr double GLOBE_RADIUS_K = 0.001;  // km to m conversion
    double distanceMeters = distance / GLOBE_RADIUS_K;
    
    // Geometric error for this tile (in meters)
    double geometricError = TileGeometricError(key.level, 0.0);  // baseError unused now
    
    // Convert to screen-space error
    // SSE = (geometricError / distance) * (windowHeight / (2 * tan(fovY/2)))
    double fovRad = fovY * M_PI_VAL / 180.0;
    double sseDenom = 2.0 * std::tan(fovRad / 2.0);
    double sse = (geometricError / distanceMeters) * (windowHeight / sseDenom);
    
    // FIX: Apply tilt factor (reduces SSE at high tilt to prevent over-subdivision)
    sse *= static_cast<double>(config_.tiltFactor);
    
    return sse;
}

bool TileLodSelector::IsOverHorizon(const glm::vec3& tileCenter, const glm::vec3& cameraPos,
                                     float tileRadius) const {
    double camDist = glm::length(cameraPos);
    if (camDist <= GLOBE_RADIUS) return false; // Inside globe
    
    // Horizon distance from camera
    double horizonDist = std::sqrt(camDist * camDist - GLOBE_RADIUS * GLOBE_RADIUS);
    
    // Distance from camera to tile center
    glm::vec3 toTile = tileCenter - cameraPos;
    double tileDist = glm::length(toTile);
    
    // Check if tile is beyond horizon (with margin for tile size)
    double effectiveHorizon = horizonDist + tileRadius + config_.horizonCullMargin * GLOBE_RADIUS;
    
    // Also check dot product - tile should be facing camera
    glm::vec3 camDir = glm::normalize(cameraPos);
    glm::vec3 tileDir = glm::normalize(tileCenter);
    double dotProduct = glm::dot(camDir, tileDir);
    
    // If tile is on back side of globe and beyond horizon
    if (dotProduct < -0.1 && tileDist > effectiveHorizon) {
        return true;
    }
    
    return false;
}

void TileLodSelector::TraverseTile(
    const TileKey& key,
    const Frustum& frustum,
    const glm::vec3& cameraPos,
    double windowHeight,
    double fovY,
    SelectionResult& result,
    const std::function<bool(const std::string&)>& isTileReady,
    int depth
) {
    // Depth limit for safety
    if (depth > 25) return;
    
    std::string keyStr = key.ToString();
    glm::vec3 tileCenter = TileCenterNormal(key.level, key.x, key.y) * static_cast<float>(GLOBE_RADIUS);
    float tileRadius = TileBoundingRadius(key.level, key.x, key.y);
    
    // Frustum culling
    if (config_.aggressiveCulling && !frustum.IntersectsSphere(tileCenter, tileRadius)) {
        result.culledCount++;
        return;
    }
    
    // Horizon culling
    if (IsOverHorizon(tileCenter, cameraPos, tileRadius)) {
        result.culledCount++;
        return;
    }
    
    result.visibleCount++;
    
    // Calculate SSE (includes tilt factor)
    double sse = CalculateSSE(key, cameraPos, windowHeight, fovY);
    
    // Compare SSE ratio to threshold (JS parity style)
    double sseRatio = sse / config_.sseThreshold;
    bool shouldRefine = (sseRatio > 1.0) && (key.level < config_.maxZoom);
    
    // Force refine if we haven't reached minimum detail level
    if (key.level < config_.minZoom) {
        shouldRefine = true;
    }
    
    if (shouldRefine) {
        result.refinedCount++;
        
        // FIX: Improved fallback logic (like legacy CollectVisibleTilesRecursive)
        bool currentTileReady = !isTileReady || isTileReady(keyStr);
        bool allChildrenReady = AreChildrenReady(key, isTileReady);
        
        // Decision logic:
        // 1. If current tile NOT ready -> must try children (they might be ready from cache)
        // 2. If current tile IS ready -> only switch to children if they're ALSO ready
        bool useChildrenForRender = false;
        
        if (!currentTileReady) {
            // Current not ready, recurse to find ready descendants
            useChildrenForRender = true;
        } else if (allChildrenReady) {
            // Current ready AND all children ready -> use children for better detail
            useChildrenForRender = true;
        } else {
            // Current ready but children not ready -> use current as fallback
            result.required.insert(keyStr);
            result.leaves.push_back(keyStr);
            
            // Still request children to load for next frame
            auto childrenArr = key.GetChildren();
            for (const auto& child : childrenArr) {
                result.required.insert(child.ToString());
            }
            useChildrenForRender = false;
        }
        
        if (useChildrenForRender) {
            auto childrenArr = key.GetChildren();
            for (const auto& child : childrenArr) {
                TraverseTile(child, frustum, cameraPos, windowHeight, fovY,
                            result, isTileReady, depth + 1);
            }
        }
    } else {
        // SSE satisfied - render this tile
        result.required.insert(keyStr);
        result.leaves.push_back(keyStr);
    }
}

int TileLodSelector::CalculateEdgeFlags(const TileKey& key, const SelectionResult& result) const {
    int flags = EDGE_NONE;
    
    // Check each neighbor - if neighbor is at lower LOD (parent), we need stitching
    auto checkNeighbor = [&](int dx, int dy, int edgeFlag) {
        TileKey neighbor(key.level, key.x + dx, key.y + dy);
        std::string neighborKey = neighbor.ToString();
        
        // If neighbor is not in leaves, check if parent is
        if (result.leaves.end() == std::find(result.leaves.begin(), result.leaves.end(), neighborKey)) {
            TileKey parent = neighbor.GetParent();
            std::string parentKey = parent.ToString();
            if (std::find(result.leaves.begin(), result.leaves.end(), parentKey) != result.leaves.end()) {
                flags |= edgeFlag;
            }
        }
    };
    
    checkNeighbor(-1, 0, EDGE_LEFT);
    checkNeighbor(1, 0, EDGE_RIGHT);
    checkNeighbor(0, -1, EDGE_TOP);
    checkNeighbor(0, 1, EDGE_BOTTOM);
    
    return flags;
}

TileLodSelector::SelectionResult TileLodSelector::Select(
    const glm::vec3& cameraPos,
    const glm::mat4& viewProj,
    double windowWidth,
    double windowHeight,
    const std::function<bool(const std::string&)>& isTileReady
) {
    SelectionResult result;
    
    Frustum frustum = Frustum::FromMVP(viewProj);
    double fovY = 45.0; // Assume default FOV
    
    // Start traversal from minZoom level (not zoom 0)
    // This ensures we have enough tiles to cover the globe
    int startZoom = config_.minZoom;
    int n = 1 << startZoom;
    
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            TraverseTile(TileKey(startZoom, x, y), frustum, cameraPos, windowHeight, fovY,
                         result, isTileReady, 0);
        }
    }
    
    // Calculate edge flags for all leaves
    for (const auto& leafKey : result.leaves) {
        TileKey key = TileKey::FromString(leafKey);
        result.edgeFlags[leafKey] = CalculateEdgeFlags(key, result);
    }
    
    return result;
}

// ============================================================================
// HELPER: AreChildrenReady
// ============================================================================

bool TileLodSelector::AreChildrenReady(const TileKey& key,
                                        const std::function<bool(const std::string&)>& isTileReady) {
    if (!isTileReady) return true;  // No callback = assume ready
    
    auto children = key.GetChildren();
    for (const auto& child : children) {
        if (!isTileReady(child.ToString())) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// TILE PRIORITY SCORER
// ============================================================================

float TilePriorityScorer::Score(
    const TileKey& key,
    const glm::vec3& tileCenter,
    const glm::vec3& cameraPos,
    const glm::mat4& mvp,
    int currentZoom,
    bool isLeaf
) {
    float score = 0.0f;
    
    // Distance score (closer = higher priority)
    float distance = glm::length(tileCenter - cameraPos);
    float maxDist = 3.0f * static_cast<float>(GLOBE_RADIUS);
    score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
    
    // Screen position score (center of screen = higher priority)
    glm::vec4 clipPos = mvp * glm::vec4(tileCenter, 1.0f);
    if (clipPos.w > 0.001f) {
        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
        score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
    }
    
    // Zoom level score (closer to target zoom = higher priority)
    int levelDiff = std::abs(key.level - currentZoom);
    score += std::max(0.0f, 5.0f - static_cast<float>(levelDiff)) * 4.0f;
    
    // Leaf bonus (visible tiles get priority)
    if (isLeaf) {
        score += 10.0f;
    }
    
    return score;
}

} // namespace earth
