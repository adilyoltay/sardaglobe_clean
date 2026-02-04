#include "lod_selector.h"
#include "../math/tile_math.h"
#include "../core/constants.h"

namespace globe {

LodSelection LodSelector::Select(
    const glm::vec3& cameraPos,
    const glm::mat4& mvp,
    int viewportWidth,
    int viewportHeight,
    const TileReadyFunc& isReady,
    const Settings& settings
) {
    LodSelection result;
    
    // Extract frustum and update horizon culler
    frustum_.Extract(mvp);
    horizon_.Update(cameraPos, static_cast<float>(EARTH_RADIUS_KM));
    
    // Calculate FOV from projection matrix
    fovDegrees_ = 2.0f * std::atan(1.0f / mvp[1][1]) * 180.0f / static_cast<float>(M_PI);
    
    // Start traversal from root tiles (level 0)
    for (int x = 0; x < 1; ++x) {
        for (int y = 0; y < 1; ++y) {
            TraverseTile(
                TileKey(0, x, y),
                cameraPos,
                mvp,
                viewportHeight,
                isReady,
                settings,
                result,
                0
            );
        }
    }
    
    return result;
}

void LodSelector::TraverseTile(
    const TileKey& key,
    const glm::vec3& cameraPos,
    const glm::mat4& mvp,
    int viewportHeight,
    const TileReadyFunc& isReady,
    const Settings& settings,
    LodSelection& result,
    int depth
) {
    // Depth limit
    if (depth > 30) return;
    
    // Zoom limits
    if (key.level < settings.minZoom) {
        // Go to children directly
        auto children = key.Children();
        for (const auto& child : children) {
            TraverseTile(child, cameraPos, mvp, viewportHeight, isReady, settings, result, depth + 1);
        }
        return;
    }
    
    // Calculate tile geometry
    glm::vec3 center = TileCenterWorld(key);
    float radius = TileBoundingRadius(key);
    
    // Frustum culling
    if (!frustum_.IsSphereVisible(center, radius)) {
        return;
    }
    
    // Horizon culling
    if (!horizon_.IsSphereVisible(center, radius)) {
        return;
    }
    
    // Add to required set
    result.required.insert(key);
    
    // Check if at max zoom
    if (key.level >= settings.maxZoom) {
        result.leaves.push_back(key);
        return;
    }
    
    // Check if should subdivide (SSE test)
    bool subdivide = ShouldSubdivide(
        key, cameraPos, viewportHeight,
        fovDegrees_, settings.sseThreshold, settings.tiltFactor
    );
    
    if (!subdivide) {
        // This is a leaf
        result.leaves.push_back(key);
        return;
    }
    
    // Should subdivide - check if children are ready
    bool childrenReady = AreChildrenReady(key, isReady);
    
    if (childrenReady) {
        // Traverse children
        auto children = key.Children();
        for (const auto& child : children) {
            TraverseTile(child, cameraPos, mvp, viewportHeight, isReady, settings, result, depth + 1);
        }
        result.refinedCount++;
    } else {
        // Use this tile as fallback, but still request children
        result.leaves.push_back(key);
        
        // Add children to required (for loading)
        auto children = key.Children();
        for (const auto& child : children) {
            result.required.insert(child);
        }
    }
}

bool LodSelector::ShouldSubdivide(
    const TileKey& key,
    const glm::vec3& cameraPos,
    int viewportHeight,
    float fovDegrees,
    float sseThreshold,
    float tiltFactor
) {
    glm::vec3 center = TileCenterWorld(key);
    float radius = TileBoundingRadius(key);
    
    // Distance to closest point on bounding sphere
    float distance = glm::length(center - cameraPos);
    distance = std::max(1.0f, distance - radius);
    
    // Convert to meters
    double distanceMeters = static_cast<double>(distance) / M_TO_WORLD;
    
    // Compute SSE
    float sse = ComputeSSE(key.level, distanceMeters, viewportHeight, fovDegrees);
    
    // Apply tilt factor (reduce detail when tilted)
    float adjustedThreshold = sseThreshold / std::max(0.1f, tiltFactor);
    
    return sse > adjustedThreshold;
}

bool LodSelector::AreChildrenReady(const TileKey& key, const TileReadyFunc& isReady) {
    auto children = key.Children();
    for (const auto& child : children) {
        if (!isReady(child)) {
            return false;
        }
    }
    return true;
}

} // namespace globe
