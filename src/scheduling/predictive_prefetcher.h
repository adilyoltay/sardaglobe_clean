#pragma once

#include "../core/tile_key.h"
#include "../math/frustum.h"
#include <glm/glm.hpp>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>

namespace globe {

// P1-6: Predictive prefetch settings
struct PredictivePrefetchSettings {
    bool enabled = true;                          // Master toggle
    int maxCandidates = 64;                       // Max tiles to add per frame
    float velocityThresholdKmPerSec = 0.05f;      // Minimum speed to trigger prefetch
    float lookaheadMinSec = 1.0f;                 // Minimum prediction time
    float lookaheadMaxSec = 2.0f;                 // Maximum prediction time
    float lookaheadSpeedScale = 0.0015f;          // Speed multiplier for lookahead
    float directionalDotMin = 0.10f;              // Minimum alignment with velocity
    float frustumRadiusScale = 1.8f;              // Frustum expansion for candidates
    int ttlMs = 500;                              // Cache protection TTL (milliseconds)
};

// P1-6: Predictive prefetch statistics
struct PredictivePrefetchStats {
    int generated = 0;           // Total candidates generated
    int filteredByCooldown = 0;  // Rejected due to TTL/cooldown
    int filteredByDirection = 0; // Rejected due to direction mismatch
    int filteredByFrustum = 0;   // Rejected due to frustum culling
    int dispatched = 0;          // Successfully added to prefetch
};

// P1-6: Predictive view prefetcher
// Generates prefetch candidates based on camera velocity and momentum
class PredictivePrefetcher {
public:
    explicit PredictivePrefetcher(const PredictivePrefetchSettings& settings);
    
    // Generate prefetch candidates based on camera state
    // Returns list of tile keys to prefetch
    std::vector<TileKey> GenerateCandidates(
        const glm::vec3& cameraPos,
        const glm::vec3& cameraVelocity,
        const glm::vec3& cameraAcceleration,
        const std::vector<TileKey>& currentLeaves,
        const std::unordered_set<TileKey>& alreadyRequired,
        const std::unordered_set<TileKey>& alreadyPrefetched,
        const Frustum& frustum,
        int minZoom,
        int maxZoom
    );
    
    // Mark a tile as dispatched (for TTL tracking)
    void MarkDispatched(const TileKey& key);
    
    // Check if a tile is in cooldown (TTL protection)
    bool IsInCooldown(const TileKey& key) const;
    
    // Get current statistics
    PredictivePrefetchStats GetStats() const { return stats_; }
    
    // Reset statistics
    void ResetStats() { stats_ = PredictivePrefetchStats{}; }
    
    // Update settings
    void SetSettings(const PredictivePrefetchSettings& settings) { settings_ = settings; }

private:
    PredictivePrefetchSettings settings_;
    mutable std::mutex cooldownMutex_;
    std::unordered_map<TileKey, std::chrono::steady_clock::time_point> cooldownMap_;
    mutable PredictivePrefetchStats stats_;
    
    // Clean up expired cooldown entries
    void CleanupCooldown() const;
};

} // namespace globe
