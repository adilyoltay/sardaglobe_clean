#include "lod_selector.h"
#include "../math/tile_math.h"
#include "../core/constants.h"

namespace globe {

LodSelection LodSelector::Select(
    const glm::vec3& cameraPos,
    const glm::mat4& mvp,
    float fovDegrees,
    float tiltDegrees,
    int viewportWidth,
    int viewportHeight,
    const TileReadyFunc& isReady,
    const Settings& settings
) {
    LodSelection result;
    
    // Extract frustum and update horizon culler
    frustum_.Extract(mvp);
    
    // CONSERVATIVE HORIZON: Add 8% margin + max terrain height (20km)
    // This prevents false-negative culling at horizon edge (tilt/orbit gaps)
    constexpr float HORIZON_MARGIN = 1.08f;  // 8% larger radius (more conservative)
    constexpr float MAX_TERRAIN_KM = 20.0f;  // Everest + extra margin
    float horizonRadius = static_cast<float>(EARTH_RADIUS_KM) * HORIZON_MARGIN + MAX_TERRAIN_KM;
    horizon_.Update(cameraPos, horizonRadius);
    
    // Use FOV directly from camera (CRITICAL FIX: don't extract from MVP)
    // MVP = proj * view, so view matrix contaminates FOV extraction
    fovDegrees_ = fovDegrees;
    if (fovDegrees_ < 1.0f) fovDegrees_ = 45.0f;  // Fallback
    
    // Store tilt for horizon culling bypass
    tiltDegrees_ = tiltDegrees;
    
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
    
    // Enforce neighbor LOD conformance (FAZ 1.2)
    if (settings.enforceNeighborDelta) {
        EnforceNeighborConformance(result, isReady, settings);
    }
    
    return result;
}

bool LodSelector::IsTileVisible(
    const TileKey& key,
    const glm::vec3& cameraPos,
    const Settings& settings
) const {
    // Calculate tile geometry
    glm::vec3 center = TileCenterWorld(key);
    float radius = TileBoundingRadius(key);
    
    // CONSERVATIVE BOUNDING: Add 15% margin for oblique/tilt views
    constexpr float CONSERVATIVE_RADIUS_MARGIN = 1.15f;
    float conservativeRadius = radius * CONSERVATIVE_RADIUS_MARGIN;
    
    // Frustum culling
    if (!settings.disableFrustumCull) {
        if (!frustum_.IsSphereVisible(center, conservativeRadius)) {
            return false;
        }
    }
    
    // Horizon culling (bypass at high tilt)
    constexpr float HORIZON_BYPASS_TILT = 30.0f;
    if (!settings.disableHorizonCull && tiltDegrees_ < HORIZON_BYPASS_TILT) {
        if (!horizon_.IsSphereVisible(center, conservativeRadius)) {
            return false;
        }
    }
    
    return true;
}

bool LodSelector::TraverseTile(
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
    if (depth > 30) return false;
    
    // Zoom limits
    if (key.level < settings.minZoom) {
        // Go to children directly
        auto children = key.Children();
        bool anyVisible = false;
        for (const auto& child : children) {
            anyVisible = TraverseTile(child, cameraPos, mvp, viewportHeight, isReady, settings, result, depth + 1) || anyVisible;
        }
        return anyVisible;
    }
    
    // Frustum/horizon culling (conservative, bypass at high tilt)
    if (!IsTileVisible(key, cameraPos, settings)) {
        return false;
    }
    
    // Add to required set
    result.required.insert(key);
    
    // Check if at max zoom
    if (key.level >= settings.maxZoom) {
        result.leaves.push_back(key);
        result.leafSet.insert(key);
        return true;
    }
    
    // Check if should subdivide (SSE test)
    bool subdivide = ShouldSubdivide(
        key, cameraPos, viewportHeight,
        fovDegrees_, settings.sseThreshold, settings.tiltFactor
    );
    
    if (!subdivide) {
        // This is a leaf
        result.leaves.push_back(key);
        result.leafSet.insert(key);
        
        // Prefetch: add neighbors for smooth panning (Google Earth style)
        auto neighbors = key.Neighbors();
        for (const auto& neighbor : neighbors) {
            // Skip invalid neighbors (at edges)
            if (!neighbor.IsValid()) continue;
            
            if (result.required.find(neighbor) == result.required.end()) {
                // Check if neighbor is visible (roughly)
                glm::vec3 neighborCenter = TileCenterWorld(neighbor);
                float neighborRadius = TileBoundingRadius(neighbor);
                if (frustum_.IsSphereVisible(neighborCenter, neighborRadius * 1.5f)) {
                    result.prefetch.push_back(neighbor);
                }
            }
        }
        
        // Prefetch: add children for zoom-in anticipation
        if (key.level < settings.maxZoom - 1) {
            auto children = key.Children();
            for (const auto& child : children) {
                if (result.required.find(child) == result.required.end()) {
                    result.prefetch.push_back(child);
                }
            }
        }
        return true;
    }
    
    // Should subdivide - check if children are ready
    bool childrenReady = AreChildrenReady(key, isReady);
    
    // Child-cull parent fill: if not all 4 children are visible, keep parent as fallback
    int childVisibleCount = 0;
    auto children = key.Children();
    for (const auto& child : children) {
        if (IsTileVisible(child, cameraPos, settings)) {
            childVisibleCount++;
        }
    }
    // Parent fallback when children are not ready OR not fully visible
    if (!childrenReady || childVisibleCount < 4) {
        result.leaves.push_back(key);
        result.leafSet.insert(key);
    }
    
    // Traverse children when ready, otherwise request for loading
    if (childrenReady) {
        for (const auto& child : children) {
            TraverseTile(child, cameraPos, mvp, viewportHeight, isReady, settings, result, depth + 1);
        }
        result.refinedCount++;
    } else {
        for (const auto& child : children) {
            result.required.insert(child);
        }
    }
    
    return true;
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
    
    // Convert km to meters (world units are km)
    double distanceMeters = static_cast<double>(distance) * 1000.0;
    
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

void LodSelector::EnforceNeighborConformance(
    LodSelection& result,
    const TileReadyFunc& isReady,
    const Settings& settings
) {
    // Build maxDescLevel map: for each tile region, track the deepest leaf level
    std::unordered_map<TileKey, int> maxDescLevel;
    for (const TileKey& leaf : result.leafSet) {
        // Walk up ancestors and update max descendant level
        TileKey ancestor = leaf;
        while (ancestor.level >= 0) {
            auto it = maxDescLevel.find(ancestor);
            if (it == maxDescLevel.end()) {
                maxDescLevel[ancestor] = leaf.level;
            } else {
                it->second = std::max(it->second, leaf.level);
            }
            if (ancestor.level == 0) break;
            ancestor = ancestor.Parent();
        }
    }
    
    // Conformance pass loop
    for (int pass = 0; pass < settings.maxConformPasses; ++pass) {
        bool anyRefined = false;
        
        // Collect leaves to refine (can't modify leafSet while iterating)
        std::vector<TileKey> toRefine;
        
        for (const TileKey& leaf : result.leafSet) {
            // Check 4 cardinal neighbors at same level
            static const int dx[] = {0, 1, 0, -1};  // N, E, S, W
            static const int dy[] = {-1, 0, 1, 0};
            
            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborKey = leaf.Neighbor(dx[dir], dy[dir]);
                if (!neighborKey.IsValid()) continue;
                
                // Find deepest level in neighbor region
                int neighborDeepest = neighborKey.level;  // default: same level
                auto it = maxDescLevel.find(neighborKey);
                if (it != maxDescLevel.end()) {
                    neighborDeepest = it->second;
                }
                
                // Check if neighbor is deeper than allowed
                if (neighborDeepest > leaf.level + settings.maxNeighborDelta) {
                    // Need to refine this leaf if children are ready
                    if (AreChildrenReady(leaf, isReady)) {
                        toRefine.push_back(leaf);
                        break;  // Only need to refine once
                    }
                }
            }
        }
        
        // Apply refinements
        for (const TileKey& leaf : toRefine) {
            result.leafSet.erase(leaf);
            auto children = leaf.Children();
            for (const auto& child : children) {
                result.leafSet.insert(child);
                
                // Update maxDescLevel for new children
                TileKey ancestor = child;
                while (ancestor.level >= 0) {
                    auto it = maxDescLevel.find(ancestor);
                    if (it == maxDescLevel.end()) {
                        maxDescLevel[ancestor] = child.level;
                    } else {
                        it->second = std::max(it->second, child.level);
                    }
                    if (ancestor.level == 0) break;
                    ancestor = ancestor.Parent();
                }
            }
            anyRefined = true;
            result.refinedCount++;
        }
        
        if (!anyRefined) break;
    }
    
    // Rebuild leaves vector from leafSet
    result.leaves.clear();
    result.leaves.reserve(result.leafSet.size());
    for (const TileKey& leaf : result.leafSet) {
        result.leaves.push_back(leaf);
    }
    
    // Rebuild required set
    RebuildRequiredSet(result);
}

void LodSelector::RebuildRequiredSet(LodSelection& result) {
    // IMPORTANT: Do NOT clear required - preserve fallback children requests
    // from TraverseTile() that are needed for progressive refinement.
    // Only ADD the new leaf ancestors to ensure conformance leaves are covered.
    
    // Add all leaves and their ancestors up to root
    for (const TileKey& leaf : result.leafSet) {
        TileKey key = leaf;
        while (key.level >= 0) {
            result.required.insert(key);
            if (key.level == 0) break;
            key = key.Parent();
        }
    }
}

} // namespace globe
