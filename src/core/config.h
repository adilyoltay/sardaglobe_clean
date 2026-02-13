#pragma once

#include "constants.h"
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace globe {

// Terrain displacement authority
enum class DisplacementMode {
    CPU_MESH_BAKE,        // DEM baked into mesh vertices (default, proven)
    GPU_HEIGHTMAP_DISPLACE // Flat mesh + GPU vertex shader displacement
};

// Google Earth quality modes for LOD selection
enum class QualityMode {
    LOW = 0,       // SSE threshold = 4.0 (~25% tiles, battery saver)
    MEDIUM = 1,    // SSE threshold = 2.0 (GE standard quality)
    HIGH = 2,      // SSE threshold = 1.4 (higher quality)
    ULTRA = 3      // SSE threshold = 1.0 (~400% tiles, screenshots)
};

// Convert quality mode to SSE threshold multiplier
inline float QualityModeToMultiplier(QualityMode mode) {
    switch (mode) {
        case QualityMode::LOW:    return 2.0f;   // 4.0 / 2.0
        case QualityMode::MEDIUM: return 1.0f;   // 2.0 / 2.0 (GE standard)
        case QualityMode::HIGH:   return 0.7f;   // 1.4 / 2.0
        case QualityMode::ULTRA:  return 0.5f;   // 1.0 / 2.0
    }
    return 1.0f;
}

// Adaptive mesh segments based on tile zoom level.
// Higher zoom tiles cover less geographic area → less spherical curvature → fewer segments needed.
// DEM grid is small (e.g. 5×5), so excessive segments just oversample bilinear interpolation.
// Formula: halve segments every 2 zoom levels beyond level 1, floor at max(demMeshN-1, 8).
inline int AdaptiveMeshSegments(int level, int meshSegments, int demMeshN, bool hasDem) {
    if (!hasDem) return meshSegments;
    int shift = std::max(0, level / 2);
    int seg = std::max(meshSegments >> shift, std::max(demMeshN - 1, 8));
    return seg;
}

// Globe engine configuration
struct Config {
    // Tile sources
    std::string tileUrl;              // Base tile URL template ({z}/{x}/{y})
    // Optional HTTP basic auth for raster tiles, format: "user:password".
    // Prefer setting via env var (see main.cpp) to avoid shell history leaks.
    std::string tileAuth;
    std::string vectorTileUrl;        // Vector tile URL (optional)
    std::string demUrl;               // Elevation/DEM URL (optional)
    // Optional HTTP basic auth for DEM endpoint, format: "user:password".
    std::string demAuth;
    
    // Google Earth provider configuration (only used when demProvider="google-earth")
    std::string geElevationEndpoint;                          // Elevation service endpoint (empty = GE disabled)
    std::string geMeshEndpoint;                               // Mesh/NodeData endpoint (Phase 5)
    std::vector<std::pair<std::string, std::string>> geHeaders; // GE-only headers (allowlisted)
    std::string geTokenEnv = "NATIVE_GLOBE_GE_TOKEN";         // Env var for auth token
    int geElevationType = 0;                                  // 0=ELLIPSOID, 1=TERRAIN, 2=SEA_LEVEL
    std::string geEpoch = "latest";                           // Dataset epoch
    std::string geChannel = "default";                        // Service channel
    
    // Phase 5 Sprint 1: RockTree/NodeData mesh (single quadkey mode)
    // Phase 5 Sprint 2: LOD-aware mesh management (geMeshQuadKeys acts as seed set)
    std::vector<std::string> geMeshQuadKeys;                  // Seed quadkeys for mesh loading
    bool geMeshFlipV = true;                                  // Flip V coordinate for texture
    
    // Sprint 2: LOD-aware mesh configuration
    int geMeshMaxLodMargin = 1;                               // Extra LOD levels around visible tiles
    int geMeshMaxInFlight = 8;                                // Max concurrent mesh requests
    double geMeshRequestBudgetMs = 5.0;                       // Per-frame request budget
    int geMeshCacheSize = 64;                                 // Max cached meshes (LRU eviction)
    
    bool geMeshEnabled() const { 
        // Sprint 2: requires endpoint with {quadkey} placeholder
        // geMeshQuadKeys is optional seed set (can be empty for camera-driven loading)
        return !geMeshEndpoint.empty() && 
               geMeshEndpoint.find("{quadkey}") != std::string::npos;
    }
    
    // Cache
    std::string cacheDir = "tile_cache";
    bool useDiskCache = true;
    bool useMemoryCache = true;
    size_t memoryCacheMaxEntries = 2048;
    size_t memoryCacheMaxBytes = 128 * 1024 * 1024; // 128 MB compressed tile bytes
    bool useDecodedMemoryCache = true;
    size_t decodedMemoryCacheMaxEntries = 1024;
    size_t decodedMemoryCacheMaxBytes = 256 * 1024 * 1024; // 256 MB RGBA payload
    
    // Zoom limits
    int minZoom = MIN_ZOOM;
    int maxZoom = MAX_ZOOM;
    
    // Rendering
    int windowWidth = 1280;
    int windowHeight = 720;
    // Create a hidden GLFW window (still creates an OpenGL context). Useful for automated tests.
    bool headless = false;
    float fovDegrees = DEFAULT_FOV_DEG;
    int meshSegments = 64;            // Mesh subdivision per tile (independent of DEM grid)
    
    // LOD
    float sseThreshold = DEFAULT_SSE_THRESHOLD;
    int maxRefinementsPerFrame = 0;   // Progressive LOD smoothness budget (<=0 unlimited)
    
    // Resource limits
    int maxTiles = MAX_TILES_IN_MEMORY;
    int maxConcurrentFetches = MAX_CONCURRENT_FETCHES;
    int maxConcurrentDecodes = MAX_CONCURRENT_DECODES;
    int maxInFlightFetches = MAX_IN_FLIGHT_FETCHES;
    int maxUploadsPerFrame = MAX_TEXTURE_UPLOADS_PER_FRAME;
    double uploadBudgetMs = TEXTURE_UPLOAD_BUDGET_MS;
    double meshUploadBudgetMs = MESH_UPLOAD_BUDGET_MS;
    int maxEvictsPerFrame = MAX_EVICTS_PER_FRAME;
    double evictBudgetMs = EVICT_BUDGET_MS;
    int meshSchedulerWorkers = MESH_SCHEDULER_WORKERS;
    int cancelAfterFramesUntouched = 120;
    bool adaptiveResourceLimits = false;
    
    // Features
    bool demEnabled = true;           // Enable terrain elevation
    bool vectorEnabled = false;
    bool wireframeMode = false;
    bool is2D = false;
    bool logDepthEnabled = true;    // Log-depth precision path (P1.4)
    bool reversedZEnabled = false;  // Reversed-Z precision path (P1.4 alternative)
    bool requestDrivenFrame = true; // Event/dirty-driven frame loop (P2.2)
    bool textureAtlasEnabled = false; // Shared color atlas path (P3.2) - disabled by default for stability
    bool selectiveSkirts = true;    // Enable selective skirt generation per edge mask
    bool edgeStitching = true;      // Enable stitch-mask aware mesh template variants
    bool lodChildQuorum = true;     // Refine only when all children are render-ready
    int textureAtlasSize = 4096;     // Atlas page resolution in pixels
    int textureAtlasSlotSize = 256;  // Fixed tile slot resolution in pixels
    int atlasGutterPx = 2;           // Per-slot gutter padding to prevent atlas seam bleed
    bool atlasEdgeDilate = true;     // Dilate border texels into gutter on upload
    bool textureAtlasMipmaps = false; // Optional atlas mip generation (costly per upload)
    
    // DEM/Terrain settings
    // Default: MapTiler Terrain-RGB v2 tiles (Mapbox Terrain-RGB encoding).
    std::string demBaseUrl = "https://api.maptiler.com/tiles/terrain-rgb-v2/{z}/{x}/{y}.png?key=YGPXGCyXf6kh5TO9dJ7l";
    // DEM Provider: terrain-rgb (default, public) | google-earth (internal, requires auth)
    std::string demProvider = "terrain-rgb";
    int demMaxZoom = 15;               // Clamp DEM requests above provider max zoom
    int demMeshN = 17;                // Grid resolution per tile (17x17 = 289 samples, GE parity)
                                      // Old: 5x5 = 25 samples (blocky terrain)
                                      // New: 17x17 = 289 samples (smooth terrain)
    size_t demCacheSize = 512;        // Max cached DEM tiles
    int demVisiblePinBudget = 1024;   // Max visible/neighbor DEM keys pinned against eviction
    // Height scale architecture: separated base scale and exaggeration for GE parity
    double demHeightScaleBase = 1.0;       // Base scale: Terrain-RGB → meters (true elevation)
    double demExaggerationFactor = 2.5;    // Visual exaggeration for rendering (2.5x)
    
    // DEPRECATED: Use demHeightScaleBase * demExaggerationFactor instead
    double demHeightScale = 2.5;           // Legacy combined value for backward compatibility
    bool demRasterCoEviction = true;  // Evict DEM cache entries when matching raster tile is evicted
    int demEdgeBlendSegments = 2;     // Edge coherence blend band (in vertex rings). 0 disables blending.
    bool demDebug = false;            // Enable DEM debug logging
    DisplacementMode terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;  // Single authority
    float skirtDepthNearKm = 0.03f;   // Near-view skirt depth (km, ~30 m)
    float skirtDepthFarKm = 0.15f;    // Far-view skirt depth (km, ~150 m)
    float skirtDepthRatio = 0.003f;   // Relative skirt depth vs tile arc length
    float skirtMinDepthKm = 0.05f;    // Minimum skirt depth (km)
    float skirtMaxDepthKm = 0.4f;     // Maximum skirt depth (km)
    
    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
    
    // Debug culling toggles (for gap diagnosis)
    bool disableFrustumCull = false;   // Skip frustum culling in LOD selection
    bool disableHorizonCull = false;   // Skip horizon culling in LOD selection
    
    // Quality mode (GE parity: 1.0/2.0/4.0 multipliers)
    QualityMode qualityMode = QualityMode::MEDIUM;  // Default: GE standard quality
};

} // namespace globe
