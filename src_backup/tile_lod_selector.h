#pragma once

// ============================================================================
// TileLodSelector - Modern SSE-based LOD Selection
// 
// FIXED (2026-02-04): Previously bypassed due to rendering bugs.
// Bug fixes applied:
// - SSE calculation now uses Earth-based geometric error (meters)
// - Distance calculation uses closest point on bounding sphere
// - Tilt factor support added for horizon tiles
// - Fallback logic improved with AreChildrenReady check
// ============================================================================

#include "tile.h"
#include "tile_key.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <array>
#include <functional>

namespace earth {

// Frustum planes for culling
struct Frustum {
    std::array<glm::vec4, 6> planes; // left, right, bottom, top, near, far
    
    static Frustum FromMVP(const glm::mat4& mvp);
    bool ContainsSphere(const glm::vec3& center, float radius) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
};

// Screen-Space Error (SSE) based LOD selection
// Google Earth / Cesium style tile refinement
class TileLodSelector {
public:
    struct Config {
        int minZoom = 2;
        int maxZoom = 22;
        double sseThreshold = 1.4;      // Pixels - refine if SSE > threshold (JS parity)
        bool aggressiveCulling = true;  // Use tight frustum culling
        double horizonCullMargin = 0.1; // Extra margin for horizon culling
        float tiltFactor = 1.0f;        // Camera tilt adjustment (1 - tilt/150)
    };
    
    struct SelectionResult {
        std::unordered_set<std::string> required;  // All tiles needed (for loading)
        std::vector<std::string> leaves;           // Tiles to render
        std::unordered_map<std::string, int> edgeFlags; // Edge stitching flags
        int visibleCount = 0;
        int culledCount = 0;
        int refinedCount = 0;
    };
    
    TileLodSelector();
    explicit TileLodSelector(const Config& config);
    
    // Main selection function
    // Returns tiles to load and render based on camera view
    SelectionResult Select(
        const glm::vec3& cameraPos,
        const glm::mat4& viewProj,
        double windowWidth,
        double windowHeight,
        const std::function<bool(const std::string&)>& isTileReady = nullptr
    );
    
    // Calculate LOD from camera altitude
    static int CalculateLodFromAltitude(double altitudeKm, double globeRadius = 6378.137);
    
    // Calculate SSE for a tile (with tilt factor support)
    double CalculateSSE(const TileKey& key, const glm::vec3& cameraPos,
                        double windowHeight, double fovY = 45.0) const;
    
    // Set tilt factor (called from camera update)
    void SetTiltFactor(float tiltFactor) { config_.tiltFactor = tiltFactor; }
    
    // Helper: Check if all 4 children are ready
    static bool AreChildrenReady(const TileKey& key,
                                  const std::function<bool(const std::string&)>& isTileReady);
    
    // Configuration
    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }
    
private:
    Config config_;
    
    // Tile geometry helpers
    static glm::vec3 TileCenterNormal(int z, int x, int y);
    static float TileAngularRadius(int z, int x, int y);
    static float TileBoundingRadius(int z, int x, int y);
    static double TileGeometricError(int z, double baseError);
    
    // Recursive traversal
    void TraverseTile(
        const TileKey& key,
        const Frustum& frustum,
        const glm::vec3& cameraPos,
        double windowHeight,
        double fovY,
        SelectionResult& result,
        const std::function<bool(const std::string&)>& isTileReady,
        int depth = 0
    );
    
    // Horizon culling
    bool IsOverHorizon(const glm::vec3& tileCenter, const glm::vec3& cameraPos,
                       float tileRadius) const;
    
    // Edge stitching
    int CalculateEdgeFlags(const TileKey& key, const SelectionResult& result) const;
};

// Priority scoring for tile loading (Google Earth style)
struct TilePriorityScorer {
    // Calculate priority score for a tile
    // Higher score = higher priority
    static float Score(
        const TileKey& key,
        const glm::vec3& tileCenter,
        const glm::vec3& cameraPos,
        const glm::mat4& mvp,
        int currentZoom,
        bool isLeaf
    );
};

} // namespace earth
