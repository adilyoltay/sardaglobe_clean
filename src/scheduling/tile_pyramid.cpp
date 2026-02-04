#include "tile_pyramid.h"
#include "../math/tile_math.h"
#include <algorithm>
#include <cmath>

namespace globe {

const LodSelection& TilePyramid::Select(
    const glm::vec3& cameraPos,
    const glm::vec3& viewDir,
    const glm::mat4& mvp,
    float fovDegrees,
    int viewportWidth,
    int viewportHeight,
    const TileMap& tiles
) {
    // Create ready check function that captures tiles reference
    auto isReady = [&tiles](const TileKey& key) -> bool {
        return IsTileReady(key, tiles);
    };
    
    // Perform LOD selection
    selection_ = selector_.Select(
        cameraPos,
        mvp,
        fovDegrees,
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
    BuildRankedLists(cameraPos, viewDir, fovDegrees, viewportHeight);
    
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
    float sse = ComputeSSE(key.level, distanceMeters, viewportHeight, fovDegrees);
    
    // Center bias: how much the tile is in the center of view
    // dirToTile = normalize(tileCenter - cameraPos)
    // centerBias = max(0, dot(viewDir, dirToTile))
    glm::vec3 dirToTile = glm::normalize(tileCenter - cameraPos);
    float centerBias = std::max(0.0f, glm::dot(viewDir, dirToTile));
    
    // Final score: SSE * (1 + w * centerBias)
    float score = sse * (1.0f + centerBiasWeight_ * centerBias);
    
    return score;
}

void TilePyramid::BuildRankedLists(const glm::vec3& cameraPos, const glm::vec3& viewDir,
                                    float fovDegrees, int viewportHeight) {
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
    
    // Rank prefetch tiles
    rankedPrefetch_.reserve(selection_.prefetch.size());
    for (const TileKey& key : selection_.prefetch) {
        float score = ComputeScore(key, cameraPos, viewDir, fovDegrees, viewportHeight);
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
    return it->second.IsReady();
}

} // namespace globe
