#include "tile_pyramid.h"

namespace globe {

const LodSelection& TilePyramid::Select(
    const glm::vec3& cameraPos,
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
    
    return selection_;
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
