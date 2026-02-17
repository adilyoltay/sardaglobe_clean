#pragma once

#include "constants.h"
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <cmath>

namespace globe {

// Terrain displacement authority
enum class DisplacementMode {
    CPU_MESH_BAKE        // DEM baked into mesh vertices (single authority)
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
    std::string geElevationEndpoint = "https://kh.google.com/rt/earth/Elevation/pb=!1m2!1s{path}!2u{epoch}"; // Default GE elevation endpoint (Google Earth Pro style)
    std::string geElevationPath = "Elevation";                // Override {path} placeholder in elevation URL
    std::string geMeshEndpoint = "https://kh.google.com/rt/earth/NodeData/pb=!1m2!1s{quadkey}!2u{epoch}!2e1!3u1031!4b0"; // Default GE NodeData endpoint (dynamic epoch)
    std::vector<std::pair<std::string, std::string>> geHeaders; // GE-only headers (allowlisted)
    std::string geTokenEnv = "NATIVE_GLOBE_GE_TOKEN";         // Env var for auth token
    int geElevationType = 1;                                  // 0=ELLIPSOID, 1=TERRAIN (GE Default), 2=SEA_LEVEL
    std::string geEpoch = "";                                  // Dataset epoch (auto-detected from PlanetoidMetadata)
    bool geEpochAutoDetect = true;                            // Auto-fetch epoch from PlanetoidMetadata
    std::string geChannel = "default";                        // Service channel
    
    // Phase 5 Sprint 1: RockTree/NodeData mesh (single quadkey mode)
    // Phase 5 Sprint 2: LOD-aware mesh management (geMeshQuadKeys acts as seed set)
    // Default seeds: San Francisco (0213), New York (0320), London (0132), Tokyo (1230)
    std::vector<std::string> geMeshQuadKeys = {
        "0213",   // San Francisco, CA
        "0212",   // San Francisco Bay Area
        "0320",   // New York, NY
        "0321",   // New York Metro
        "0132",   // London, UK
        "0133",   // London Metro
        "1230",   // Tokyo, Japan
        "1231",   // Tokyo Metro
        "0302",   // Chicago, IL
        "0310",   // Washington DC
        "1201",   // Beijing, China
        "1220",   // Seoul, Korea
    };  // Seed quadkeys for mesh loading
    bool geMeshFlipV = true;                                  // Flip V coordinate for texture
    
    // Sprint 2: LOD-aware mesh configuration
    int geMeshMaxLodMargin = 1;                               // Extra LOD levels around visible tiles
    int geMeshMaxInFlight = 8;                                // Max concurrent mesh requests
    double geMeshRequestBudgetMs = 5.0;                       // Per-frame request budget
    int geMeshCacheSize = 64;                                 // Max cached meshes (LRU eviction)
    
    // Sprint 2.3: Child-LOD proximity selection
    bool geMeshEnableChildLod = true;                         // Enable child LOD for close tiles
    float geMeshChildLodDistance = 5000.0f;                   // Distance threshold for child LOD (meters)
    int geMeshMaxChildRequestsPerFrame = 2;                   // Max child requests per frame
    
    // Sprint 3: HTTP/2 transport configuration
    bool geMeshEnableHttp2 = true;                            // Prefer HTTP/2
    bool geMeshAllowHttp1Fallback = true;                     // Allow HTTP/1.1 fallback
    long geMeshTcpKeepAliveSec = 30;                          // TCP keep-alive interval
    long geMeshTcpKeepAliveIdleSec = 15;                      // TCP keep-alive idle time
    bool geMeshEnableConnectionReuse = true;                  // Enable connection reuse

    // Octree discovery configuration
    std::string geBulkMetadataEndpoint = "https://kh.google.com/rt/earth/BulkMetadata/pb=!1m2!1s{path}!2u{epoch}";
    std::string gePlanetoidMetadataUrl = "https://kh.google.com/rt/earth/PlanetoidMetadata";
    int geRateLimitMs = 250;                                  // Min interval between GE requests (ms)
    bool geOctreeEnabled = true;                              // Enable octree discovery (vs legacy quadkey)
    int geBulkMetadataMaxPending = 4;                         // Max concurrent BulkMetadata fetches
    
    bool geMeshEnabled() const { 
        // Sprint 2: requires endpoint with {quadkey} placeholder
        // geMeshQuadKeys is optional seed set (can be empty for camera-driven loading)
        return !geMeshEndpoint.empty() && 
               geMeshEndpoint.find("{quadkey}") != std::string::npos;
    }
    
    // RockMesh (NodeData) vertex explosion mitigation (P0-P2)
    bool rockMeshRenderEnabled = true;              // Master kill-switch for RockMesh
    bool rockMeshSanityEnabled = true;              // Enable validation gates
    float rockMeshMaxBboxDiagonalKm = 100.0f;       // AABB discard threshold (conservative start)
    float rockMeshMaxVertexDistanceFromOriginKm = 300.0f;  // Vertex distance sanity check
    bool rockMeshFallbackMagenta = false;           // Debug: magenta fallback for invalid meshes
    uint8_t rockMeshFallbackR = 128;                // Fallback color R (default gray)
    uint8_t rockMeshFallbackG = 128;                // Fallback color G
    uint8_t rockMeshFallbackB = 128;                // Fallback color B
    
    // Cache
    std::string cacheDir = "tile_cache";
    bool useDiskCache = true;
    bool useMemoryCache = true;
    bool autoTuneMemoryCache = true;                       // Auto-size memory caches from system RAM
    size_t memoryCacheMaxEntries = 16384;                   // 8x - Cache thrashing önlemi
    size_t memoryCacheMaxBytes = 1024 * 1024 * 1024;        // 1 GB (8x) compressed tile bytes
    bool useDecodedMemoryCache = true;
    size_t decodedMemoryCacheMaxEntries = 4096;             // 4x artırıldı
    size_t decodedMemoryCacheMaxBytes = 1024 * 1024 * 1024; // 1 GB (4x) RGBA payload
    
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
    bool demEnabled = true;           // Enable terrain by default (uses GE elevation API)
    bool vectorEnabled = false;
    bool wireframeMode = false;
    bool is2D = false;
    bool logDepthEnabled = true;    // Log-depth precision path (P1.4)
    bool reversedZEnabled = false;  // Reversed-Z precision path (P1.4 alternative)
    bool useRteRender = true;       // RTE/RTC relative-to-center rendering (jitter fix)
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
    
    // Faz 2A: PBO async texture upload (renamed for consistency: usePboUploads)
    bool usePboUploads = true;      // Enable async PBO texture upload (reduces stutter)
    int pboUploadCount = 8;         // Number of PBOs in ring buffer
    size_t pboUploadSize = 4 * 1024 * 1024; // Default PBO buffer size (4MB)
    
    // Faz 2B: Texture2DArray (layer-based texture storage)
    bool useTexture2DArray = false; // Enable Texture2DArray (prevents bleeding, default false for safe rollout)
    
    // Faz 3: Performance optimizations
    // Horizon Culling
    bool useHorizonCulling = true;           // Enable horizon culling (default true)
    double horizonCullingSafetyMargin = 0.01; // Safety margin in radians (0.01 ~ 0.57 degrees)
    bool horizonCullingDebug = false;        // Visualize culled tiles for debugging
    
    // Weighted Scheduler
    bool useWeightedScheduler = true;        // Enable weighted tile scheduling
    float schedulerAgingHalfLifeMs = 5000.0f; // Aging half-life in milliseconds
    bool schedulerUseAging = true;           // Enable aging factor in priority
    
    // P4: Weighted scheduler tuning parameters
    float schedulerSseWeight = 1.0f;                    // SSE term weight (default: 1.0)
    float schedulerCenterBiasWeight = 0.30f;            // Center bias weight (default: 0.3)
    float schedulerDistanceWeight = 0.0f;               // Distance term weight (default: 0.0)
    float schedulerLodWeight = 0.0f;                    // LOD level weight (default: 0.0)
    float schedulerAgingWeight = 1.0f;                  // Aging multiplier (default: 1.0)
    float schedulerDirectionalPredictiveWeight = 0.5f;  // Directional predictive weight (default: 0.5)
    
    // Adaptive LOD
    bool useAdaptiveLod = true;              // Enable adaptive LOD based on terrain variance
    float lodVarianceThreshold = 100.0f;     // Height variance threshold for LOD adjustment
    int lodHysteresisFrames = 3;             // Frames to wait before LOD change
    
    // P2: Distance-based terrain morph (replaces time-based for smoother GE-style transitions)
    bool useDistanceBasedTerrainMorph = true;        // Enable distance-based morph (default: true)
    float terrainMorphDistanceRangeKm = 0.2f;        // Morph band width in km (default: 200m)
    bool enableTerrainMorphTimeFallback = true;      // Allow time-based fallback if distance invalid
    
    // DEM/Terrain settings
    // Default: Public AWS Terrarium tile template (no API key required)
    std::string demBaseUrl = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
    // DEM encoding for terrain-rgb provider: auto | mapbox | terrarium
    std::string demEncoding = "auto";
    std::string demApiKey;                                    // Added via --dem-api-key / env var
    std::string demApiKeyEnv = "NATIVE_GLOBE_DEM_TOKEN";      // Env var for DEM API key
    // DEM Provider: terrain-rgb (default, public) | google-earth (internal, requires auth)
    std::string demProvider = "google-earth";  // Default: Google Earth elevation API (may require browser context)
    int demMaxZoom = 22;               // Requested DEM max zoom (provider cap applied at runtime)
    int demProviderEffectiveMaxZoom = 15; // Runtime effective DEM max zoom after provider cap
    int demMaxCoarseningDeltaLod = 2;  // Max DEM coarsening below tile LOD (seam stability guardrail)
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
    // DEM mesh safety clamp (km). Prevents NaN/extreme input from collapsing vertices.
    // 12 km bounds are Earth-elevation-safe for terrain-rgb and GE sources.
    float demHeightMinKm = -12.0f;
    float demHeightMaxKm = 12.0f;
    // Evidence-trap mode: abort on invalid vertex in demDebug, or auto-recover when false.
    bool demDebugAbortOnInvalidVertex = false;
    DisplacementMode terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;  // Single authority
    float skirtDepthNearKm = 0.03f;   // Near-view skirt depth (km, ~30 m)
    float skirtDepthFarKm = 0.15f;    // Far-view skirt depth (km, ~150 m)
    float skirtDepthRatio = 0.003f;   // Relative skirt depth vs tile arc length
    float skirtMinDepthKm = 0.05f;    // Minimum skirt depth (km)
    float skirtMaxDepthKm = 0.4f;     // Maximum skirt depth (km)
    
    // DEM no-data / terrain sanitization
    float demNoDataMinHeightM = -11000.0f;    // Heights below this are treated as no-data
    float demNoDataReplacementM = 0.0f;       // Replacement height for no-data samples
    bool forceClampTerrainNoData = true;       // Enable no-data clamping in DEM decode

    // Fallback / parent retention
    bool fallbackRequireParentUntilChildrenReady = true; // Keep parent visible until all children ready

    // Debug
    bool showDebugInfo = true;
    bool logNetwork = false;
    std::string smokeScene = "default";  // Smoke scene preset (default | aegean)

    // Debug culling toggles (for gap diagnosis)
    bool disableFrustumCull = false;   // Skip frustum culling in LOD selection
    bool disableHorizonCull = false;   // Skip horizon culling in LOD selection
    
    // Quality mode (GE parity: 1.0/2.0/4.0 multipliers)
    QualityMode qualityMode = QualityMode::MEDIUM;  // Default: GE standard quality
    
    // P1-4: Config validasyonu - çakışan ayarları düzelt
    void Validate() {
        // LogDepth ve Reversed-Z aynı anda aktif olamaz!
        if (reversedZEnabled && logDepthEnabled) {
            logDepthEnabled = false;
            // Log mesajı uygulama başlangıcında yazılacak
        }
        
        // P4: Scheduler weight validasyonu
        auto ClampNonNegative = [](float& val) {
            if (!std::isfinite(val) || val < 0.0f) val = 0.0f;
        };
        ClampNonNegative(schedulerSseWeight);
        ClampNonNegative(schedulerCenterBiasWeight);
        ClampNonNegative(schedulerDistanceWeight);
        ClampNonNegative(schedulerLodWeight);
        ClampNonNegative(schedulerAgingWeight);
        ClampNonNegative(schedulerDirectionalPredictiveWeight);
        
        // Aging half-life sınırları
        if (!std::isfinite(schedulerAgingHalfLifeMs) || schedulerAgingHalfLifeMs <= 0.0f) {
            schedulerAgingHalfLifeMs = 5000.0f;  // Default fallback
        }
        
        // RockMesh sanity validasyonu
        if (!std::isfinite(rockMeshMaxBboxDiagonalKm) || rockMeshMaxBboxDiagonalKm <= 0.0f) {
            rockMeshMaxBboxDiagonalKm = 100.0f;  // Conservative default
        }
        if (!std::isfinite(rockMeshMaxVertexDistanceFromOriginKm) || rockMeshMaxVertexDistanceFromOriginKm <= 0.0f) {
            rockMeshMaxVertexDistanceFromOriginKm = 300.0f;
        }

        demMaxZoom = std::clamp(demMaxZoom, 0, 22);
        demProviderEffectiveMaxZoom = std::clamp(demProviderEffectiveMaxZoom, 0, 22);
        demMaxCoarseningDeltaLod = std::clamp(demMaxCoarseningDeltaLod, 0, 22);
        if (!std::isfinite(demHeightMinKm)) demHeightMinKm = -12.0f;
        if (!std::isfinite(demHeightMaxKm)) demHeightMaxKm = 12.0f;
        if (demHeightMinKm > demHeightMaxKm) std::swap(demHeightMinKm, demHeightMaxKm);
    }
};

} // namespace globe
