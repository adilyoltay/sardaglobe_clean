#include "tile_pyramid.h"
#include "../math/tile_math.h"
#include "../io/dem_manager.h"  // P1: For strict DEM+RGB quorum
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
    // P1: Use strict DEM+RGB quorum if DEM manager is configured
    auto isReady = [this, &tiles](const TileKey& key) -> bool {
        if (demManager_ && settings_.strictDemRgbQuorum) {
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
    
    // Compute SSE (Screen-Space Error) - uses tile level for geometric error
    float sse = ComputeSSE(key, distanceMeters, viewportHeight, fovDegrees);
    
    // Center bias: how much the tile is in the center of view
    // dirToTile = normalize(tileCenter - cameraPos)
    // centerBias = max(0, dot(viewDir, dirToTile))
    glm::vec3 dirToTile = glm::normalize(tileCenter - cameraPos);
    float centerBias = std::max(0.0f, glm::dot(viewDir, dirToTile));
    
    // Final score: SSE * (1 + w * centerBias)
    float score = sse * (1.0f + centerBiasWeight_ * centerBias);
    
    return score;
}

void TilePyramid::BuildRankedLists(const glm::vec3& cameraPos, const glm::vec3& cameraVelocity,
                                   const glm::vec3& viewDir, float fovDegrees, int viewportHeight) {
    // Clear previous rankings
    rankedRequired_.clear();
    rankedPrefetch_.clear();
    
    // Rank required tiles
    rankedRequired_.reserve(selection_.required.size());
    for (const TileKey& key : selection_.required) {
        float score = ComputeScore(key, cameraPos, viewDir, fovDegrees, viewportHeight);
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

            // Directional bias so tiles along momentum vector are preferred.
            glm::vec3 velDir = glm::normalize(cameraVelocity);
            glm::vec3 dirToTile = glm::normalize(center - cameraPos);
            float directional = std::max(0.0f, glm::dot(velDir, dirToTile));
            predictedScore *= (1.0f + 0.5f * directional);

            score = predictedScore;
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
