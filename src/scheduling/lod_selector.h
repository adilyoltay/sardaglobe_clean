#pragma once

#include "../core/tile_key.h"
#include "../core/tile.h"
#include "../core/config.h"
#include "../math/frustum.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>

namespace globe {

// Result of LOD selection
struct LodSelection {
    std::unordered_set<TileKey> required;  // All tiles needed (ancestors + leaves)
    std::unordered_set<TileKey> leafSet;   // O(1) leaf lookup (same content as leaves)
    std::vector<TileKey> leaves;           // Tiles to render (ordered)
    std::vector<TileKey> prefetch;         // Tiles to prefetch (neighbors, next zoom)
    int refinedCount = 0;                  // Number of subdivisions
};

// SSE-based LOD selector with frustum/horizon culling
class LodSelector {
public:
    using TileReadyFunc = std::function<bool(const TileKey&)>;
    
    struct Settings {
        int minZoom = 0;
        int maxZoom = 22;
        float sseThreshold = 2.0f;  // GE standard quality (was 1.4f)
        float tiltFactor = 1.0f;  // 0-1, reduces detail when tilted
        // Limits how many parent->children refinements can happen in one selection pass.
        // <=0 means unlimited (legacy behavior).
        int maxRefinementsPerFrame = 0;

        // SSE hysteresis: once a tile refines (children become leaves), require
        // SSE to drop to threshold * sseHysteresisRatio before collapsing back.
        // Prevents frame-to-frame LOD oscillation that causes visible flickering.
        // 0.0 = no hysteresis, 0.8 = 20% dead zone (default).
        float sseHysteresisRatio = 0.80f;

        // Neighbor LOD conformance (FAZ 1.2)
        int maxNeighborDelta = 1;      // Max LOD difference between neighbors
        bool enforceNeighborDelta = true;  // Enable conformance pass
        int maxConformPasses = 6;      // Prevent infinite refinement
        bool lodChildQuorum = true;    // Refine only when full child set is render-ready

        // minLodPixels culling (GE parity): tiles smaller than this are invisible.
        // Prevents rendering sub-pixel tiles at distant zoom levels. Tests override to 0.0f.
        float minLodPixels = 256.0f;  // GE default (disable via 0.0f for test compatibility)

        // Quality mode multiplier (GE parity: 1.0/2.0/4.0)
        float qualityMultiplier = 1.0f;  // Applied to sseThreshold (1.0 = GE standard)

        // Debug culling toggles (for gap diagnosis)
        bool disableFrustumCull = false;
        bool disableHorizonCull = false;
    };
    
    LodSelector() = default;
    
    // Perform LOD selection
    // fovDegrees should come directly from camera, NOT extracted from MVP
    LodSelection Select(
        const glm::vec3& cameraPos,
        const glm::vec3& cameraVelocity,
        const glm::mat4& mvp,
        float fovDegrees,  // CRITICAL: Pass FOV directly from camera
        float tiltDegrees, // Camera tilt (0=looking down, 90=horizon)
        int viewportWidth,
        int viewportHeight,
        const TileReadyFunc& isReady,
        const Settings& settings
    );

private:
    bool TraverseTile(
        const TileKey& key,
        const glm::vec3& cameraPos,
        const glm::mat4& mvp,
        int viewportHeight,
        const TileReadyFunc& isReady,
        const Settings& settings,
        LodSelection& result,
        int depth
    );

    // Conservative visibility test (frustum + horizon)
    bool IsTileVisible(
        const TileKey& key,
        const glm::vec3& cameraPos,
        const Settings& settings
    ) const;
    
    bool ShouldSubdivide(
        const TileKey& key,
        const glm::vec3& cameraPos,
        int viewportHeight,
        float fovDegrees,
        float sseThreshold,
        float tiltFactor,
        bool isCurrentlySubdivided,
        float hysteresisRatio,
        float minLodPixels = 256.0f,      // GE parity: min visible tile size
        float qualityMultiplier = 1.0f    // GE parity: quality mode multiplier
    );
    
    bool AreChildrenReady(const TileKey& key, const TileReadyFunc& isReady, const Settings& settings);
    
    // Neighbor LOD conformance (FAZ 1.2)
    void EnforceNeighborConformance(
        LodSelection& result,
        const TileReadyFunc& isReady,
        const Settings& settings
    );
    
    void RebuildRequiredSet(LodSelection& result);
    void AddPredictivePrefetch(
        const glm::vec3& cameraPos,
        const glm::vec3& cameraVelocity,
        const Settings& settings,
        LodSelection& result
    );
    
    Frustum frustum_;
    HorizonCuller horizon_;
    float fovDegrees_ = 45.0f;
    float tiltDegrees_ = 0.0f;  // For horizon culling bypass
    std::unordered_set<TileKey> previousLeafSet_;  // SSE hysteresis state
};

} // namespace globe
