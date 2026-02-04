#pragma once

// ============================================================================
// DEPRECATED: TileLodSelector
// This class is currently NOT USED due to rendering bugs.
// The legacy BuildVisibleTileSets() in globe_engine.cpp is used instead.
// 
// TODO: Either fix the SSE calculation bugs and enable this class, or
// remove it entirely if the legacy approach is sufficient.
// 
// Known issues:
// - SSE threshold calibration differs from legacy implementation
// - Edge case handling for polar regions needs work
// - Fallback tile selection logic incomplete
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
        double sseThreshold = 2.0;      // Pixels - refine if SSE > threshold
        double geometricError = 1.0;    // Base geometric error at zoom 0
        bool aggressiveCulling = true;  // Use tight frustum culling
        double horizonCullMargin = 0.1; // Extra margin for horizon culling
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
    
    // Calculate SSE for a tile
    double CalculateSSE(const TileKey& key, const glm::vec3& cameraPos,
                        double windowHeight, double fovY = 45.0) const;
    
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
