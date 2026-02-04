#pragma once

#include "constants.h"
#include <string>

namespace globe {

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
    int meshSegments = 4;             // Mesh subdivision per tile
    
    // LOD
    float sseThreshold = DEFAULT_SSE_THRESHOLD;
    
    // Resource limits
    int maxTiles = MAX_TILES_IN_MEMORY;
    int maxConcurrentFetches = MAX_CONCURRENT_FETCHES;
    int maxConcurrentDecodes = MAX_CONCURRENT_DECODES;
    int maxUploadsPerFrame = MAX_TEXTURE_UPLOADS_PER_FRAME;
    double uploadBudgetMs = TEXTURE_UPLOAD_BUDGET_MS;
    
    // Features
    bool demEnabled = true;           // Enable terrain elevation
    bool vectorEnabled = false;
    bool wireframeMode = false;
    bool is2D = false;
    
    // DEM/Terrain settings (PiriReis mesh service)
    std::string demBaseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
    int demMeshN = 5;                 // Grid resolution per tile (5x5)
    size_t demCacheSize = 256;        // Max cached DEM tiles
    double demHeightScale = 1.0;      // Height exaggeration (1.0 = realistic)
    bool demDebug = false;            // Enable DEM debug logging
    
    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
};

} // namespace globe
