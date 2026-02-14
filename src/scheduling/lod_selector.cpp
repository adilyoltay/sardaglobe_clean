#include "lod_selector.h"
#include "../math/tile_math.h"
#include "../core/constants.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace globe {

LodSelection LodSelector::Select(
    const glm::vec3& cameraPos,
    const glm::vec3& cameraVelocity,
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
    
    // Conservative horizon: shrink the occluder slightly so large tiles near the horizon
    // are not false-negative culled (which manifests as low-LOD black "holes" and
    // zoom-in stalls where no new children ever get requested).
    constexpr float HORIZON_OCCLUDER_SHRINK = 0.9985f;  // ~0.15% radius shrink (~10km)
    float horizonRadius = static_cast<float>(EARTH_RADIUS_KM) * HORIZON_OCCLUDER_SHRINK;
    horizon_.Update(cameraPos, horizonRadius);
    
    // Faz 3A: Apply safety margin from settings
    if (settings.useHorizonCulling) {
        // Convert radian safety margin to a factor for cosine threshold
        // Small positive margin = more conservative (keep more tiles)
        // We use a simplified approximation: factor = 1 + margin/10
        float safetyFactor = 1.0f + settings.horizonSafetyMarginRad * 10.0f;
        horizon_.SetSafetyMargin(safetyFactor);
    }
    
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

    // Predictive prefetch (GE-style): bias prefetch towards camera momentum.
    AddPredictivePrefetch(cameraPos, cameraVelocity, settings, result);

    // Hard guard: never return an empty leaf set when we still have required tiles.
    // This prevents one-frame "all missing" artifacts during rapid LOD transitions.
    if (result.leafSet.empty() && !result.required.empty()) {
        int minLevel = std::numeric_limits<int>::max();
        for (const TileKey& key : result.required) {
            minLevel = std::min(minLevel, key.level);
        }
        if (minLevel == std::numeric_limits<int>::max()) {
            minLevel = 0;
        }

        for (const TileKey& key : result.required) {
            if (key.level == minLevel) {
                result.leafSet.insert(key);
                result.leaves.push_back(key);
            }
        }
    }

    // Save finalized leaf set for next frame's hysteresis check.
    previousLeafSet_ = result.leafSet;
    
    return result;
}

bool LodSelector::IsTileVisible(
    const TileKey& key,
    const glm::vec3& cameraPos,
    const Settings& settings
) const {
    // Calculate tile geometry
    glm::vec3 center = TileCenterWorld(key);
    
    // Elevation-aware bounding radius (P2-1 final)
    // Yüksek arazi (Himalayalar vb.) için doğru culling
    float maxHeightKm = 0.0f;
    if (getMaxHeight_) {
        maxHeightKm = getMaxHeight_(key);
    }
    float radius = TileBoundingRadius(key, maxHeightKm, 0.4f);  // 0.4km = skirt
    
    // Conservative bounding:
    // We intentionally inflate bounds at low LODs because any false-negative cull
    // creates a visible "black hole" (coverage gap) that has no render-time fallback.
    // Cost is low at coarse levels, correctness is critical.
    float margin = 1.25f;
    if (key.level <= 2) {
        margin = 2.0f;
    } else if (key.level <= 4) {
        margin = 1.6f;
    }
    float conservativeRadius = radius * margin;
    
    // Frustum culling
    if (!settings.disableFrustumCull) {
        if (!frustum_.IsSphereVisible(center, conservativeRadius)) {
            return false;
        }
    }
    
    // Horizon culling (bypass at high tilt - 45° threshold for better oblique coverage)
    constexpr float HORIZON_BYPASS_TILT = 45.0f;
    if (settings.useHorizonCulling && !settings.disableHorizonCull && tiltDegrees_ < HORIZON_BYPASS_TILT) {
        // Slightly inflate bounds for horizon test: horizon false-negatives are the
        // primary source of low-LOD missing-tile rectangles in practice.
        float horizonRadius = conservativeRadius * settings.horizonNearHorizonInflation;
        if (!horizon_.IsSphereVisible(center, horizonRadius)) {
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
    
    auto children = key.Children();
    bool wasRefinedLastFrame = false;
    if (!previousLeafSet_.empty()) {
        for (const auto& child : children) {
            if (previousLeafSet_.count(child)) {
                wasRefinedLastFrame = true;
                break;
            }
        }
    }

    // Check if should subdivide (SSE + hysteresis + quality + minLodPixels).
    bool subdivide = ShouldSubdivide(
        key, cameraPos, viewportHeight, fovDegrees_,
        settings.sseThreshold, settings.tiltFactor,
        wasRefinedLastFrame, settings.sseHysteresisRatio,
        settings.minLodPixels,      // GE parity: min visible tile size
        settings.qualityMultiplier  // GE parity: quality mode
    );

    // Progressive refinement budget (GE-style smoothness):
    // avoid large one-frame 4x leaf explosions by capping how many parent->child
    // refinements are applied in one selection pass.
    const bool refineBudgetEnabled = settings.maxRefinementsPerFrame > 0;
    const bool refineBudgetReached =
        refineBudgetEnabled && result.refinedCount >= settings.maxRefinementsPerFrame;
    if (subdivide && refineBudgetReached) {
        // Keep parent as leaf for this frame, but request children so data streams in.
        result.leaves.push_back(key);
        result.leafSet.insert(key);
        auto children = key.Children();
        for (const auto& child : children) {
            result.required.insert(child);
        }
        return true;
    }
    
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
    bool childrenReady = AreChildrenReady(key, isReady, settings);
    
    // Keep parent fallback only when children are not ready.
    // If children are ready, retaining a coarse parent can violate neighbor conformance
    // and produce unnecessary mixed-LOD artifacts.
    if (!childrenReady) {
        result.leaves.push_back(key);
        result.leafSet.insert(key);
    }
    
    // Traverse children when ready, otherwise request for loading.
    if (childrenReady) {
        // Count this split before descending so budget checks in descendants
        // see an up-to-date refinement count in the same frame.
        ++result.refinedCount;

        // NOTE: Do not require all 4 children to be "visible" before refining.
        // That stalls refinement at the horizon and can lock the engine at very
        // coarse LODs (visible in debug as Leaves: 1, Required: ~4 and no new
        // network fetches on zoom-in/out).
        //
        // We simply traverse all children and let conservative per-tile culling
        // decide which subtrees contribute to the final leaf set.
        bool anyChildVisible = false;
        for (const auto& child : children) {
            bool childVisible = TraverseTile(child, cameraPos, mvp, viewportHeight, isReady, settings, result, depth + 1);
            anyChildVisible = anyChildVisible || childVisible;
        }
        // Guard against visibility edge-cases: if all refined children were culled,
        // keep parent as fallback leaf for full coverage continuity.
        if (!anyChildVisible) {
            result.leaves.push_back(key);
            result.leafSet.insert(key);
            --result.refinedCount;
        }
    } else {
        // Child quorum requires *all* children to eventually become ready; otherwise,
        // refinement can deadlock when some children are culled and therefore never
        // requested. This manifests as "no new tiles load on zoom" after startup.
        if (settings.lodChildQuorum) {
            for (const auto& child : children) {
                result.required.insert(child);
            }
        } else {
            // IMPORTANT: Request children conservatively even when visibility tests fail.
            // If the parent tile is visible and subdivide is requested, at least one child must
            // be visible geometrically. When our conservative sphere/frustum/horizon tests
            // produce a false-negative for all 4 children, failing to request them causes a
            // permanent LOD stall (no new raster/DEM tiles ever get scheduled).
            int visibleChildCount = 0;
            for (const auto& child : children) {
                if (IsTileVisible(child, cameraPos, settings)) {
                    result.required.insert(child);
                    ++visibleChildCount;
                }
            }
            if (visibleChildCount == 0) {
                for (const auto& child : children) {
                    result.required.insert(child);
                }
            }
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
    float tiltFactor,
    bool isCurrentlySubdivided,
    float hysteresisRatio,
    float minLodPixels,      // GE parity: min visible tile size
    float qualityMultiplier  // GE parity: quality mode multiplier
) {
    glm::vec3 center = TileCenterWorld(key);
    float radius = TileBoundingRadius(key);
    
    // Distance to closest point on bounding sphere
    float distance = glm::length(center - cameraPos);
    distance = std::max(1.0f, distance - radius);
    
    // Convert km to meters (world units are km)
    double distanceMeters = static_cast<double>(distance) * 1000.0;
    
    // Compute SSE
    float sse = ComputeSSE(key, distanceMeters, viewportHeight, fovDegrees);
    
    // minLodPixels check: Compute projected tile size at CHILD level
    // If children would be too small to see, don't subdivide.
    // NOTE: Set minLodPixels = 0.0f in tests to disable for forced-refine scenarios.
    if (minLodPixels > 0.0f && key.level < 22) {
        float childGeometricError = ComputeGeometricError(key.level + 1);
        float fovRad = fovDegrees * static_cast<float>(M_PI) / 180.0f;
        float sseFactor = static_cast<float>(viewportHeight) / (2.0f * std::tan(fovRad / 2.0f));
        // Fix: Multiply by 256 (tile size) because ComputeGeometricError returns meters/pixel,
        // but we want to compare the full tile's screen size against minLodPixels.
        float childProjectedSize = (childGeometricError * 256.0f / static_cast<float>(distanceMeters)) * sseFactor;
        if (childProjectedSize < minLodPixels) {
            return false;
        }
    }
    
    // Apply tilt factor (reduce detail when tilted)
    // Floor raised to 0.25 (max 4x inflation) to prevent LOD stall at extreme tilt
    float adjustedThreshold = sseThreshold / std::max(0.25f, tiltFactor);

    // Apply quality mode multiplier (GE parity: 1.0/2.0/4.0)
    adjustedThreshold *= qualityMultiplier;

    // Hysteresis: once refined, use a lower collapse threshold to avoid
    // frame-to-frame oscillation near the boundary.
    if (isCurrentlySubdivided && hysteresisRatio > 0.0f && hysteresisRatio < 1.0f) {
        adjustedThreshold *= hysteresisRatio;
    }

    return sse > adjustedThreshold;
}

bool LodSelector::AreChildrenReady(const TileKey& key, const TileReadyFunc& isReady, const Settings& settings) {
    auto children = key.Children();
    int readyCount = 0;
    for (const auto& child : children) {
        if (isReady(child)) {
            ++readyCount;
        }
    }
    if (settings.lodChildQuorum) {
        return readyCount == static_cast<int>(children.size());
    }
    return readyCount > 0;
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
                    if (AreChildrenReady(leaf, isReady, settings)) {
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

void LodSelector::AddPredictivePrefetch(
    const glm::vec3& cameraPos,
    const glm::vec3& cameraVelocity,
    const Settings& settings,
    LodSelection& result
) {
    float speedKmPerSec = glm::length(cameraVelocity);
    if (!std::isfinite(speedKmPerSec) || speedKmPerSec < 0.05f) {
        return;  // Ignore tiny/noisy velocity.
    }

    glm::vec3 velocityDir = glm::normalize(cameraVelocity);
    float predictionSeconds = std::clamp(1.0f + speedKmPerSec * 0.0015f, 1.0f, 2.0f);
    glm::vec3 predictedCameraPos = cameraPos + cameraVelocity * predictionSeconds;

    std::unordered_set<TileKey> seen;
    seen.reserve(result.required.size() + result.prefetch.size() + 32);
    for (const TileKey& key : result.required) {
        seen.insert(key);
    }
    for (const TileKey& key : result.prefetch) {
        seen.insert(key);
    }

    constexpr int kMaxPredictiveAdds = 64;
    int added = 0;

    auto tryAddCandidate = [&](const TileKey& candidate) {
        if (added >= kMaxPredictiveAdds) return;
        if (!candidate.IsValid()) return;
        if (seen.find(candidate) != seen.end()) return;
        if (candidate.level < settings.minZoom || candidate.level > settings.maxZoom) return;

        glm::vec3 center = TileCenterWorld(candidate);
        float radius = TileBoundingRadius(candidate);
        float currentDist = glm::length(center - cameraPos);
        float predictedDist = glm::length(center - predictedCameraPos);
        if (!std::isfinite(currentDist) || !std::isfinite(predictedDist)) return;

        glm::vec3 toTileDir = glm::normalize(center - cameraPos);
        float directional = glm::dot(velocityDir, toTileDir);
        bool predictedCloser = predictedDist + radius < currentDist;
        if (directional < 0.10f && !predictedCloser) {
            return;
        }

        // Conservative frustum guard: keep candidates near the current view volume.
        if (!frustum_.IsSphereVisible(center, radius * 1.8f)) {
            return;
        }

        result.prefetch.push_back(candidate);
        seen.insert(candidate);
        ++added;
    };

    // Use current leaves as seed; expand in motion direction.
    for (const TileKey& leaf : result.leaves) {
        if (added >= kMaxPredictiveAdds) break;

        auto neighbors = leaf.Neighbors();
        for (const TileKey& neighbor : neighbors) {
            tryAddCandidate(neighbor);
            if (added >= kMaxPredictiveAdds) break;
        }

        if (leaf.level < settings.maxZoom) {
            auto children = leaf.Children();
            for (const TileKey& child : children) {
                tryAddCandidate(child);
                if (added >= kMaxPredictiveAdds) break;
            }
        }
    }
}

} // namespace globe
