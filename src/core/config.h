#pragma once

#include "constants.h"
#include <string>
#include <cstddef>

namespace globe {

// Terrain displacement authority
enum class DisplacementMode {
    CPU_MESH_BAKE,        // DEM baked into mesh vertices (default, proven)
    GPU_HEIGHTMAP_DISPLACE // Flat mesh + GPU vertex shader displacement
};

// Globe engine configuration
struct Config {
    // Tile sources
    std::string tileUrl;              // Base tile URL template ({z}/{x}/{y})
    std::string vectorTileUrl;        // Vector tile URL (optional)
    std::string demUrl;               // Elevation/DEM URL (optional)
    
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
    float fovDegrees = DEFAULT_FOV_DEG;
    int meshSegments = 64;            // Mesh subdivision per tile (independent of DEM grid)
    
    // LOD
    float sseThreshold = DEFAULT_SSE_THRESHOLD;
    
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
    
    // DEM/Terrain settings (PiriReis mesh service)
    std::string demBaseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
    int demMeshN = 5;                 // Grid resolution per tile (5x5 = 25 samples, webglobe parity)
    size_t demCacheSize = 512;        // Max cached DEM tiles
    int demVisiblePinBudget = 1024;   // Max visible/neighbor DEM keys pinned against eviction
    double demHeightScale = 2.5;      // Height exaggeration (2.5x for visible terrain)
    bool demDebug = false;            // Enable DEM debug logging
    DisplacementMode terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;  // Single authority
    float skirtDepthNearKm = 0.015f;  // Near-view skirt depth (km, ~15 m)
    float skirtDepthFarKm = 0.10f;    // Far-view skirt depth (km, ~100 m)
    float skirtDepthRatio = 0.003f;   // Relative skirt depth vs tile arc length
    float skirtMinDepthKm = 0.05f;    // Minimum skirt depth (km)
    float skirtMaxDepthKm = 0.4f;     // Maximum skirt depth (km)
    
    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
    
    // Debug culling toggles (for gap diagnosis)
    bool disableFrustumCull = false;   // Skip frustum culling in LOD selection
    bool disableHorizonCull = false;   // Skip horizon culling in LOD selection
};

} // namespace globe
