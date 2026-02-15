#include "tile_pyramid.h"
#include "../math/tile_math.h"
#include "../io/dem_manager.h"  // P1: For strict DEM+RGB quorum
#include <GLFW/glfw3.h>         // P3: For time measurement
#include <algorithm>
#include <cmath>

namespace globe {

const LodSelection& TilePyramid::Select(
    const glm::vec3& cameraPos,
    const glm::vec3& cameraVelocity,
    const glm::vec3& viewDir,
    const glm::mat4& mvp,
    float fovDegrees,
    float tiltDegrees,
    int viewportWidth,
    int viewportHeight,
    const TileMap& tiles
) {
    // P1: Use strict DEM+RGB quorum only if DEM manager is healthy
    const bool useStrictQuorum = demManager_ && 
                                  settings_.strictDemRgbQuorum &&
                                  demManager_->GetHealthStatus() == DemHealthStatus::Healthy;
    
    auto isReady = [this, useStrictQuorum, &tiles](const TileKey& key) -> bool {
        if (useStrictQuorum) {
            return IsTileReadyStrict(key, tiles);
        }
        return IsTileReady(key, tiles);
    };
    
    // Perform LOD selection
    selection_ = selector_.Select(
        cameraPos,
        cameraVelocity,
        mvp,
        fovDegrees,
        tiltDegrees,
        viewportWidth,
        viewportHeight,
        isReady,
        settings_
    );
    
    // Update prefetch set for O(1) lookup
    prefetchSet_.clear();
    for (const auto& key : selection_.prefetch) {
        prefetchSet_.insert(key);
    }
    
    // P3: Update current time for aging calculations (convert to ms)
    currentTimeMs_ = glfwGetTime() * 1000.0;
    
    // Build ranked lists for fetch prioritization (GE-style scoring)
    BuildRankedLists(cameraPos, cameraVelocity, viewDir, fovDegrees, viewportHeight);
    
    return selection_;
}

float TilePyramid::ComputeScore(const TileKey& key, const glm::vec3& cameraPos,
                                 const glm::vec3& viewDir, float fovDegrees, int viewportHeight) {
    // Get tile center and radius (in km)
    glm::vec3 tileCenter = TileCenterWorld(key);
    float tileRadius = TileBoundingRadius(key);
    
    // Distance from camera to tile (km), clamped to avoid division issues
    float distanceKm = std::max(1.0f, glm::length(tileCenter - cameraPos) - tileRadius);
    float distanceMeters = distanceKm * 1000.0f;
    
    // P4: Weighted score computation
    float score = 0.0f;
    
    // SSE term with center bias
    if (settings_.schedulerSseWeight > 0.0f) {
        float sse = ComputeSSE(key, distanceMeters, viewportHeight, fovDegrees);
        glm::vec3 dirToTile = glm::normalize(tileCenter - cameraPos);
        float centerBias = std::max(0.0f, glm::dot(viewDir, dirToTile));
        // SSE * (1 + centerBiasWeight * centerBias)
        float sseTerm = sse * (1.0f + settings_.schedulerCenterBiasWeight * centerBias);
        score += settings_.schedulerSseWeight * sseTerm;
    }
    
    // Distance term
    if (settings_.schedulerDistanceWeight > 0.0f) {
        float distanceTerm = 1.0f / (distanceKm + 1.0f);
        score += settings_.schedulerDistanceWeight * distanceTerm;
    }
    
    // LOD level term
    if (settings_.schedulerLodWeight > 0.0f) {
        float lodTerm = static_cast<float>(key.level) / 
                       std::max(1.0f, static_cast<float>(settings_.maxZoom + 1));
        score += settings_.schedulerLodWeight * lodTerm;
    }
    
    return score;
}

void TilePyramid::BuildRankedLists(const glm::vec3& cameraPos, const glm::vec3& cameraVelocity,
                                   const glm::vec3& viewDir, float fovDegrees, int viewportHeight) {
    // Clear previous rankings
    rankedRequired_.clear();
    rankedPrefetch_.clear();
    
    // P3: Update aging state - add new tiles, remove old ones
    if (settings_.useWeightedScheduler) {
        // Add new required tiles to aging map
        for (const TileKey& key : selection_.required) {
            if (requiredFirstSeenMs_.find(key) == requiredFirstSeenMs_.end()) {
                requiredFirstSeenMs_[key] = currentTimeMs_;
            }
        }
        // Remove tiles no longer required
        for (auto it = requiredFirstSeenMs_.begin(); it != requiredFirstSeenMs_.end();) {
            if (selection_.required.find(it->first) == selection_.required.end()) {
                it = requiredFirstSeenMs_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Same for prefetch
        for (const TileKey& key : selection_.prefetch) {
            if (prefetchFirstSeenMs_.find(key) == prefetchFirstSeenMs_.end()) {
                prefetchFirstSeenMs_[key] = currentTimeMs_;
            }
        }
        for (auto it = prefetchFirstSeenMs_.begin(); it != prefetchFirstSeenMs_.end();) {
            if (prefetchSet_.find(it->first) == prefetchSet_.end()) {
                it = prefetchFirstSeenMs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Rank required tiles
    rankedRequired_.reserve(selection_.required.size());
    for (const TileKey& key : selection_.required) {
        float score = ComputeScore(key, cameraPos, viewDir, fovDegrees, viewportHeight);
        
        // P4: Apply aging boost if weighted scheduler is enabled
        if (settings_.useWeightedScheduler && settings_.schedulerUseAging) {
            auto it = requiredFirstSeenMs_.find(key);
            if (it != requiredFirstSeenMs_.end()) {
                double ageMs = currentTimeMs_ - it->second;
                float halfLifeMs = settings_.schedulerAgingHalfLifeMs;
                if (halfLifeMs > 0.0f && settings_.schedulerAgingWeight > 0.0f) {
                    // ageFactor = schedulerAgingWeight * (1 - 2^(-age/halfLife))
                    float ageFactor = settings_.schedulerAgingWeight * 
                                     (1.0f - std::pow(0.5f, static_cast<float>(ageMs / halfLifeMs)));
                    float ageBoost = 1.0f + ageFactor;
                    score *= ageBoost;
                }
            }
        }
        
        rankedRequired_.push_back({key, score});
    }
    
    // Sort by score descending (higher score = more important)
    std::sort(rankedRequired_.begin(), rankedRequired_.end(),
              [](const RankedTile& a, const RankedTile& b) { return a.score > b.score; });
    
    // Rank prefetch tiles.
    // GE RE-aligned predictive priority:
    // score = 1 / (predicted_distance + 1), where predicted distance is measured
    // from camera position projected 1-2 seconds forward along current velocity.
    float speedKmPerSec = glm::length(cameraVelocity);
    bool predictiveActive = std::isfinite(speedKmPerSec) && speedKmPerSec >= 0.05f;
    float predictionSeconds = std::clamp(1.0f + speedKmPerSec * 0.0015f, 1.0f, 2.0f);
    glm::vec3 predictedCameraPos = cameraPos + cameraVelocity * predictionSeconds;

    // Rank prefetch tiles
    rankedPrefetch_.reserve(selection_.prefetch.size());
    for (const TileKey& key : selection_.prefetch) {
        float score = ComputeScore(key, cameraPos, viewDir, fovDegrees, viewportHeight);
        
        if (predictiveActive) {
            glm::vec3 center = TileCenterWorld(key);
            float radius = TileBoundingRadius(key);
            float predictedDistance = glm::length(center - predictedCameraPos);
            predictedDistance = std::max(0.0f, predictedDistance - radius);
            float predictedScore = 1.0f / (predictedDistance + 1.0f);

            // P4: Directional bias with configurable weight
            // Tiles along momentum vector are preferred
            glm::vec3 velDir = glm::normalize(cameraVelocity);
            glm::vec3 dirToTile = glm::normalize(center - cameraPos);
            float directional = std::max(0.0f, glm::dot(velDir, dirToTile));
            predictedScore *= (1.0f + settings_.schedulerDirectionalPredictiveWeight * directional);

            score = predictedScore;
        }
        
        // P4: Apply aging boost to prefetch tiles too (AFTER predictive, so it multiplies)
        if (settings_.useWeightedScheduler && settings_.schedulerUseAging) {
            auto it = prefetchFirstSeenMs_.find(key);
            if (it != prefetchFirstSeenMs_.end()) {
                double ageMs = currentTimeMs_ - it->second;
                float halfLifeMs = settings_.schedulerAgingHalfLifeMs;
                if (halfLifeMs > 0.0f && settings_.schedulerAgingWeight > 0.0f) {
                    // ageFactor = schedulerAgingWeight * (1 - 2^(-age/halfLife))
                    float ageFactor = settings_.schedulerAgingWeight * 
                                     (1.0f - std::pow(0.5f, static_cast<float>(ageMs / halfLifeMs)));
                    float ageBoost = 1.0f + ageFactor;
                    score *= ageBoost;
                }
            }
        }
        
        rankedPrefetch_.push_back({key, score});
    }
    
    // Sort by score descending
    std::sort(rankedPrefetch_.begin(), rankedPrefetch_.end(),
              [](const RankedTile& a, const RankedTile& b) { return a.score > b.score; });
}

bool TilePyramid::IsPrefetch(const TileKey& key) const {
    return prefetchSet_.count(key) > 0;
}

bool TilePyramid::IsTileReady(const TileKey& key, const TileMap& tiles) {
    auto it = tiles.find(key);
    if (it == tiles.end()) return false;
    // Child quorum must be based on texture readiness.
    // Requiring mesh here deadlocks refinement because child meshes are built
    // after they become leaves.
    return it->second.IsReady();
}

// P1: Strict readiness check - BOTH texture AND DEM must be ready
bool TilePyramid::IsTileReadyStrict(const TileKey& key, const TileMap& tiles) const {
    auto it = tiles.find(key);
    if (it == tiles.end()) return false;
    
    // Texture readiness (existing check)
    bool textureReady = it->second.IsReady();
    if (!textureReady) return false;
    
    // P1: DEM readiness check (strict quorum)
    if (demManager_) {
        return demManager_->HasData(key);
    }
    
    // No DEM manager configured - fall back to texture-only check
    return true;
}

} // namespace globe
