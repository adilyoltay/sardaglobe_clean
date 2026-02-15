#pragma once

#include "lod_selector.h"
#include "../core/tile.h"
#include "../core/config.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace globe {

// Forward declaration
class DemManager;

// Tile with computed priority score (GE-style SSE + center bias)
struct RankedTile {
    TileKey key;
    float score = 0.0f;  // Higher = more important
};

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
    
    // Elevation-aware culling callback (P2-1 final)
    void SetMaxHeightCallback(LodSelector::GetTileMaxHeightFn callback) {
        selector_.SetMaxHeightCallback(callback);
    }
    
    // P3: Terrain variance callback for adaptive LOD
    void SetTileVarianceCallback(LodSelector::GetTileVarianceFn callback) {
        selector_.SetTileVarianceCallback(callback);
    }
    
    // Center bias weight for scoring (GE-style)
    void SetCenterBiasWeight(float w) { centerBiasWeight_ = w; }
    float GetCenterBiasWeight() const { return centerBiasWeight_; }
    
    // P1: DEM Manager for strict DEM+RGB quorum
    void SetDemManager(DemManager* demManager) { demManager_ = demManager; }
    DemManager* GetDemManager() const { return demManager_; }
    
    // Perform LOD selection and update internal state (with viewDir for scoring)
    // Returns the selection result for this frame
    const LodSelection& Select(
        const glm::vec3& cameraPos,
        const glm::vec3& cameraVelocity, // Camera velocity (km/s) for predictive prefetch
        const glm::vec3& viewDir,  // Camera forward direction for center bias
        const glm::mat4& mvp,
        float fovDegrees,
        float tiltDegrees,         // Camera tilt for horizon culling bypass
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
    
    // Get ranked tiles for fetch prioritization (GE-style SSE + center bias scoring)
    const std::vector<RankedTile>& GetRankedRequired() const { return rankedRequired_; }
    const std::vector<RankedTile>& GetRankedPrefetch() const { return rankedPrefetch_; }
    
    // Stats
    int GetRefinedCount() const { return selection_.refinedCount; }
    int GetLeafCount() const { return static_cast<int>(selection_.leaves.size()); }
    int GetRequiredCount() const { return static_cast<int>(selection_.required.size()); }

private:
    // Check if tile is ready (has texture and is in Ready state)
    static bool IsTileReady(const TileKey& key, const TileMap& tiles);
    
    // P1: Strict readiness check - BOTH texture AND DEM must be ready
    bool IsTileReadyStrict(const TileKey& key, const TileMap& tiles) const;
    
    // Compute tile score: SSE * (1 + w * centerBias)
    float ComputeScore(const TileKey& key, const glm::vec3& cameraPos, 
                       const glm::vec3& viewDir, float fovDegrees, int viewportHeight);
    
    // Build ranked lists from selection
    void BuildRankedLists(const glm::vec3& cameraPos, const glm::vec3& cameraVelocity,
                          const glm::vec3& viewDir, float fovDegrees, int viewportHeight);
    
    LodSelector selector_;
    LodSelector::Settings settings_;
    LodSelection selection_;
    
    // Cache prefetch set for O(1) lookup
    std::unordered_set<TileKey> prefetchSet_;
    
    // Ranked tiles for fetch prioritization (sorted by score descending)
    std::vector<RankedTile> rankedRequired_;
    std::vector<RankedTile> rankedPrefetch_;
    
    // Scoring parameters
    float centerBiasWeight_ = 0.3f;
    
    // P1: DEM Manager for strict DEM+RGB quorum checks
    DemManager* demManager_ = nullptr;
    
    // P3: Weighted scheduler aging state (first seen timestamps in ms)
    std::unordered_map<TileKey, double> requiredFirstSeenMs_;
    std::unordered_map<TileKey, double> prefetchFirstSeenMs_;
    double currentTimeMs_ = 0.0;
};

} // namespace globe
