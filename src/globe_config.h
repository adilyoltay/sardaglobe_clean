#pragma once

#include <string>
#include <vector>
#include <cmath>

// Layer type for different tile sources
enum class RasterLayerType {
  XYZ,   // Standard XYZ tile server
  TMS,   // TMS (inverted Y)
  WMS    // OGC WMS
};

// WMS-specific configuration
struct WMSConfig {
  std::string layers;           // WMS LAYERS parameter
  std::string styles;           // WMS STYLES parameter
  std::string format = "image/png";  // WMS FORMAT
  std::string version = "1.1.1";     // WMS VERSION
  std::string srs = "EPSG:3857";     // WMS SRS/CRS
  bool transparent = true;
  int width = 256;
  int height = 256;
};

// Raster layer configuration for multi-layer support
struct RasterLayerConfig {
  std::string id;
  std::string name;
  std::string url;
  std::string supportUrl;  // Fallback URL for retries
  bool supportTransparentPixel = false;
  bool supportEmptyContent = false;
  bool supportOutOfBBOX = false;
  RasterLayerType type = RasterLayerType::XYZ;
  WMSConfig wms;           // WMS-specific settings (only used if type == WMS)
  float opacity = 1.0f;
  glm::vec4 color = glm::vec4(1.0f); // Color multiplier (JS parity: FColor)
  bool visible = true;
  int minZoom = 0;
  int maxZoom = 22;        // JS Parity: Per-layer max LOD (raster.maxLodLevel)
  int zIndex = 0;          // Draw order (lower = drawn first)
  
  // Cesium-style overlay options
  int tileWidth = 256;            // Tile image width in pixels
  int tileHeight = 256;           // Tile image height in pixels
  float maximumScreenSpaceError = 2.0f;  // SSE threshold for overlay detail
  bool usePlaceholder = true;     // Use placeholder while loading
};

enum class MeshType {
  // JS parity: CSMeshTypes -> WGS84=1, XYZ_MERCATOR=2
  WGS84 = 1,
  XYZ_MERCATOR = 2,
};

enum class AltitudeMode {
  CLAMP_TO_GROUND,    // 0: Follow terrain surface
  RELATIVE_TO_GROUND, // 1: Height above terrain
  ABSOLUTE            // 2: Height above WGS84 ellipsoid
};

// JS Sa table: LOD → Altitude (meters) lookup
// Used by api_SetMinNavigationLOD, api_SetMaxNavigationLOD, etc.
inline double GetAltitudeFromLOD(int lod) {
  static const double SA_TABLE[] = {
    25512546.06,  // LOD 0
    25512546.06,  // LOD 1
    25512546.06,  // LOD 2
    25512546.06,  // LOD 3
    12408290.88,  // LOD 4
    5837640.93,   // LOD 5
    2770920.56,   // LOD 6
    1379009.96,   // LOD 7
    678769.05,    // LOD 8
    336039.9,     // LOD 9
    171201.96,    // LOD 10
    86927.14,     // LOD 11
    43121.77,     // LOD 12
    21526.8,      // LOD 13
    10611.02,     // LOD 14
    5386.31,      // LOD 15
    2688.9,       // LOD 16
    1352.01,      // LOD 17
    672.8,        // LOD 18
    335.87,       // LOD 19
    169.95,       // LOD 20
    84.84,        // LOD 21
    39.97,        // LOD 22
    39.97,        // LOD 23
    39.97,        // LOD 24
    39.97         // LOD 25
  };
  if (lod < 0) lod = 0;
  if (lod > 25) lod = 25;
  return SA_TABLE[lod];
}

// JS: FindAltitudeFromLOD with interpolation
inline double FindAltitudeFromLOD(double lod) {
  int lodLow = static_cast<int>(std::floor(lod));
  int lodHigh = static_cast<int>(std::ceil(lod));
  if (lodLow == lodHigh) return GetAltitudeFromLOD(lodLow);
  double altLow = GetAltitudeFromLOD(lodLow);
  double altHigh = GetAltitudeFromLOD(lodHigh);
  return altLow + (lod - lodLow) * (altHigh - altLow);
}

struct GlobeConfig {
  std::string tileUrl;
  std::string vectorTileUrl;
  std::string vectorLayerName;
  std::string cacheDir = "tile_cache";
  bool useDiskCache = true;
  float sseThresholdPx = 1.4f;  // main.js uses ~350px threshold (256*1.4=358)
  float fadeDuration = 0.25f;
  bool fadeEase = true;
  int minZoom = 2;
  int maxZoom = 22;
  int baseRasterMinZoom = -1;  // Optional base raster min LOD override (-1 = use minZoom)
  int baseRasterMaxZoom = -1;  // Optional base raster max LOD override (-1 = use maxZoom)
  int fixedZoom = 2;
  bool useFixedZoom = false;
  int segments = 4; // JS parity: MESHN=5 (4 segments)
  int tileRadius = 2;
  int windowWidth = 1280;
  int windowHeight = 720;
  std::vector<RasterLayerConfig> rasterLayers;  // Multiple raster layers

  // Mesh/DEM (terrain) sampling - JS parity
  MeshType meshType = MeshType::WGS84;
  int wgs84MaxLOD = 15;  // Safe default to prevent index explosion
  std::string meshUrl;  // Optional override for demBaseUrl
  std::vector<std::string> meshUrls;  // Optional list for round-robin
  int meshTileZoom = -1;  // For XYZ_MERCATOR, -1 = use current zoom
  size_t meshCacheSize = 1000;
  bool meshRetryAtTimeout = true;
  bool meshContinueDivision = false;
  bool demDebug = false; // Toggle DEM debug logging

  // DEM (terrain) sampling - PiriReis mesh service
  bool demEnabled = true;
  std::string demBaseUrl =
      "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
  int demMeshN = 5;   // FAZ 2: JS parity - MESHN=5 (satır 45242: u = 5)
  double demTileSpanLonDeg = 5.625;
  double demTileSpanLatDeg = 3.83346369892724;
  bool demRowsNorthToSouth = true;
  size_t demCacheSize = 8;
  int demBatchGrid = 2;  // 1 = single cell, 2 = 2x2 (CN=4), etc.
  int demBatchMaxCount = 10;  // JS parity: MAX_MESH_REQUEST (CN upper bound)
  double demBatchMaxWaitSec = 0.05;  // Flush batch after this wait (seconds)

  // Cesium-style cache and culling options
  size_t maximumCachedBytes = 512 * 1024 * 1024;  // 512MB byte-based cache
  double tileCacheUnloadTimeLimitMs = 5.0;        // Max ms per frame for unloading
  uint32_t loadingDescendantLimit = 20;           // Max loading descendants before render ancestor
  bool enableFogCulling = true;                   // Cull tiles in fog
  bool enableCulledSSE = true;                    // Refine culled tiles to culledSSEThreshold
  float culledSSEThreshold = 64.0f;               // SSE threshold for culled tiles
  float fogDensityBase = 2.0e-5f;                 // Base fog density at low altitude
  bool enableLodTransition = true;                // Smooth LOD transitions
  float lodTransitionDurationSec = 0.5f;          // Fade duration between LODs
  

  
  // Phase 3: Hybrid LOD (SSE vs Distance Table)
  bool useLegacyDistanceLOD = false;              // Use table-based LOD selection
};
