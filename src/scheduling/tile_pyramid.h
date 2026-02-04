#pragma once

#include "lod_selector.h"
#include "../core/tile.h"
#include "../core/config.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>

namespace globe {

// TilePyramid - Centralized tile selection and management (GE-style)
// Encapsulates LOD selection, required/prefetch sets, and tile state queries
class TilePyramid {
public:
    using TileMap = std::unordered_map<TileKey, Tile>;
    
    TilePyramid() = default;
    
    // Configure LOD settings
    void SetSettings(const LodSelector::Settings& settings) { settings_ = settings; }
    LodSelector::Settings& GetSettings() { return settings_; }
    const LodSelector::Settings& GetSettings() const { return settings_; }
    
    // Perform LOD selection and update internal state
    // Returns the selection result for this frame
    const LodSelection& Select(
        const glm::vec3& cameraPos,
        const glm::mat4& mvp,
        float fovDegrees,
        int viewportWidth,
        int viewportHeight,
        const TileMap& tiles
    );
    
    // Access current selection
    const LodSelection& GetSelection() const { return selection_; }
    
    // Query helpers
    bool IsLeaf(const TileKey& key) const { return selection_.leafSet.count(key) > 0; }
    bool IsRequired(const TileKey& key) const { return selection_.required.count(key) > 0; }
    bool IsPrefetch(const TileKey& key) const;
    
    // Get visible leaf tiles (for rendering)
    const std::vector<TileKey>& GetLeaves() const { return selection_.leaves; }
    
    // Get prefetch tiles (for background loading)
    const std::vector<TileKey>& GetPrefetch() const { return selection_.prefetch; }
    
    // Get all required tiles (leaves + ancestors)
    const std::unordered_set<TileKey>& GetRequired() const { return selection_.required; }
    
    // Stats
    int GetRefinedCount() const { return selection_.refinedCount; }
    int GetLeafCount() const { return static_cast<int>(selection_.leaves.size()); }
    int GetRequiredCount() const { return static_cast<int>(selection_.required.size()); }

private:
    // Check if tile is ready (has texture and is in Ready state)
    static bool IsTileReady(const TileKey& key, const TileMap& tiles);
    
    LodSelector selector_;
    LodSelector::Settings settings_;
    LodSelection selection_;
    
    // Cache prefetch set for O(1) lookup
    std::unordered_set<TileKey> prefetchSet_;
};

} // namespace globe
