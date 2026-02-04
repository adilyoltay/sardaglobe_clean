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
    bool demEnabled = false;
    bool vectorEnabled = false;
    bool wireframeMode = false;
    bool is2D = false;
    
    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
};

} // namespace globe
