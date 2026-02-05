#pragma once

#include "constants.h"
#include <string>

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
    
    // Zoom limits
    int minZoom = MIN_ZOOM;
    int maxZoom = MAX_ZOOM;
    
    // Rendering
    int windowWidth = 1280;
    int windowHeight = 720;
    float fovDegrees = DEFAULT_FOV_DEG;
    int meshSegments = 64;            // Mesh subdivision per tile (matches DEM grid)
    
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
    
    // DEM/Terrain settings (PiriReis mesh service)
    std::string demBaseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
    int demMeshN = 65;                // Grid resolution per tile (65x65 = 4225 samples)
    size_t demCacheSize = 512;        // Max cached DEM tiles
    double demHeightScale = 2.5;      // Height exaggeration (2.5x for visible terrain)
    bool demDebug = false;            // Enable DEM debug logging
    DisplacementMode terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;  // Single authority
    
    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
    
    // Debug culling toggles (for gap diagnosis)
    bool disableFrustumCull = false;   // Skip frustum culling in LOD selection
    bool disableHorizonCull = false;   // Skip horizon culling in LOD selection
};

} // namespace globe
