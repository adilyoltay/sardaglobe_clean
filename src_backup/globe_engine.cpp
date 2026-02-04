// ============================================================================
// GLOBE ENGINE - Main Implementation
// ============================================================================
// This file is 11,500+ lines and needs refactoring.
// See docs/GLOBE_ENGINE_REFACTORING_PLAN.md for the refactoring roadmap.
//
// FILE STRUCTURE (approximate line numbers):
// - [0000-0200]    Includes and forward declarations
// - [0200-1800]    Helper functions (URL, coordinates, tile math)
// - [1800-2000]    Texture creation (see also texture_manager.h)
// - [2000-3600]    Tile synchronization (SyncRasterTiles, etc.)
// - [3600-4500]    Scheduler integration (ITileFetcher, ITileDecoder)
// - [4500-5500]    Impl struct definition
// - [5500-7500]    DEM/Terrain handling
// - [7500-8500]    Download workers
// - [8500-9500]    Rendering (RenderTiles, RenderVectors, etc.)
// - [9500-11500]   Public API, tests, main loop
//
// MUTEX LOCK HIERARCHY (to prevent deadlocks):
// configMutex (L1) → downloadMutex (L2) → pendingMutex (L3) → cancelMutex (L4)
// ============================================================================

#include "globe_engine.h"
#include "layer_manager.h"
#include "earth_camera.h"
#include "flight_controller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <optional>

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#include <curl/curl.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <vtzero/vector_tile.hpp>
#include <vtzero/geometry.hpp>
#include <mapbox/earcut.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "tile.h"
#include "tile_scheduler.h"
#include "tile_mesh_builder.h"
#include "tile_lod_selector.h"
#include "tile_math.h"
#include "download_types.h"
#include "tile_fetcher.h"
#include "label_manager.h"
#include "icon_map.h"
#include "json_parser.h"
#include "flight_controller.h"

namespace {
using HeightSampler = std::function<bool(double lonDeg, double latDeg, int level, double& heightMeters)>;

static void GlfwErrorCallback(int error, const char* description) {
  std::cerr << "GLFW error " << error << ": " << (description ? description : "(null)") << std::endl;
}

static void LogOpenGLInfo() {
  const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  std::cerr << "OpenGL Vendor: " << (vendor ? vendor : "unknown") << std::endl;
  std::cerr << "OpenGL Renderer: " << (renderer ? renderer : "unknown") << std::endl;
  std::cerr << "OpenGL Version: " << (version ? version : "unknown") << std::endl;
  std::cerr << "GLSL Version: " << (glsl ? glsl : "unknown") << std::endl;
}

static bool CheckOpenGLVersion(int requiredMajor, int requiredMinor) {
  int major = 0;
  int minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  if (major == 0) {
    const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (versionStr) {
      std::sscanf(versionStr, "%d.%d", &major, &minor);
    }
  }
  if (major < requiredMajor || (major == requiredMajor && minor < requiredMinor)) {
    std::cerr << "OpenGL " << requiredMajor << "." << requiredMinor
              << " required, found " << major << "." << minor << std::endl;
    return false;
  }
  return true;
}










struct VectorTile {
  int x = 0;
  int y = 0;
  int z = 0;
  glm::vec3 center = {};
  float angularRadius = 0.0f;
  float radius = 0.0f;
  GLuint lineVao = 0;
  GLuint lineVbo = 0;
  GLsizei lineVertexCount = 0;
  GLuint pointVao = 0;
  GLuint pointVbo = 0;
  GLsizei pointVertexCount = 0;
  GLuint fillVao = 0;
  GLuint fillVbo = 0;
  GLsizei fillVertexCount = 0;
  Tile schedulerTile; // For TileScheduler integration
};

// DownloadPriority, DownloadJob, DownloadResult moved to download_types.h

// Maximum age for pending downloads before they are considered stale (seconds)
constexpr double MAX_PENDING_AGE_SECONDS = 60.0;
// DEM mesh rebuild throttle (seconds)
constexpr double kDemMeshRecheckIntervalSec = 0.25;

// WebGL State Tracker (Google Earth style - minimize redundant GL calls)
struct GLStateTracker {
  GLuint boundProgram = 0;
  GLuint boundVao = 0;
  GLuint boundTextures[8] = {0};
  int activeTextureUnit = 0;
  bool depthTestEnabled = true;
  bool blendEnabled = false;
  bool cullFaceEnabled = true;
  GLenum blendSrcFactor = GL_ONE;
  GLenum blendDstFactor = GL_ZERO;
  
  void UseProgram(GLuint program) {
    if (boundProgram != program) {
      glUseProgram(program);
      boundProgram = program;
    }
  }
  
  void BindVAO(GLuint vao) {
    if (boundVao != vao) {
      glBindVertexArray(vao);
      boundVao = vao;
    }
  }
  
  void BindTexture(int unit, GLuint texture) {
    if (activeTextureUnit != unit) {
      glActiveTexture(GL_TEXTURE0 + unit);
      activeTextureUnit = unit;
    }
    if (boundTextures[unit] != texture) {
      glBindTexture(GL_TEXTURE_2D, texture);
      boundTextures[unit] = texture;
    }
  }
  
  void SetDepthTest(bool enabled) {
    if (depthTestEnabled != enabled) {
      if (enabled) glEnable(GL_DEPTH_TEST);
      else glDisable(GL_DEPTH_TEST);
      depthTestEnabled = enabled;
    }
  }
  
  void SetBlend(bool enabled) {
    if (blendEnabled != enabled) {
      if (enabled) glEnable(GL_BLEND);
      else glDisable(GL_BLEND);
      blendEnabled = enabled;
    }
  }
  
  void SetBlendFunc(GLenum src, GLenum dst) {
    if (blendSrcFactor != src || blendDstFactor != dst) {
      glBlendFunc(src, dst);
      blendSrcFactor = src;
      blendDstFactor = dst;
    }
  }
  
  void SetCullFace(bool enabled) {
    if (cullFaceEnabled != enabled) {
      if (enabled) glEnable(GL_CULL_FACE);
      else glDisable(GL_CULL_FACE);
      cullFaceEnabled = enabled;
    }
  }
  
  void Reset() {
    boundProgram = 0;
    boundVao = 0;
    for (int i = 0; i < 8; ++i) boundTextures[i] = 0;
    
    // Sync real GL state
    glActiveTexture(GL_TEXTURE0);
    activeTextureUnit = 0;
    
    depthTestEnabled = true;
    blendEnabled = false;
    cullFaceEnabled = true;
  }
};

// Check if image data represents an empty/transparent tile
// Only checks for fully transparent tiles - solid color tiles are valid imagery
// (e.g., ocean tiles, ice sheets, low-contrast areas)
// Returns: 0 = valid image, 1 = empty/transparent tile, -1 = decode failure
int CheckTileImage(const std::vector<unsigned char>& data, int& width, int& height) {
  if (data.empty()) return -1;  // No data = decode failure
  
  int channels = 0;
  unsigned char* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                                 &width, &height, &channels, 4);
  if (!pixels) return -1;  // Decode failure - NOT empty tile
  
  // Only check for fully transparent tiles (alpha = 0)
  // Do NOT treat solid-color tiles as empty - they are valid imagery
  bool allTransparent = true;
  constexpr unsigned char kAlphaVisibleThreshold = 10;
  
  for (int i = 0; i < width * height; ++i) {
    int idx = i * 4;
    if (pixels[idx + 3] > kAlphaVisibleThreshold) {  // Alpha > threshold means visible pixel
      allTransparent = false;
      break;
    }
  }
  
  stbi_image_free(pixels);
  return allTransparent ? 1 : 0;
}

// Legacy wrapper for backward compatibility
bool IsEmptyTileImage(const std::vector<unsigned char>& data, int& width, int& height) {
  return CheckTileImage(data, width, height) != 0;  // true if empty OR decode failure
}

bool DecodeImageRGBA(const std::vector<unsigned char>& data,
                     std::vector<unsigned char>& outPixels,
                     int& width,
                     int& height) {
  outPixels.clear();
  width = 0;
  height = 0;
  if (data.empty()) return false;
  int channels = 0;
  // stbi_set_flip handled globally in Constructor
  unsigned char* pixels = stbi_load_from_memory(data.data(),
                                                static_cast<int>(data.size()),
                                                &width, &height, &channels, 4);
  if (!pixels) return false;
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  outPixels.assign(pixels, pixels + count);
  stbi_image_free(pixels);
  return true;
}

void AnalyzeAlpha(const std::vector<unsigned char>& pixels,
                  bool& anyTransparent,
                  bool& allTransparent) {
  constexpr unsigned char kAlphaVisibleThreshold = 10;
  anyTransparent = false;
  allTransparent = true;
  if (pixels.empty()) {
    anyTransparent = true;
    return;
  }
  for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
    const unsigned char alpha = pixels[i + 3];
    if (alpha > kAlphaVisibleThreshold) {
      allTransparent = false;
    } else {
      anyTransparent = true;
    }
  }
  if (allTransparent) {
    anyTransparent = true;
  }
}

// Priority queue comparator for download jobs
// Google Earth style: Priority level first, then priorityScore (higher = more important)
struct DownloadJobComparator {
  bool operator()(const DownloadJob& a, const DownloadJob& b) const {
    // Lower priority value = higher priority (URGENT=0 > LOW=3)
    if (a.priority != b.priority) return a.priority > b.priority;
    
    // Same priority level: use priorityScore (higher score = higher priority)
    // Priority queue pops MAX, so higher score should return false (a < b)
    if (std::abs(a.priorityScore - b.priorityScore) > 0.001f) {
      return a.priorityScore < b.priorityScore;  // Higher score wins
    }
    
    // Tie-break: Prefer lower Z (parents/ancestors load first for fallback)
    if (a.z != b.z) return a.z > b.z;
    
    // Same zoom: FIFO order
    return a.queueTime > b.queueTime;
  }
};

struct Plane {
  glm::vec3 normal{};
  float d = 0.0f;
};

struct ResolvedTexture {
  GLuint texture = 0;
  glm::vec2 uvOffset = glm::vec2(0.0f);
  glm::vec2 uvScale = glm::vec2(1.0f);
  bool valid = false;
};

struct RowMat3 {
  double m[3][3] = {
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0}
  };
};

struct JsEuler {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  int order = 0;
};

inline glm::dvec3 JsToWorld(const glm::dvec3& v) {
  return glm::dvec3(v.x, v.z, v.y);
}

inline glm::dvec3 WorldToJs(const glm::dvec3& v) {
  return glm::dvec3(v.x, v.z, v.y);
}

inline glm::dvec3 JsGeoTo3D(double lonRad, double latRad, double radius) {
  const double cosLat = std::cos(latRad);
  const double sinLat = std::sin(latRad);
  return glm::dvec3(radius * cosLat * std::cos(lonRad),
                    radius * cosLat * std::sin(lonRad),
                    radius * sinLat);
}

void JsRotate3D(glm::dvec3& v, double x, double y, double z);

inline glm::dvec3 JsGeoTo3DRot(double lonRad, double latRad, double radius, const JsEuler& ea) {
  glm::dvec3 v = JsGeoTo3D(lonRad, latRad, radius);
  JsRotate3D(v, ea.x, -ea.y, ea.z);
  return v;
}

inline glm::dmat4 JsToWorldMat(const glm::dmat4& m) {
  glm::dmat4 p(1.0);
  p[1][1] = 0.0;
  p[2][2] = 0.0;
  p[1][2] = 1.0;
  p[2][1] = 1.0;
  return p * m * p;
}

// ============================================================================
// MERCATOR PROJECTION (JS FlatNavigation parity)
// ============================================================================
namespace Mercator {
  // Constants matching JS globe_constants.js
  constexpr double WORLD_SIZE = 40075016.68;       // Earth circumference in meters
  constexpr double HALF_WORLD = 20037508.34;       // Half circumference
  constexpr double MAX_LAT = 85.05112878;          // Max latitude for Web Mercator
  constexpr double MIN_LAT = -85.05112878;
  
  // JS: mercator.LonDegToMercator
  inline double LonToMerc(double lonDeg) {
    return lonDeg * HALF_WORLD / 180.0;
  }
  
  // JS: mercator.LatDegToMercator
  inline double LatToMerc(double latDeg) {
    double lat = std::clamp(latDeg, MIN_LAT, MAX_LAT);
    double latRad = lat * M_PI / 180.0;
    return std::log(std::tan(M_PI / 4.0 + latRad / 2.0)) * HALF_WORLD / M_PI;
  }
  
  // JS: mercator.MercatorToLonDeg
  inline double MercToLon(double x) {
    return x * 180.0 / HALF_WORLD;
  }
  
  // JS: mercator.MercatorToLatDeg
  inline double MercToLat(double y) {
    double latRad = 2.0 * std::atan(std::exp(y * M_PI / HALF_WORLD)) - M_PI / 2.0;
    return latRad * 180.0 / M_PI;
  }
  
  // JS: mercator.PixelToMeter
  inline double PixelToMeter(int pixelSize, double zoomLevel) {
    return WORLD_SIZE / (static_cast<double>(pixelSize) * std::pow(2.0, zoomLevel));
  }

  // JS: mercator.GetMeterForLatitude
  inline double GetMeterForLatitude(double meters, double latDeg) {
    double latRad = glm::radians(latDeg);
    return meters / std::cos(latRad);
  }
  
  // JS: mercator.ZoomScale
  inline double ZoomScale(double deltaZoom) {
    return std::pow(2.0, deltaZoom);
  }
  
  // JS: mercator.LimitToZoomMerc - Calculate zoom level for a bounding box
  inline double LimitToZoom(double minX, double minY, double maxX, double maxY, 
                            int screenWidth, int screenHeight) {
    double dx = maxX - minX;
    double dy = maxY - minY;
    double size = std::max(dx, dy);
    if (size <= 0.0) return 22.0;
    return std::log2(WORLD_SIZE / size);
  }
}  // namespace Mercator

// ============================================================================
// LOD DISTANCE TABLE (JS ao array parity)
// ============================================================================
// LOD range constants (JS globe_constants.js parity)
constexpr int GLOBE_MIN_LOD = 2;
constexpr int GLOBE_MAX_LOD = 22;

namespace LodTable {
  // JS parity: SA_TABLE values for LOD to Altitude (meters)
  // Source: webglobe.js / globe_constants.js
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
  
  constexpr int MAX_LOD = 22; // JS GLOBE_MAX_LOD

  // Get altitude in meters from LOD (exact table lookup)
  inline double AltitudeMetersFromLod(int lod) {
    if (lod < 0) lod = 0;
    if (lod > 25) lod = 25;
    return SA_TABLE[lod];
  }
  
  // Get interpolated altitude from fractional LOD
  inline double AltitudeMetersFromLod(double lod) {
    int lodLow = static_cast<int>(std::floor(lod));
    int lodHigh = static_cast<int>(std::ceil(lod));
    if (lodLow == lodHigh) return AltitudeMetersFromLod(lodLow);
    
    // Clamp to table range
    if (lodLow < 0) lodLow = 0;
    if (lodHigh > 25) lodHigh = 25;
    
    double altLow = SA_TABLE[lodLow];
    double altHigh = SA_TABLE[lodHigh];
    return altLow + (lod - lodLow) * (altHigh - altLow);
  }
  
  // Get LOD from altitude in meters (inverse lookup)
  inline double LodFromAltitudeMeters(double altMeters) {
    // Binary search or linear scan since table is sorted descending (mostly)
    // Handle out of bounds
    if (altMeters >= SA_TABLE[0]) return 0.0;
    if (altMeters <= SA_TABLE[25]) return 25.0;

    for (int i = 0; i < 25; ++i) {
      double upper = SA_TABLE[i];
      double lower = SA_TABLE[i+1];
      if (altMeters <= upper && altMeters >= lower) {
        // Interpolate between i and i+1
        // alt = upper + (lod - i) * (lower - upper)  <-- Linear assumption
        // (alt - upper) / (lower - upper) = lod - i
        double fraction = (altMeters - upper) / (lower - upper);
        return static_cast<double>(i) + fraction;
      }
    }
    return 22.0; // Default fallback (max LOD for very low altitudes)
  }

  // Legacy DistanceFromLod (used for camera distance from center)
  // Converted to use Altitude + Radius
  inline double DistanceFromLod(int lod) {
    return (AltitudeMetersFromLod(lod) * GLOBE_RADIUS_K) + GLOBE_RADIUS;
  }
}  // namespace LodTable

// ============================================================================
// ECEF NORMALIZATION (HS parity for numerical precision)
// ============================================================================
namespace ECEF {
  struct AABB {
    glm::dvec3 min;
    glm::dvec3 max;
    
    glm::dvec3 center() const {
      return (min + max) * 0.5;
    }
    
    glm::dvec3 size() const {
      return max - min;
    }
  };
  
  // Compute normalization scale for ECEF coordinates
  inline double NormalizationScale(const AABB& aabb) {
    glm::dvec3 s = aabb.size();
    return 1.0 / std::max({s.x, s.y, s.z});
  }
  
  // Create normalization matrix for ECEF coordinates (improves precision)
  inline glm::dmat4 NormalizeMatrix(const AABB& bounds) {
    double scale = NormalizationScale(bounds);
    glm::dvec3 center = bounds.center();
    
    glm::dmat4 m(1.0);
    m[0][0] = scale;
    m[1][1] = scale;
    m[2][2] = scale;
    m[3][0] = -center.x * scale;
    m[3][1] = -center.y * scale;
    m[3][2] = -center.z * scale;
    return m;
  }
  
  // Create denormalization matrix (inverse of normalize)
  inline glm::dmat4 DenormalizeMatrix(const AABB& bounds) {
    double scale = NormalizationScale(bounds);
    glm::dvec3 center = bounds.center();
    
    glm::dmat4 m(1.0);
    m[0][0] = 1.0 / scale;
    m[1][1] = 1.0 / scale;
    m[2][2] = 1.0 / scale;
    m[3][0] = center.x;
    m[3][1] = center.y;
    m[3][2] = center.z;
    return m;
  }
  
  // Compute AABB for a globe tile
  inline AABB TileBounds(int z, int x, int y) {
    double n = static_cast<double>(1 << z);
    double lonMin = (x / n) * 360.0 - 180.0;
    double lonMax = ((x + 1) / n) * 360.0 - 180.0;
    double latMax = std::atan(std::sinh(M_PI * (1.0 - 2.0 * y / n))) * 180.0 / M_PI;
    double latMin = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (y + 1) / n))) * 180.0 / M_PI;
    
    // Convert corners to ECEF and find bounds
    auto toECEF = [](double lat, double lon) -> glm::dvec3 {
      double latRad = lat * M_PI / 180.0;
      double lonRad = lon * M_PI / 180.0;
      double cosLat = std::cos(latRad);
      return glm::dvec3(
        cosLat * std::cos(lonRad) * GLOBE_RADIUS,
        cosLat * std::sin(lonRad) * GLOBE_RADIUS,
        std::sin(latRad) * GLOBE_RADIUS
      );
    };
    
    glm::dvec3 corners[4] = {
      toECEF(latMin, lonMin),
      toECEF(latMin, lonMax),
      toECEF(latMax, lonMin),
      toECEF(latMax, lonMax)
    };
    
    AABB aabb;
    aabb.min = corners[0];
    aabb.max = corners[0];
    for (int i = 1; i < 4; ++i) {
      aabb.min = glm::min(aabb.min, corners[i]);
      aabb.max = glm::max(aabb.max, corners[i]);
    }
    return aabb;
  }
}  // namespace ECEF

// ============================================================================
// UP VECTORS (HS parity - per-tile normal vectors for lighting)
// ============================================================================
namespace UpVectors {
  // Compute up vector (surface normal) at a point on the globe
  inline glm::vec3 AtLatLon(double latDeg, double lonDeg) {
    double latRad = latDeg * M_PI / 180.0;
    double lonRad = lonDeg * M_PI / 180.0;
    double cosLat = std::cos(latRad);
    return glm::normalize(glm::vec3(
      cosLat * std::cos(lonRad),
      cosLat * std::sin(lonRad),
      std::sin(latRad)
    ));
  }
  
  // Compute up vector at tile corner (u,v in [0,1])
  inline glm::vec3 AtTileUV(int z, int x, int y, float u, float v) {
    double n = static_cast<double>(1 << z);
    double lon = ((x + u) / n) * 360.0 - 180.0;
    double lat = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (y + v) / n))) * 180.0 / M_PI;
    return AtLatLon(lat, lon);
  }
  
  // Get all 4 corner up vectors for a tile
  struct TileUpVectors {
    glm::vec3 tl, tr, bl, br;  // top-left, top-right, bottom-left, bottom-right
  };
  
  inline TileUpVectors ForTile(int z, int x, int y) {
    return {
      AtTileUV(z, x, y, 0.0f, 0.0f),  // TL
      AtTileUV(z, x, y, 1.0f, 0.0f),  // TR
      AtTileUV(z, x, y, 0.0f, 1.0f),  // BL
      AtTileUV(z, x, y, 1.0f, 1.0f)   // BR
    };
  }
  
  // Interpolate up vector within tile
  inline glm::vec3 Interpolate(const TileUpVectors& uv, float u, float v) {
    glm::vec3 top = glm::mix(uv.tl, uv.tr, u);
    glm::vec3 bottom = glm::mix(uv.bl, uv.br, u);
    return glm::normalize(glm::mix(top, bottom, v));
  }
}  // namespace UpVectors

// Atmosphere namespace removed


// ============================================================================
// SCREEN POSITION HISTORY (JS FScreenLocPrevNext parity)
// ============================================================================
struct ScreenPosition {
  double lonDeg = 0.0;
  double latDeg = 0.0;
  double dist = GLOBE_MAX_DIST;
  double tiltDeg = GLOBE_MIN_TILTANGLE;
  double northAngleDeg = 0.0;
};

class ScreenPositionHistory {
public:
  static constexpr size_t MAX_HISTORY = 100;  // JS: maxSavedScrLocNmbr

  void Init() {
    history_.clear();
    curPointer_ = -1;
    doPrevPos_ = false;
    doNextPos_ = false;
  }

  // JS parity: SaveLastScreenPosition
  void SavePosition(const ScreenPosition& pos) {
    curPointer_++;
    if (curPointer_ < static_cast<int>(history_.size())) {
      history_[curPointer_] = pos;
    } else {
      history_.push_back(pos);
    }
    if (static_cast<int>(history_.size()) - 1 > curPointer_) {
      history_.resize(curPointer_ + 1);
    }
    if (history_.size() > MAX_HISTORY) {
      history_.erase(history_.begin());
      curPointer_--;
    }
  }

  bool IsPrevAvailable() const {
    return curPointer_ > -1;
  }

  bool IsNextAvailable() const {
    return curPointer_ < static_cast<int>(history_.size()) - 1;
  }

  // JS parity: goToPrevPos
  bool GoToPrev(const ScreenPosition& currentPos, ScreenPosition& out) {
    if (!IsPrevAvailable()) return false;
    if (doNextPos_) {
      doNextPos_ = false;
      doPrevPos_ = true;
      curPointer_--;
    }
    if (curPointer_ < 0 || curPointer_ >= static_cast<int>(history_.size())) return false;
    ScreenPosition target = history_[curPointer_];
    if (static_cast<int>(history_.size()) - 1 == curPointer_) {
      if (curPointer_ + 1 < static_cast<int>(history_.size())) {
        history_[curPointer_ + 1] = currentPos;
      } else {
        history_.push_back(currentPos);
      }
      doPrevPos_ = true;
    }
    out = target;
    curPointer_--;
    return true;
  }

  // JS parity: goToNextPos
  bool GoToNext(ScreenPosition& out) {
    if (!IsNextAvailable()) return false;
    if (doPrevPos_) {
      doPrevPos_ = false;
      doNextPos_ = true;
      curPointer_ += 2;
    } else {
      curPointer_++;
    }
    if (curPointer_ < 0 || curPointer_ >= static_cast<int>(history_.size())) return false;
    out = history_[curPointer_];
    return true;
  }

  void Clear() {
    Init();
  }

  int GetCurrentIndex() const { return curPointer_; }
  size_t GetHistorySize() const { return history_.size(); }
  
private:
  std::vector<ScreenPosition> history_;
  int curPointer_ = -1;
  bool doPrevPos_ = false;
  bool doNextPos_ = false;
};

RowMat3 MultiplyRowMat3(const RowMat3& a, const RowMat3& b) {
  RowMat3 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
    }
  }
  return out;
}

RowMat3 RowMat3FromQuaternion(double x, double y, double z, double w) {
  RowMat3 out{};
  const double xx = x * x;
  const double yy = y * y;
  const double zz = z * z;
  const double xy = x * y;
  const double xz = x * z;
  const double yz = y * z;
  const double wx = w * x;
  const double wy = w * y;
  const double wz = w * z;

  out.m[0][0] = 1.0 - 2.0 * yy - 2.0 * zz;
  out.m[0][1] = 2.0 * xy + 2.0 * wz;
  out.m[0][2] = 2.0 * xz - 2.0 * wy;

  out.m[1][0] = 2.0 * xy - 2.0 * wz;
  out.m[1][1] = 1.0 - 2.0 * xx - 2.0 * zz;
  out.m[1][2] = 2.0 * yz + 2.0 * wx;

  out.m[2][0] = 2.0 * xz + 2.0 * wy;
  out.m[2][1] = 2.0 * yz - 2.0 * wx;
  out.m[2][2] = 1.0 - 2.0 * xx - 2.0 * yy;
  return out;
}

glm::dmat3 ToGlmMat3(const RowMat3& m) {
  glm::dmat3 out(1.0);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[c][r] = m.m[r][c];
    }
  }
  return out;
}

RowMat3 FromGlmMat3(const glm::dmat3& m) {
  RowMat3 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out.m[r][c] = m[c][r];
    }
  }
  return out;
}

double JsDegreeToRadian(double deg) {
  return deg * M_PI / 180.0;
}

double JsRadianToDegree(double rad) {
  return rad * 180.0 / M_PI;
}

double Js180to90(double rad) {
  return -(rad + M_PI / 2.0 - M_PI);
}

void JsCSRotate(double angle, double cx, double cy, double x, double y, glm::dvec2& out) {
  out.x = (x - cx) * std::sin(angle) + (y - cy) * std::cos(angle) + cy;
  out.y = (x - cx) * std::cos(angle) - (y - cy) * std::sin(angle) + cx;
}

void JsRotate3D(glm::dvec3& v, double x, double y, double z) {
  glm::dvec2 tmp{};
  JsCSRotate(x, 0.0, 0.0, v.y, v.z, tmp);
  v.y = tmp.y;
  v.z = tmp.x;
  JsCSRotate(y, 0.0, 0.0, v.x, v.z, tmp);
  v.x = tmp.y;
  v.z = tmp.x;
  JsCSRotate(z, 0.0, 0.0, v.x, v.y, tmp);
  v.x = tmp.y;
  v.y = tmp.x;
}

void JsCarToSpher(const glm::dvec3& v, glm::dvec3& out) {
  const double mag = glm::length(v);
  out.x = mag;
  double ang = std::acos(v.z / mag);
  if (!std::isfinite(ang)) {
    ang = v.y < 0.0 ? M_PI : 0.0;
  }
  out.y = Js180to90(ang);
  double lon = std::atan2(v.y, v.x);
  if (!std::isfinite(lon)) lon = 0.0;
  out.z = lon;
}

void JsRot3DToGeo(glm::dvec3 v, const JsEuler& e, glm::dvec2& outLonLat) {
  glm::dvec2 tmp{};
  JsCSRotate(e.x, 0.0, 0.0, v.y, v.z, tmp);
  v.y = tmp.y;
  v.z = tmp.x;
  JsCSRotate(-e.y, 0.0, 0.0, v.x, v.z, tmp);
  v.x = tmp.y;
  v.z = tmp.x;
  JsCSRotate(e.z, 0.0, 0.0, v.x, v.y, tmp);
  v.x = tmp.y;
  v.y = tmp.x;
  glm::dvec3 sph{};
  JsCarToSpher(v, sph);
  outLonLat.x = sph.z;
  outLonLat.y = sph.y;
}

void JsEulToHMatrix(const JsEuler& e, RowMat3& out) {
  const double i = e.x;
  const double a = e.y;
  const double o = e.z;
  const double n = std::cos(i);
  const double s = std::cos(a);
  const double l = std::cos(o);
  const double h = std::sin(i);
  const double u = std::sin(a);
  const double c = std::sin(o);
  const double d = n * l;
  const double f = n * c;
  const double y = h * l;
  const double v = h * c;

  out.m[0][0] = s * l;
  out.m[0][1] = u * y - f;
  out.m[0][2] = u * d + v;
  out.m[1][0] = s * c;
  out.m[1][1] = u * v + d;
  out.m[1][2] = u * f - y;
  out.m[2][0] = -u;
  out.m[2][1] = s * h;
  out.m[2][2] = s * n;
}

void JsEulFromHMatrix(const RowMat3& m, JsEuler& out) {
  const double o = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[1][0] * m.m[1][0]);
  if (o > 1e-8) {
    out.x = std::atan2(m.m[2][1], m.m[2][2]);
    out.y = std::atan2(-m.m[2][0], o);
    out.z = std::atan2(m.m[1][0], m.m[0][0]);
  } else {
    out.x = std::atan2(-m.m[1][2], m.m[1][1]);
    out.y = std::atan2(-m.m[2][0], o);
    out.z = 0.0;
  }
}

struct JsArcBall {
  RowMat3 abQuat{};
  RowMat3 abLast{};
  RowMat3 abNext{};
  glm::dvec2 abStartPix{};
  glm::dvec2 abCurPix{};
  glm::dvec3 abStart{0.0, 0.0, 1.0};
  glm::dvec3 abCurr{0.0, 0.0, 1.0};
  glm::dvec3 abInertiaAxis{0.0, 0.0, 1.0};  // Cached axis for inertia
  glm::dvec3 abEye{};
  glm::dvec3 abEyeDir{};
  double abZoom2 = 0.0;
  double abZoom = 0.0;
  double abSphere = 1.0;
  double abEdge = 1.0;
  glm::dmat4 abGLM{1.0};
  glm::dmat4 abGLP{1.0};
  glm::ivec4 abGLV{0, 0, 0, 0};

  void UpdateMatrices(double sphereRadius, const glm::dvec3& eyeJs,
                      const glm::dmat4& proj, const glm::dmat4& model,
                      const glm::ivec4& viewport) {
    abEye = eyeJs;
    abZoom2 = glm::dot(abEye, abEye);
    abZoom = std::sqrt(abZoom2);
    abSphere = sphereRadius * sphereRadius;
    if (abZoom > 0.0) {
      abEyeDir = abEye / abZoom;
      abEdge = abSphere / abZoom;
    }
    abGLP = proj;
    abGLM = model;
    abGLV = viewport;
  }

  void XMb3Il(const glm::dvec3& t, glm::dvec3& out) const {
    const double denom = glm::dot(abEyeDir, t);
    if (std::abs(denom) < 1e-12) {
      out = glm::normalize(t);
      return;
    }
    const double r = (abEdge - abZoom) / denom;
    const glm::dvec3 n = abEye + t * r;
    const glm::dvec3 s = abEyeDir * abEdge - n;
    const double i = glm::dot(n, s);
    const double a = glm::dot(s, s);
    const double disc = i * i - a * (glm::dot(n, n) - abSphere);
    if (disc <= 0.0) {
      out = glm::normalize(t);
      return;
    }
    const double o = (0.0 - i - std::sqrt(disc)) / a;
    out = glm::normalize(n + s * o);
  }

  bool ProjectToSphere(double sx, double sy, glm::dvec3& out) const {
    if (abGLV.z <= 0 || abGLV.w <= 0) return false;
    const double glY = static_cast<double>(abGLV.y + abGLV.w) - sy;
    glm::dvec3 world = glm::unProject(glm::dvec3(sx, glY, 0.0), abGLM, abGLP, glm::dvec4(abGLV));
    glm::dvec3 p = WorldToJs(world);
    glm::dvec3 h = p - abEye;
    const double i = glm::dot(h, h);
    const double a = glm::dot(abEye, h);
    const double disc = a * a - i * (abZoom2 - abSphere);
    if (disc <= 0.0) {
      XMb3Il(h, out);
      return true;
    }
    const double n = (0.0 - a - std::sqrt(disc)) / i;
    out = glm::normalize(abEye + h * n);
    return true;
  }

  double Begin(double sx, double sy) {
    abLast = abQuat;
    abStartPix = glm::dvec2(sx, sy);
    ProjectToSphere(sx, sy, abStart);
    return 0.0;
  }

  double Drag(double sx, double sy) {
    abCurPix = glm::dvec2(sx, sy);
    ProjectToSphere(sx, sy, abCurr);
    if (glm::length(abCurr - abStart) < 1e-6 ||
        glm::length(abCurPix - abStartPix) < 2.0) {
      abQuat = abLast;
      abStart = abCurr;
      abStartPix = abCurPix;
      return 0.0;
    }
    const double a = glm::dot(abStart, abCurr);
    const double o = std::sqrt(0.5 * (1.0 - a));
    const double n = std::sqrt(0.5 * (1.0 + a));
    glm::dvec3 axis = glm::cross(abCurr, abStart); // P3: Swap for correct "Grab" direction
    if (glm::length(axis) > 1e-8) {
      axis = glm::normalize(axis) * o;
    } else {
      axis = glm::dvec3(0.0);
    }
    abNext = RowMat3FromQuaternion(axis.x, axis.y, axis.z, n);
    abQuat = MultiplyRowMat3(abLast, abNext);
    // JS parity: Absolute update (relative to Begin), no incremental update.
    return JsRadianToDegree(std::acos(n));
  }

  bool FlyRotate(const glm::dvec3& target, const glm::dvec3& current, double stepDiv, double thresholdDeg) {
    abLast = abQuat;
    glm::dvec3 a = glm::normalize(current);
    glm::dvec3 t = glm::normalize(target);
    double dot = glm::dot(t, a);
    dot = std::clamp(dot, -1.0, 1.0);
    const double n = std::sqrt(0.5 * (1.0 + dot));
    double angle = std::acos(n);
    const double thresholdRad = JsDegreeToRadian(thresholdDeg);
    const bool incremental = !(std::abs(angle) < thresholdRad);
    if (!incremental) {
      // Use full rotation when very close
      stepDiv = 1.0;
    }
    const double s = angle / stepDiv;
    const double h = std::sin(s);
    glm::dvec3 axis = glm::cross(t, a);
    if (glm::length(axis) < 1e-12) {
      return incremental;
    }
    axis = glm::normalize(axis);
    RowMat3 q = RowMat3FromQuaternion(axis.x * h, axis.y * h, axis.z * h, std::cos(s));
    abQuat = MultiplyRowMat3(abLast, q);
    return incremental;
  }

  // JS: XuzxcV inertia - rotate by angle around cached drag axis
  // Note: angleRad is already the half-angle (from Drag() which returns acos(n))
  bool RotateByAngle(double halfAngleRad) {
    if (std::abs(halfAngleRad) < 1e-9) return false;
    if (glm::length(abInertiaAxis) < 1e-12) return false;
    
    // Use halfAngleRad directly - Drag() already returns acos(n) which is half-angle
    const double sinH = std::sin(halfAngleRad);
    const double cosH = std::cos(halfAngleRad);
    
    RowMat3 delta = RowMat3FromQuaternion(
        abInertiaAxis.x * sinH, abInertiaAxis.y * sinH, abInertiaAxis.z * sinH, cosH);
    abQuat = MultiplyRowMat3(abQuat, delta);
    return true;
  }

  // Cache the current drag axis for inertia use
  void CacheInertiaAxis() {
    glm::dvec3 axis = glm::cross(abCurr, abStart); // P3: Swap for correct Inertia direction
    if (glm::length(axis) > 1e-12) {
      abInertiaAxis = glm::normalize(axis);
    }
  }
};

struct JsCamera {
  JsArcBall arcball;
  JsEuler ea{};
  double tiltDeg = GLOBE_MIN_TILTANGLE;
  double dist = GLOBE_START_DIST_YATAY;
  double camZ = 0.0;
  glm::dvec2 camLongLat{0.0, 0.0};
  double saveTiltDeg = 0.0;
  double saveDist = 0.0;
  double saveCamZ = 0.0;
};

// Per-layer tile storage for multi-raster rendering
struct RasterLayerTiles {
  std::string layerId;
  std::unordered_map<std::string, Tile> tiles;
  std::vector<Tile*> visibleTiles;
  std::unordered_set<std::string> pendingDownloads;
};

enum class NetRequestStatus {
  Pending,
  Success,
  Failed
};

struct NetRequestEntry {
  std::string url;
  std::string type;  // "tile", "vector", "dem"
  NetRequestStatus status = NetRequestStatus::Pending;
  double startTime = 0.0;
  double endTime = 0.0;
  size_t bytes = 0;
};

constexpr size_t kMaxNetworkLogEntries = 100;
constexpr size_t kMaxReadyDownloadsPerFrame = 64;  // Increased for faster loading (was 16)
// kMaxMeshRebuildsPerFrame removed in favor of dynamic limit (Adaptive Upload P2)
constexpr size_t kMaxDownloadQueueSize = 512;     // Increased for more concurrent downloads (was 256)
constexpr size_t kMaxPendingDemBatches = 16;

// JS parity: Generate LOD altitude table from LodTable formula
// Formula: distance = GLOBE_RADIUS * pow(2, 22 - lod) / 256
// altitude_meters = (distance - GLOBE_RADIUS) / GLOBE_RADIUS_K
// Note: For LOD >= 14, distance < GLOBE_RADIUS, so we use minimum altitude
inline double ComputeLodAltitudeMeters(int lod) {
  double distance = LodTable::DistanceFromLod(lod);
  double altitudeWorld = distance - GLOBE_RADIUS;
  if (altitudeWorld < 0.0) {
    // Below surface - use minimum practical altitude (10 meters)
    return 10.0 + (22 - lod) * 2.0;  // Gradual decrease for high LOD
  }
  return altitudeWorld / GLOBE_RADIUS_K;
}

double ClampLod(double lod, int minZoom, int maxZoom) {
  if (lod < static_cast<double>(minZoom)) return static_cast<double>(minZoom);
  if (lod > static_cast<double>(maxZoom)) return static_cast<double>(maxZoom);
  return lod;
}

// FAZ 1: JS parity - Use SA_TABLE for LOD calculation (like JS FindLODFromAltitude)
// JS: FindLODFromAltitude does reverse lookup in Sa table with interpolation
double FindLodFromAltitudeMeters(double altitudeMeters, int minZoom, int maxZoom) {
  // Use SA_TABLE based lookup (JS parity)
  double lod = LodTable::LodFromAltitudeMeters(altitudeMeters);
  return ClampLod(lod, minZoom, maxZoom);
}

double FindAltitudeFromLod(double lod) {
  // JS parity: Use SA_TABLE interpolation (same as FindAltitudeFromLOD in JS)
  return LodTable::AltitudeMetersFromLod(lod);
}

// JS parity: Screen-width distance table (Mi) for FindDistForSrcWdMeter
struct DistFactorEntry {
  double width;
  double fov;
  double ratioFactor;
  std::array<double, 11> small;  // ratios 0.2..1.2
  std::array<double, 26> big;    // ratios 2.5..5.0
};

static const DistFactorEntry kDistFactorTable[] = {
  {
    1000000, 51, 0.05,
    { 1.029, 1.031, 1.033, 1.031, 1.029, 1.027, 1.025, 1.023, 1.02, 1.015, 1.01 },
    { 1, 1, 1, 1, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.98, 0.98, 0.98, 0.98, 0.98, 0.98, 0.97, 0.97, 0.97, 0.97, 0.97, 0.97, 0.96 }
  },
  {
    2000000, 52.75, 0.0625,
    { 1.06, 1.05, 1.04, 1.03, 1.025, 1.015, 1.01, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1.01, 1.015, 1.017, 1.019, 1.021, 1.023, 1.025, 1.027, 1.029, 1.031, 1.033, 1.035, 1.037, 1.039, 1.041, 1.043, 1.045, 1.047, 1.049 }
  },
  {
    3000000, 54.5, 0.0833333333333,
    { 1.075, 1.068, 1.05, 1.038, 1.03, 1.02, 1.015, 1.01, 1.01, 1, 1 },
    { 1.01, 1.015, 1.016, 1.018, 1.022, 1.026, 1.029, 1.034, 1.037, 1.04, 1.043, 1.047, 1.052, 1.059, 1.063, 1.067, 1.071, 1.075, 1.079, 1.083, 1.087, 1.091, 1.095, 1.099, 1.103, 1.107 }
  },
  {
    4000000, 56.5, 0.125,
    { 1.17, 1.13, 1.095, 1.07, 1.056, 1.04, 1.03, 1.02, 1.015, 1.01, 1.005 },
    { 1.005, 1.01, 1.015, 1.02, 1.023, 1.026, 1.032, 1.037, 1.042, 1.047, 1.052, 1.057, 1.064, 1.074, 1.08, 1.086, 1.092, 1.098, 1.104, 1.11, 1.116, 1.122, 1.128, 1.134, 1.14, 1.146 }
  },
  {
    5000000, 58.5, 0.166666666667,
    { 1.23, 1.19, 1.15, 1.11, 1.085, 1.065, 1.05, 1.037, 1.025, 1.017, 1.012 },
    { 1.01, 1.15, 1.02, 1.025, 1.03, 1.036, 1.043, 1.05, 1.059, 1.065, 1.072, 1.083, 1.09, 1.097, 1.11, 1.12, 1.13, 1.14, 1.15, 1.16, 1.17, 1.18, 1.19, 1.2, 1.21, 1.22 }
  },
  {
    6000000, 61, 0.2,
    { 1.24, 1.24, 1.19, 1.14, 1.1, 1.078, 1.058, 1.04, 1.03, 1.02, 1.012 },
    { 1.015, 1.02, 1.027, 1.035, 1.043, 1.051, 1.06, 1.072, 1.081, 1.093, 1.1, 1.12, 1.14, 1.16, 1.18, 1.2, 1.21, 1.22, 1.23, 1.24, 1.25, 1.26, 1.27, 1.28, 1.29, 1.3 }
  },
  {
    7000000, 63.5, 0.25,
    { 1.34, 1.34, 1.24, 1.19, 1.14, 1.09, 1.07, 1.06, 1.05, 1.03, 1.03 },
    { 1.02, 1.03, 1.035, 1.045, 1.055, 1.065, 1.075, 1.085, 1.095, 1.1, 1.1, 1.101, 1.102, 1.103, 1.104, 1.105, 1.106, 1.107, 1.108, 1.109, 1.11, 1.111, 1.112, 1.113, 1.114, 1.115 }
  },
  {
    8000000, 66.5, 0.294117647059,
    { 1.29, 1.29, 1.3, 1.23, 1.18, 1.13, 1.1, 1.079, 1.055, 1.04, 1.03 },
    { 1.024, 1.032, 1.041, 1.05, 1.059, 1.07, 1.08, 1.09, 1.1, 1.12, 1.14, 1.16, 1.18, 1.2, 1.22, 1.24, 1.26, 1.28, 1.3, 1.32, 1.34, 1.35, 1.36, 1.37, 1.38, 1.39 }
  },
  {
    9000000, 69.75, 0.333333333333,
    { 1.1, 1.1, 1.35, 1.26, 1.21, 1.15, 1.12, 1.09, 1.065, 1.045, 1.03 },
    { 1.02, 1.025, 1.03, 1.035, 1.042, 1.05, 1.065, 1.08, 1.095, 1.1, 1.12, 1.14, 1.15, 1.16, 1.17, 1.18, 1.185, 1.19, 1.195, 1.2, 1.205, 1.21, 1.215, 1.22, 1.225, 1.23 }
  },
  {
    10000000, 73.5, 0.338983050847,
    { 1.31, 1.31, 1.31, 1.24, 1.17, 1.13, 1.096, 1.07, 1.05, 1.034, 1.023 },
    { 1.03, 1.035, 1.043, 1.055, 1.067, 1.08, 1.095, 1.1, 1.11, 1.12, 1.13, 1.14, 1.15, 1.16, 1.17, 1.18, 1.19, 1.2, 1.21, 1.22, 1.23, 1.235, 1.24, 1.245, 1.25, 1.255 }
  },
  {
    11000000, 77.5, 0.344827586207,
    { 1.3, 1.3, 1.3, 1.22, 1.16, 1.12, 1.085, 1.065, 1.045, 1.022, 1.012 },
    { 1.04, 1.045, 1.055, 1.065, 1.075, 1.085, 1.095, 1.1, 1.11, 1.12, 1.13, 1.14, 1.15, 1.16, 1.17, 1.18, 1.19, 1.2, 1.21, 1.22, 1.23, 1.235, 1.24, 1.245, 1.25, 1.255 }
  },
};

struct FovDistInfo {
  double fov;
  double distFactor;
};

inline double Lerp(double a, double b, double t) {
  return a + (b - a) * t;
}

inline double GetDistFactorAtRatio(const DistFactorEntry& lo,
                                   const DistFactorEntry& hi,
                                   double y,
                                   double ratio,
                                   bool small) {
  double ratioRounded = std::round(ratio * 10.0) / 10.0;
  int idx = static_cast<int>(std::round(ratioRounded * 10.0));
  if (small) {
    idx = std::clamp(idx - 2, 0, 10);
    return Lerp(lo.small[idx], hi.small[idx], y * y);
  }
  idx = std::clamp(idx - 25, 0, 25);
  return Lerp(lo.big[idx], hi.big[idx], y * y);
}

inline double CalculateDistFactor(double y,
                                  double aspect,
                                  const DistFactorEntry& lo,
                                  const DistFactorEntry& hi) {
  if (aspect > 1.2 && aspect < 2.5) {
    return 1.0;
  }
  double ratio = std::clamp(aspect, 0.2, 5.0);
  if (ratio >= 0.2 && ratio <= 1.2) {
    return GetDistFactorAtRatio(lo, hi, y, ratio, true);
  }
  if (ratio >= 2.5 && ratio <= 5.0) {
    return GetDistFactorAtRatio(lo, hi, y, ratio, false);
  }
  return 1.0;
}

inline FovDistInfo CalculateFOVAndDistFactor(double widthMeters, double aspect) {
  constexpr double kAspectBase = 1520.0 / 953.0;
  constexpr size_t kTableSize = sizeof(kDistFactorTable) / sizeof(kDistFactorTable[0]);
  constexpr size_t kLast = kTableSize - 1;

  if (widthMeters < 1e6) {
    return {static_cast<double>(GLOBE_FOV), 1.0};
  }

  if (widthMeters >= 11e6) {
    const auto& entry = kDistFactorTable[kLast];
    double fovScale = std::pow(aspect / kAspectBase, entry.ratioFactor);
    double distFactor = CalculateDistFactor(0.0, aspect, entry, entry);
    return {entry.fov * fovScale, distFactor};
  }

  size_t idx = 0;
  while (idx + 1 < kTableSize && kDistFactorTable[idx + 1].width <= widthMeters) {
    ++idx;
  }
  size_t next = std::min(idx + 1, kLast);
  const auto& lo = kDistFactorTable[idx];
  const auto& hi = kDistFactorTable[next];
  double y = (widthMeters - lo.width) / (hi.width - lo.width);
  double fov = Lerp(lo.fov, hi.fov, y);
  double ratioFactor = Lerp(lo.ratioFactor, hi.ratioFactor, y * y);
  double fovAdjusted = fov * std::pow(aspect / kAspectBase, ratioFactor);
  double distFactor = CalculateDistFactor(y, aspect, lo, hi);
  return {fovAdjusted, distFactor};
}

inline double GreatCircleDistanceMeters(double lon1Deg, double lat1Deg,
                                        double lon2Deg, double lat2Deg) {
  double lat1 = glm::radians(lat1Deg);
  double lat2 = glm::radians(lat2Deg);
  double dlon = glm::radians(lon2Deg - lon1Deg);
  double sinLat1 = std::sin(lat1);
  double sinLat2 = std::sin(lat2);
  double cosLat1 = std::cos(lat1);
  double cosLat2 = std::cos(lat2);
  double cosAngle = sinLat1 * sinLat2 + cosLat1 * cosLat2 * std::cos(dlon);
  cosAngle = std::clamp(cosAngle, -1.0, 1.0);
  double angle = std::acos(cosAngle);
  return angle * (GLOBE_RADIUS / GLOBE_RADIUS_K);
}

uint64_t HashFnv1a64(const std::string& data) {
  const uint64_t kOffset = 14695981039346656037ull;
  const uint64_t kPrime = 1099511628211ull;
  uint64_t hash = kOffset;
  for (unsigned char c : data) {
    hash ^= static_cast<uint64_t>(c);
    hash *= kPrime;
  }
  return hash;
}

std::string HashToHex(uint64_t value) {
  char buf[17] = {};
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
  return std::string(buf);
}

bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  file.seekg(0, std::ios::end);
  std::streamsize size = file.tellg();
  if (size <= 0) return false;
  file.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(size));
  return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return false;
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(file);
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t totalSize = size * nmemb;
  auto* buffer = static_cast<std::vector<unsigned char>*>(userp);
  buffer->insert(buffer->end(), static_cast<unsigned char*>(contents),
                 static_cast<unsigned char*>(contents) + totalSize);
  return totalSize;
}

// Download timeout in seconds
constexpr long DOWNLOAD_TIMEOUT_SECONDS = 10;
constexpr long DOWNLOAD_CONNECT_TIMEOUT_SECONDS = 5;
constexpr long MESH_DOWNLOAD_TIMEOUT_SECONDS = 6;  // JS: MESH_TIMEOUT_MS = 6000

// Extract origin (scheme + host) from URL for Referer/Origin headers
static std::string ExtractOrigin(const std::string& url) {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return "";
  size_t hostStart = schemeEnd + 3;
  
  // Find the first delimiter that ends the authority section: '/', '?', or '#'
  size_t pathStart = url.find('/', hostStart);
  size_t queryStart = url.find('?', hostStart);
  size_t fragmentStart = url.find('#', hostStart);
  
  size_t end = url.length();
  if (pathStart != std::string::npos && pathStart < end) end = pathStart;
  if (queryStart != std::string::npos && queryStart < end) end = queryStart;
  if (fragmentStart != std::string::npos && fragmentStart < end) end = fragmentStart;
  
  return url.substr(0, end);
}

bool DownloadUrl(const std::string& url, std::vector<unsigned char>& out, 
                 long timeoutSeconds = DOWNLOAD_TIMEOUT_SECONDS,
                 const std::vector<std::string>& customHeaders = {}) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  // Use a modern User-Agent to avoid blocking by some WAFs
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  
  struct curl_slist* headers = nullptr;
  for (const auto& header : customHeaders) {
    headers = curl_slist_append(headers, header.c_str());
  }
  
  if (headers) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  
  // Set timeouts to prevent hanging downloads
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, DOWNLOAD_CONNECT_TIMEOUT_SECONDS);
  
  // Low speed limit - abort if less than 1KB/s for 20 seconds
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
  
  CURLcode res = curl_easy_perform(curl);
  long responseCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
  
  if (headers) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl);
  return res == CURLE_OK && responseCode == 200;
}

struct DemCell {
  int tileX = 0;
  int tileY = 0;
  int level = 0;
  double llx = 0.0;
  double lly = 0.0;
  double urx = 0.0;
  double ury = 0.0;
};

struct DemJob {
  std::string url;
  std::string batchKey;
  int meshN = 0;
  int batchGrid = 1;
  int retryCount = 0;
  std::vector<DemCell> cells;
};

std::string BuildDemBatchUrl(const std::string& baseUrl,
                             MeshType meshType,
                             int meshN,
                             const std::vector<DemCell>& cells,
                             bool debug = false) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(15);
  oss << baseUrl
      << "?FLOAT=1&MESHN=" << meshN
      << "&CN=" << cells.size();
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto& cell = cells[i];
    const size_t idx = i + 1;
    if (meshType == MeshType::XYZ_MERCATOR) {
      oss << "&C" << idx << "z=" << cell.level
          << "&C" << idx << "x=" << cell.tileX
          << "&C" << idx << "y=" << cell.tileY;
    } else {
      oss << "&C" << idx << "LLX=" << cell.llx
          << "&C" << idx << "LLY=" << cell.lly
          << "&C" << idx << "URX=" << cell.urx
          << "&C" << idx << "URY=" << cell.ury;
    }
  }
  
  std::string finalUrl = oss.str();
  if (debug) {
    fprintf(stderr, "[DEM DEBUG] Generated URL: %s\n", finalUrl.c_str());
    if (!cells.empty()) {
        fprintf(stderr, "[DEM DEBUG] First Cell Params: Level=%d, TileX=%d, TileY=%d, LLX=%.6f, LLY=%.6f, URX=%.6f, URY=%.6f\n",
                cells[0].level, cells[0].tileX, cells[0].tileY, cells[0].llx, cells[0].lly, cells[0].urx, cells[0].ury);
    }
  }
  return finalUrl;
}

bool ParseDemGrid(const std::string& payload, int meshN, std::vector<double>& out, bool debug = false) {
  if (debug) {
    std::string preview = payload.substr(0, std::min<size_t>(payload.size(), 200));
    fprintf(stderr, "[DEM DEBUG] Response Preview (%zu bytes): %s...\n", payload.size(), preview.c_str());
  }

  out.clear();
  const char* cur = payload.c_str();
  const char* searchStart = cur;
  
  // 1. Prioritize finding "data" key to narrow down context
  const char* dataKey = std::strstr(cur, "\"data\"");
  if (dataKey) {
      searchStart = dataKey;
  }
  
  const char* arrayStart = nullptr;
  
  // 2. Search for 2D array start "[[" (strict)
  arrayStart = std::strstr(searchStart, "[[");
  
  // 3. If strict search failed, try whitespace-tolerant search "[ ... ["
  if (!arrayStart) {
      const char* firstOpen = std::strchr(searchStart, '[');
      if (firstOpen) {
          const char* nextChar = firstOpen + 1;
          while (*nextChar && std::isspace(static_cast<unsigned char>(*nextChar))) nextChar++;
          if (*nextChar == '[') {
              arrayStart = firstOpen;
          }
      }
  }
  
  if (!arrayStart) {
      // HARD FAIL: No 2D grid structure found.
      fprintf(stderr, "DEM Parse Error: No 2D grid '[[...]]' found in payload.\n");
      return false;
  }
  
  cur = arrayStart;

  char* end = nullptr;
  while (*cur) {
    if ((*cur >= '0' && *cur <= '9') || *cur == '-' || *cur == '+') {
      double val = std::strtod(cur, &end);
      if (end != cur) {
        out.push_back(val);
        cur = end;
        continue;
      }
    }
    ++cur;
  }
  
  if (out.size() < static_cast<size_t>(meshN * meshN)) {
      // Always log warning if parse failed/incomplete, even if debug is off
      fprintf(stderr, "DEM Parse Warning: Parsed %zu values (expected >= %d). Payload size: %zu\n", 
              out.size(), meshN*meshN, payload.size());
  } else if (debug) {
      fprintf(stderr, "DEM Parse OK: %zu values\n", out.size());
  }
  
  return out.size() >= static_cast<size_t>(meshN * meshN);
}

double SampleDemBilinear(const std::vector<double>& grid,
                          int meshN,
                          double u,
                          double v) {
  int n = meshN;
  if (n <= 1) return grid.empty() ? 0.0 : grid[0];
  u = std::clamp(u, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);
  double x = u * (n - 1);
  double y = v * (n - 1);
  int x0 = static_cast<int>(std::floor(x));
  int y0 = static_cast<int>(std::floor(y));
  int x1 = std::min(x0 + 1, n - 1);
  int y1 = std::min(y0 + 1, n - 1);
  double tx = x - x0;
  double ty = y - y0;

  auto idx = [n](int row, int col) {
    return static_cast<size_t>(row * n + col);
  };

  double h00 = grid[idx(y0, x0)];
  double h10 = grid[idx(y0, x1)];
  double h01 = grid[idx(y1, x0)];
  double h11 = grid[idx(y1, x1)];

  double h0 = h00 + (h10 - h00) * tx;
  double h1 = h01 + (h11 - h01) * tx;
  return h0 + (h1 - h0) * ty;
}

// JS parity: Enhanced bilinear sampling with neighbor stitching
// Uses boundary arrays (FMB2 equivalent) for seamless edge interpolation
// boundaries size is meshN*2-1 to support child tile stitching
// rowsNorthToSouth: true if row 0 is north, false if row 0 is south
double SampleDemBilinearStitched(const std::vector<double>& grid,
                                  int meshN,
                                  double u,
                                  double v,
                                  const std::array<std::vector<double>, 4>& boundaries,
                                  const bool boundariesValid[4],
                                  const bool childBoundariesValid[4],
                                  bool rowsNorthToSouth = true) {
  int n = meshN;
  if (n <= 1) return grid.empty() ? 0.0 : grid[0];
  
  u = std::clamp(u, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);
  
  double x = u * (n - 1);
  double y = v * (n - 1);
  int x0 = static_cast<int>(std::floor(x));
  int y0 = static_cast<int>(std::floor(y));
  int x1 = std::min(x0 + 1, n - 1);
  int y1 = std::min(y0 + 1, n - 1);
  double tx = x - x0;
  double ty = y - y0;

  auto idx = [n](int row, int col) {
    return static_cast<size_t>(row * n + col);
  };
  
  // Get base grid values
  double h00 = grid[idx(y0, x0)];
  double h10 = grid[idx(y0, x1)];
  double h01 = grid[idx(y1, x0)];
  double h11 = grid[idx(y1, x1)];
  
  // JS parity: Apply edge stitching from neighbor boundaries
  // Edge blending zone (within 1 cell of edge)
  const double edgeBlendDist = 1.0;
  const int c = n - 1;  // JS: c = De - 1
  
  // Determine which boundary indices to use for top/bottom based on row ordering
  // boundaries[2] = bottom (south), boundaries[3] = top (north) in geographic terms
  // When rowsNorthToSouth is true: row 0 = north (top), row n-1 = south (bottom)
  // When rowsNorthToSouth is false: row 0 = south (bottom), row n-1 = north (top)
  int topBoundaryIdx = rowsNorthToSouth ? 3 : 2;     // Geographic north
  int bottomBoundaryIdx = rowsNorthToSouth ? 2 : 3;  // Geographic south
  
  // Helper to get boundary value with child support
  // If childBoundariesValid, use extended boundary (size meshN*2-1)
  // y coordinate maps: for meshN=5, c=4, boundary has indices 0..8
  // First half (0..4) = first child, second half (4..8) = second child
  auto getBoundaryValue = [&](int dir, int coord) -> double {
    if (!boundariesValid[dir] || boundaries[dir].empty()) return 0.0;
    
    if (childBoundariesValid[dir] && boundaries[dir].size() >= static_cast<size_t>(n * 2 - 1)) {
      // Use extended boundary with child data
      // Map coord (0..n-1) to extended index based on position
      // For smooth interpolation across child boundary
      int extIdx = coord;  // Use direct mapping for first half
      if (coord >= c / 2) {
        // Blend towards second child data
        extIdx = coord + (coord - c / 2);
        if (extIdx >= static_cast<int>(boundaries[dir].size())) {
          extIdx = static_cast<int>(boundaries[dir].size()) - 1;
        }
      }
      return boundaries[dir][extIdx];
    }
    
    // Standard boundary (size meshN)
    if (static_cast<size_t>(coord) < boundaries[dir].size()) {
      return boundaries[dir][coord];
    }
    return 0.0;
  };
  
  // Left edge (x0 == 0): blend with boundary[0] (left neighbor's right edge)
  if (x0 == 0 && boundariesValid[0] && !boundaries[0].empty()) {
    double blendFactor = 1.0 - std::min(x, edgeBlendDist) / edgeBlendDist;
    if (blendFactor > 0.0) {
      double bh0 = getBoundaryValue(0, y0);
      double bh1 = getBoundaryValue(0, y1);
      h00 = h00 * (1.0 - blendFactor) + bh0 * blendFactor;
      h01 = h01 * (1.0 - blendFactor) + bh1 * blendFactor;
    }
  }
  
  // Right edge (x1 == n-1): blend with boundary[1] (right neighbor's left edge)
  if (x1 == n - 1 && boundariesValid[1] && !boundaries[1].empty()) {
    double distFromEdge = (n - 1) - x;
    double blendFactor = 1.0 - std::min(distFromEdge, edgeBlendDist) / edgeBlendDist;
    if (blendFactor > 0.0) {
      double bh0 = getBoundaryValue(1, y0);
      double bh1 = getBoundaryValue(1, y1);
      h10 = h10 * (1.0 - blendFactor) + bh0 * blendFactor;
      h11 = h11 * (1.0 - blendFactor) + bh1 * blendFactor;
    }
  }
  
  // Bottom edge (y1 == n-1): blend with bottom neighbor
  if (y1 == n - 1 && boundariesValid[bottomBoundaryIdx] && !boundaries[bottomBoundaryIdx].empty()) {
    double distFromEdge = (n - 1) - y;
    double blendFactor = 1.0 - std::min(distFromEdge, edgeBlendDist) / edgeBlendDist;
    if (blendFactor > 0.0) {
      double bh0 = getBoundaryValue(bottomBoundaryIdx, x0);
      double bh1 = getBoundaryValue(bottomBoundaryIdx, x1);
      h01 = h01 * (1.0 - blendFactor) + bh0 * blendFactor;
      h11 = h11 * (1.0 - blendFactor) + bh1 * blendFactor;
    }
  }
  
  // Top edge (y0 == 0): blend with top neighbor
  if (y0 == 0 && boundariesValid[topBoundaryIdx] && !boundaries[topBoundaryIdx].empty()) {
    double blendFactor = 1.0 - std::min(y, edgeBlendDist) / edgeBlendDist;
    if (blendFactor > 0.0) {
      double bh0 = getBoundaryValue(topBoundaryIdx, x0);
      double bh1 = getBoundaryValue(topBoundaryIdx, x1);
      h00 = h00 * (1.0 - blendFactor) + bh0 * blendFactor;
      h10 = h10 * (1.0 - blendFactor) + bh1 * blendFactor;
    }
  }

  double h0 = h00 + (h10 - h00) * tx;
  double h1 = h01 + (h11 - h01) * tx;
  return h0 + (h1 - h0) * ty;
}

GLuint CreateFallbackTexture(const glm::vec3& color) {
  GLuint tex = 0;
  unsigned char pixel[4] = {
      static_cast<unsigned char>(color.r * 255.0f),
      static_cast<unsigned char>(color.g * 255.0f),
      static_cast<unsigned char>(color.b * 255.0f),
      255};
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

GLuint CreateTextureFromMemory(const std::vector<unsigned char>& data) {
  int width = 0, height = 0, channels = 0;
  // stbi_set_flip handled globally in Constructor
  unsigned char* img = stbi_load_from_memory(data.data(),
                                             static_cast<int>(data.size()),
                                             &width, &height, &channels, 4);
  if (!img) {
    return 0;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, img);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(img);
  return tex;
}

GLuint CreateTextureFromRGBA(const unsigned char* pixels, int width, int height) {
  if (!pixels || width <= 0 || height <= 0) {
    return 0;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

GLuint CreateLoadingTexture() {
  return CreateFallbackTexture(glm::vec3(0.35f, 0.35f, 0.38f));
}

std::string MakeTileKey(int z, int x, int y) {
  return std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
}

int WrapTileX(int x, int n) {
  int r = x % n;
  return r < 0 ? r + n : r;
}

int ClampTileY(int y, int n) {
  if (y < 0) return 0;
  if (y > n - 1) return n - 1;
  return y;
}

// Tile2Lon and Tile2Lat - delegate to tile_math.h
double Tile2Lon(int x, int z) { return earth::Tile2Lon(x, z); }
double Tile2Lat(int y, int z) { return earth::Tile2Lat(y, z); }

std::pair<int, int> GeoToTileXY(double latDeg, double lonDeg, int z) {
  const double latRad = glm::radians(latDeg);
  const double n = static_cast<double>(1 << z);
  const double x = (lonDeg + 180.0) / 360.0 * n;
  const double y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n;
  return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

glm::vec3 TilePointToSphere(const vtzero::point& p,
                            int extent,
                            int z,
                            int x,
                            int y) {
  double lonLeft = Tile2Lon(x, z);
  double lonRight = Tile2Lon(x + 1, z);
  double latTop = Tile2Lat(y, z);
  double latBottom = Tile2Lat(y + 1, z);

  double u = static_cast<double>(p.x) / static_cast<double>(extent);
  double v = static_cast<double>(p.y) / static_cast<double>(extent);

  double lon = lonLeft + (lonRight - lonLeft) * u;
  double lat = latTop + (latBottom - latTop) * v;

  double latRad = glm::radians(lat);
  double lonRad = glm::radians(lon);
  glm::vec3 out;
  out.x = static_cast<float>(std::cos(latRad) * std::cos(lonRad));
  out.y = static_cast<float>(std::cos(latRad) * std::sin(lonRad));
  out.z = static_cast<float>(std::sin(latRad));
  return out;
}

// Tile geometry functions - delegating to shared tile_math.h
// These wrappers maintain backward compatibility with existing call sites
glm::vec3 TileCenterNormal(int z, int x, int y) {
  // Returns world coordinates (scaled by GLOBE_RADIUS)
  return earth::TileCenterWorld(z, x, y);
}

float TileAngularRadius(int z, int x, int y) {
  return earth::TileAngularRadius(z, x, y);
}

float TileBoundingRadius(int z, int x, int y) {
  return earth::TileBoundingRadius(z, x, y);
}

// Helper to compute sphere position from lat/lon (scaled by radius for main.js parity)
glm::vec3 LatLonToSphere(double latRad, double lonRad, double radius) {
  return glm::vec3(
    static_cast<float>(std::cos(latRad) * std::cos(lonRad) * radius),
    static_cast<float>(std::cos(latRad) * std::sin(lonRad) * radius),
    static_cast<float>(std::sin(latRad) * radius)
  );
}

glm::vec3 LatLonToSphere(double latRad, double lonRad) {
  return LatLonToSphere(latRad, lonRad, GLOBE_RADIUS);
}

// ComputeGeometricError now in tile_math.h (earth::ComputeGeometricError)

// Build tile mesh - now uses modular TileMeshBuilder
// This is a wrapper for backward compatibility
TileMesh BuildTileMesh(int x, int y, int z, int segments, int edgeFlags,
                       const HeightSampler* heightSampler,
                       bool* outDemUsed,
                       bool* outDemPending,
                       bool debug = false) {
  // Use static builder instance for efficiency
  static earth::TileMeshBuilder builder;
  
  // Configure segments if different
  auto config = builder.GetConfig();
  if (config.segments != segments) {
    config.segments = segments;
    builder.SetConfig(config);
  }
  
  // Convert HeightSampler to earth::HeightSampler if provided
  earth::HeightSampler earthSampler = nullptr;
  if (heightSampler && *heightSampler) {
    earthSampler = *heightSampler;
  }
  
  // Build mesh using modular builder
  earth::MeshData data = builder.Build(x, y, z, edgeFlags, 
                                        earthSampler ? &earthSampler : nullptr);
  
  // Output DEM status
  if (outDemUsed) *outDemUsed = data.demUsed;
  if (outDemPending) *outDemPending = data.demPending;
  
  // Debug output
  if (data.demUsed && debug) {
    fprintf(stderr, "[Mesh] Tile %d/%d/%d (seg=%d): H[%.2f, %.2f]\n", 
            z, x, y, segments, data.minHeight, data.maxHeight);
  }
  
  // Upload to GPU and return
  return earth::TileMeshBuilder::UploadToGPU(data);
}

// Compute edge flags for a tile based on visible tile set
// P0 Fix: Check multiple ancestor levels (not just parent) to handle 2+ LOD differences
int ComputeEdgeFlags(int z, int x, int y, const std::unordered_set<std::string>& availableKeys, bool debug = false) {
  int flags = EDGE_NONE;
  int n = 1 << z;
  
  // Check each neighbor - if neighbor at same zoom doesn't exist but ANY ancestor does,
  // this edge needs stitching
  auto checkNeighbor = [&](int nx, int ny, int edgeFlag) {
    nx = (nx + n) % n;  // Wrap X
    if (ny < 0 || ny >= n) return;  // Clamp Y
    
    std::string neighborKey = MakeTileKey(z, nx, ny);
    if (availableKeys.find(neighborKey) != availableKeys.end()) return;

    // Neighbor not visible at same zoom - check its ancestors up to 3 levels
    const int maxLevels = std::min(3, z);
    for (int level = 1; level <= maxLevels; ++level) {
      int ancestorZ = z - level;
      int ancestorX = nx >> level;
      int ancestorY = ny >> level;
      std::string ancestorKey = MakeTileKey(ancestorZ, ancestorX, ancestorY);
      if (availableKeys.find(ancestorKey) != availableKeys.end()) {
        flags |= edgeFlag;
        break;  // Found an ancestor, no need to check further
      }
    }
  };
  
  checkNeighbor(x - 1, y, EDGE_LEFT);
  checkNeighbor(x + 1, y, EDGE_RIGHT);
  checkNeighbor(x, y - 1, EDGE_TOP);
  checkNeighbor(x, y + 1, EDGE_BOTTOM);
  
  if (flags != EDGE_NONE && debug) {
      fprintf(stderr, "[Stitch] Tile %d/%d/%d Flags: %d\n", z, x, y, flags);
  }
  
  return flags;
}

// HS-style pole mesh generation for north/south poles
// Creates a fan mesh from the pole to the edge latitude
PoleMesh BuildPoleMesh(bool isNorth, int segments = 32) {
  const double poleLatDeg = isNorth ? 85.05112878 : -85.05112878;  // Web Mercator limit
  const double poleLat = glm::radians(poleLatDeg);
  const double poleCapLat = isNorth ? glm::radians(90.0) : glm::radians(-90.0);
  
  // Use slightly smaller radius to render behind tiles (avoid z-fighting)
  const double poleRadius = GLOBE_RADIUS * 0.9999;
  
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  
  // Helper lambda for pole vertices (uses poleRadius instead of GLOBE_RADIUS)
  auto latLonToPole = [poleRadius](double latRad, double lonRad) -> glm::vec3 {
    return glm::vec3(
      static_cast<float>(std::cos(latRad) * std::cos(lonRad) * poleRadius),
      static_cast<float>(std::cos(latRad) * std::sin(lonRad) * poleRadius),
      static_cast<float>(std::sin(latRad) * poleRadius)
    );
  };
  
  // Center vertex at the pole
  glm::vec3 polePos = latLonToPole(poleCapLat, 0.0);
  vertices.push_back(polePos.x);
  vertices.push_back(polePos.y);
  vertices.push_back(polePos.z);
  vertices.push_back(0.5f);  // UV center
  vertices.push_back(isNorth ? 0.0f : 1.0f);
  
  // Ring of vertices at edge latitude
  for (int i = 0; i <= segments; ++i) {
    double lon = glm::radians(static_cast<double>(i) / segments * 360.0 - 180.0);
    glm::vec3 pos = latLonToPole(poleLat, lon);
    vertices.push_back(pos.x);
    vertices.push_back(pos.y);
    vertices.push_back(pos.z);
    float u = static_cast<float>(i) / segments;
    float v = isNorth ? 1.0f : 0.0f;
    vertices.push_back(u);
    vertices.push_back(v);
  }
  
  // Fan triangles from pole to edge.
  // Match tile mesh winding to avoid cull removal under GL_BACK.
  for (int i = 0; i < segments; ++i) {
    if (isNorth) {
      indices.push_back(0);           // Pole center
      indices.push_back(i + 1);       // Current edge vertex
      indices.push_back(i + 2);       // Next edge vertex
    } else {
      indices.push_back(0);           // Pole center
      indices.push_back(i + 2);       // Next edge vertex
      indices.push_back(i + 1);       // Current edge vertex
    }
  }
  
  PoleMesh mesh;
  glGenVertexArrays(1, &mesh.vao);
  glGenBuffers(1, &mesh.vbo);
  glGenBuffers(1, &mesh.ebo);
  
  glBindVertexArray(mesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
  
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
  
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
  
  glBindVertexArray(0);
  mesh.indexCount = static_cast<GLsizei>(indices.size());
  mesh.initialized = true;
  return mesh;
}

// AtmosphereMesh and BuildAtmosphereMesh removed


GLuint CompileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::cerr << "Shader compile error: " << log << std::endl;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint CreateProgram(const char* vs, const char* fs) {
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
  GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
  if (!v || !f) {
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    return 0;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, v);
  glAttachShader(program, f);
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::cerr << "Program link error: " << log << std::endl;
    glDeleteProgram(program);
    program = 0;
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return program;
}

bool ProjectToScreen(const glm::mat4& mvp,
                     const glm::vec3& world,
                     int width,
                     int height,
                     glm::vec2& out) {
  glm::vec4 clip = mvp * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0f) {
    return false;
  }
  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  out.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
  out.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
  return true;
}

bool ComputeScreenRadius(const glm::mat4& mvp,
                         const glm::vec3& center,
                         float radius,
                         int width,
                         int height,
                         float& outPx) {
  glm::vec2 c2;
  if (!ProjectToScreen(mvp, center, width, height, c2)) {
    return false;
  }
  glm::vec3 up = (std::abs(center.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 tangent = glm::normalize(glm::cross(up, center));
  glm::vec3 edge = center + tangent * radius;
  glm::vec2 e2;
  if (!ProjectToScreen(mvp, edge, width, height, e2)) {
    return false;
  }
  outPx = glm::length(e2 - c2);
  return true;
}

// ComputeGeometricError and ComputeGeometricSSE moved to tile_math.h
// Use earth::ComputeGeometricError() and earth::ComputeGeometricSSE()

// Google Earth style tile importance score for smart cache eviction
// Higher score = more important = less likely to evict
float ComputeTileImportanceScore(const Tile& tile, double currentTime, 
                                  const glm::vec3& cameraPos, int currentZoom) {
  float score = 0.0f;
  
  // Pinned tiles have highest importance
  if (tile.pinned) return 10000.0f;
  
  // Access count bonus (frequently accessed tiles are important)
  score += tile.accessCount * 2.0f;
  
  // Recency bonus (recently used tiles are important)
  double age = currentTime - tile.lastFrameUsed * 0.016;  // Approximate time from frame
  float ageFactor = std::max(0.0f, 10.0f - static_cast<float>(age));
  score += ageFactor * 1.0f;
  
  // Distance penalty (far tiles are less important)
  float distance = glm::length(tile.center - cameraPos);
  float distanceFactor = 1.0f / (1.0f + distance * 0.001f);
  score += distanceFactor * 3.0f;
  
  // Level bonus (tiles near current zoom are more important)
  int levelDiff = std::abs(tile.z - currentZoom);
  float levelBonus = std::max(0.0f, 5.0f - levelDiff);
  score += levelBonus;
  
  return score;
}

float ComputeTileSSE(const glm::vec3& cameraPos, int viewportHeight, float fovRad, int z, int x, int y) {
  glm::vec3 center = TileCenterNormal(z, x, y);
  float radius = TileBoundingRadius(z, x, y);
  float distance = glm::length(center - cameraPos);
  
  // Distance to closest point on bounding sphere
  distance = std::max(1.0f, distance - radius);
  
  // Phase 3 Fix: Convert normalized distance to meters
  double distanceMeters = static_cast<double>(distance) / GLOBE_RADIUS_K;
  
  return earth::ComputeGeometricSSE(z, distanceMeters, viewportHeight, glm::degrees(fovRad));
}

float ComputeTileSseRatio(const glm::vec3& cameraPos, int viewportHeight, float fovRad,
                          int z, int x, int y, float sseThreshold,
                          float tiltFactor) {
  float ssePx = ComputeTileSSE(cameraPos, viewportHeight, fovRad, z, x, y);
  
  // Phase 3 Fix: Compare geometric error directly to threshold ratio
  // If sseThreshold is 1.4, we subdivide if error > 1.4 pixels.
  // Note: sseThresholdPx in config is typically 1.4.
  // JS Parity: Apply tilt factor (1 - tilt/150).
  // Reduces SSE ratio at high tilt to prevent over-subdivision near horizon.
  return (ssePx * tiltFactor) / sseThreshold;
}

float ComputeTileLevelFloat(float ratio) {
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }
  ratio = (ratio - 0.44f) * (1.0f / 0.56f);
  if (ratio > 1.0f) {
    ratio = 1.0f;
  }
  if (ratio < 0.0f) {
    ratio = 0.0f;
  }
  return ratio;
}

// ShouldSubdivideTile removed - logic now in TileLodSelector::CalculateSSE()

int EstimateZoomFromView(const glm::mat4& mvp,
                         const glm::vec3& viewCenter,
                         int width,
                         int height,
                         int minZoom,
                         int maxZoom) {
  const float targetPx = 256.0f;
  int bestZoom = minZoom;
  float bestErr = 1e9f;
  
  // Use actual view center latitude
  double centerLat = glm::degrees(std::asin(static_cast<double>(viewCenter.y)));
  double centerLon = glm::degrees(std::atan2(static_cast<double>(viewCenter.z), 
                                             static_cast<double>(viewCenter.x)));
  double latRad = glm::radians(centerLat);

  for (int z = minZoom; z <= maxZoom; ++z) {
    double tileDeg = 360.0 / static_cast<double>(1 << z);
    double lon0 = centerLon;
    double lon1 = centerLon + tileDeg;
    glm::vec3 p0;
    glm::vec3 p1;
    p0.x = static_cast<float>(std::cos(latRad) * std::cos(glm::radians(lon0)) * GLOBE_RADIUS);
    p0.y = static_cast<float>(std::cos(latRad) * std::sin(glm::radians(lon0)) * GLOBE_RADIUS);
    p0.z = static_cast<float>(std::sin(latRad) * GLOBE_RADIUS);
    p1.x = static_cast<float>(std::cos(latRad) * std::cos(glm::radians(lon1)) * GLOBE_RADIUS);
    p1.y = static_cast<float>(std::cos(latRad) * std::sin(glm::radians(lon1)) * GLOBE_RADIUS);
    p1.z = static_cast<float>(std::sin(latRad) * GLOBE_RADIUS);

    glm::vec2 s0, s1;
    if (!ProjectToScreen(mvp, p0, width, height, s0) ||
        !ProjectToScreen(mvp, p1, width, height, s1)) {
      continue;
    }
    float px = glm::length(s1 - s0);
    float err = std::abs(px - targetPx);
    if (err < bestErr) {
      bestErr = err;
      bestZoom = z;
    }
  }
  return bestZoom;
}

double EstimateZoomExact(const glm::mat4& mvp,
                         const glm::vec3& viewCenter,
                         int width,
                         int height,
                         int minZoom,
                         int maxZoom) {
  const double targetPx = 256.0;
  // viewCenter is normalized (from ScreenToGeo or similar)
  double centerLat = glm::degrees(std::asin(static_cast<double>(viewCenter.z)));
  double centerLon = glm::degrees(std::atan2(static_cast<double>(viewCenter.y),
                                             static_cast<double>(viewCenter.x)));
  double latRad = glm::radians(centerLat);

  double lon0 = centerLon;
  double lon1 = centerLon + 1.0;  // 1 degree step
  glm::vec3 p0;
  glm::vec3 p1;
  p0.x = static_cast<float>(std::cos(latRad) * std::cos(glm::radians(lon0)) * GLOBE_RADIUS);
  p0.y = static_cast<float>(std::cos(latRad) * std::sin(glm::radians(lon0)) * GLOBE_RADIUS);
  p0.z = static_cast<float>(std::sin(latRad) * GLOBE_RADIUS);
  p1.x = static_cast<float>(std::cos(latRad) * std::cos(glm::radians(lon1)) * GLOBE_RADIUS);
  p1.y = static_cast<float>(std::cos(latRad) * std::sin(glm::radians(lon1)) * GLOBE_RADIUS);
  p1.z = static_cast<float>(std::sin(latRad) * GLOBE_RADIUS);

  glm::vec2 s0, s1;
  if (!ProjectToScreen(mvp, p0, width, height, s0) ||
      !ProjectToScreen(mvp, p1, width, height, s1)) {
    return static_cast<double>(minZoom);
  }

  double pxPerDeg = glm::length(s1 - s0);
  if (pxPerDeg <= 1e-6) {
    return static_cast<double>(minZoom);
  }

  double z = std::log2((pxPerDeg * 360.0) / targetPx);
  if (z < minZoom) z = minZoom;
  if (z > maxZoom) z = maxZoom;
  return z;
}

// Helper for deferred deletion
struct DeferredQueue {
    std::vector<GLuint> textures;
    std::vector<GLuint> buffers;
    std::vector<GLuint> vaos;
};

void DestroyTile(Tile& tile, DeferredQueue* queue) {
  if (tile.texture && tile.ownsTexture) {
    if (queue) {
        queue->textures.push_back(tile.texture);
    } else {
        glDeleteTextures(1, &tile.texture);
    }
  }
  tile.texture = 0;
  tile.ownsTexture = false;
  
  if (tile.mesh.vbo) {
    if (queue) queue->buffers.push_back(tile.mesh.vbo);
    else glDeleteBuffers(1, &tile.mesh.vbo);
    tile.mesh.vbo = 0;
  }
  if (tile.mesh.ebo) {
    if (queue) queue->buffers.push_back(tile.mesh.ebo);
    else glDeleteBuffers(1, &tile.mesh.ebo);
    tile.mesh.ebo = 0;
  }
  if (tile.mesh.vao) {
    if (queue) queue->vaos.push_back(tile.mesh.vao);
    else glDeleteVertexArrays(1, &tile.mesh.vao);
    tile.mesh.vao = 0;
  }
}

void DestroyVectorTile(VectorTile& tile, DeferredQueue* queue) {
  if (tile.lineVbo) {
    if (queue) queue->buffers.push_back(tile.lineVbo);
    else glDeleteBuffers(1, &tile.lineVbo);
    tile.lineVbo = 0;
  }
  if (tile.lineVao) {
    if (queue) queue->vaos.push_back(tile.lineVao);
    else glDeleteVertexArrays(1, &tile.lineVao);
    tile.lineVao = 0;
  }
  if (tile.pointVbo) {
    if (queue) queue->buffers.push_back(tile.pointVbo);
    else glDeleteBuffers(1, &tile.pointVbo);
    tile.pointVbo = 0;
  }
  if (tile.pointVao) {
    if (queue) queue->vaos.push_back(tile.pointVao);
    else glDeleteVertexArrays(1, &tile.pointVao);
    tile.pointVao = 0;
  }
  if (tile.fillVbo) {
    if (queue) queue->buffers.push_back(tile.fillVbo);
    else glDeleteBuffers(1, &tile.fillVbo);
    tile.fillVbo = 0;
  }
  if (tile.fillVao) {
    if (queue) queue->vaos.push_back(tile.fillVao);
    else glDeleteVertexArrays(1, &tile.fillVao);
    tile.fillVao = 0;
  }
  tile.lineVertexCount = 0;
  tile.pointVertexCount = 0;
  tile.fillVertexCount = 0;
}

std::string BuildTileUrl(const std::string& baseUrl, int z, int x, int y) {
  TileUrlGenerator gen(baseUrl);
  return gen.GenerateUrl(TileKey(z, x, y));
}

// Convert tile coordinates to Web Mercator bounding box (EPSG:3857)
void TileToBBox3857(int z, int x, int y, double& minX, double& minY, double& maxX, double& maxY) {
  const double earthRadius = 6378137.0;
  const double originShift = 2.0 * glm::pi<double>() * earthRadius / 2.0;
  double tileSize = 2.0 * originShift / (1 << z);
  
  minX = -originShift + x * tileSize;
  maxX = minX + tileSize;
  maxY = originShift - y * tileSize;
  minY = maxY - tileSize;
}

// Convert tile coordinates to geographic bounding box (EPSG:4326)
void TileToBBox4326(int z, int x, int y, double& minLon, double& minLat, double& maxLon, double& maxLat) {
  int n = 1 << z;
  minLon = x * 360.0 / n - 180.0;
  maxLon = (x + 1) * 360.0 / n - 180.0;
  
  double latRadMax = std::atan(std::sinh(glm::pi<double>() * (1.0 - 2.0 * y / n)));
  double latRadMin = std::atan(std::sinh(glm::pi<double>() * (1.0 - 2.0 * (y + 1) / n)));
  maxLat = glm::degrees(latRadMax);
  minLat = glm::degrees(latRadMin);
}

// Build WMS GetMap URL from tile coordinates
std::string BuildWMSUrl(const std::string& baseUrl, const WMSConfig& wms, int z, int x, int y) {
  std::string url = baseUrl;
  
  // Ensure base URL has proper separator
  if (url.find('?') == std::string::npos) {
    url += "?";
  } else if (url.back() != '&' && url.back() != '?') {
    url += "&";
  }
  
  // Get bounding box based on SRS
  std::string bbox;
  if (wms.srs == "EPSG:4326" || wms.srs == "CRS:84") {
    double minLon, minLat, maxLon, maxLat;
    TileToBBox4326(z, x, y, minLon, minLat, maxLon, maxLat);
    char buf[256];
    if (wms.version == "1.3.0" && wms.srs == "EPSG:4326") {
      // WMS 1.3.0 uses lat,lon order for EPSG:4326
      std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f,%.6f", minLat, minLon, maxLat, maxLon);
    } else {
      std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f,%.6f", minLon, minLat, maxLon, maxLat);
    }
    bbox = buf;
  } else {
    // Default to EPSG:3857 (Web Mercator)
    double minX, minY, maxX, maxY;
    TileToBBox3857(z, x, y, minX, minY, maxX, maxY);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f,%.2f", minX, minY, maxX, maxY);
    bbox = buf;
  }
  
  // Build query parameters
  url += "SERVICE=WMS&REQUEST=GetMap";
  url += "&VERSION=" + wms.version;
  url += "&LAYERS=" + wms.layers;
  url += "&STYLES=" + wms.styles;
  url += "&FORMAT=" + wms.format;
  url += "&WIDTH=" + std::to_string(wms.width);
  url += "&HEIGHT=" + std::to_string(wms.height);
  url += "&BBOX=" + bbox;
  
  // SRS vs CRS depending on version
  if (wms.version == "1.3.0") {
    url += "&CRS=" + wms.srs;
  } else {
    url += "&SRS=" + wms.srs;
  }
  
  if (wms.transparent) {
    url += "&TRANSPARENT=TRUE";
  }
  
  return url;
}

void CreateVectorBuffer(GLuint& vao, GLuint& vbo, const std::vector<glm::vec3>& verts) {
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
  glBindVertexArray(0);
}

Plane NormalizePlane(const glm::vec4& p) {
  Plane plane;
  plane.normal = glm::vec3(p);
  plane.d = p.w;
  float len = glm::length(plane.normal);
  if (len > 0.0f) {
    plane.normal /= len;
    plane.d /= len;
  }
  return plane;
}

std::array<Plane, 6> ExtractFrustumPlanes(const glm::mat4& m, const glm::vec3& insidePoint) {
  std::array<Plane, 6> planes;
  // GLM uses column-major storage, so m[col][row]
  // Extract rows for Gribb/Hartmann method
  glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
  glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
  glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
  glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
  
  // Gribb/Hartmann frustum plane extraction
  planes[0] = NormalizePlane(row3 + row0);  // left
  planes[1] = NormalizePlane(row3 - row0);  // right
  planes[2] = NormalizePlane(row3 + row1);  // bottom
  planes[3] = NormalizePlane(row3 - row1);  // top
  planes[4] = NormalizePlane(row3 + row2);  // near
  planes[5] = NormalizePlane(row3 - row2);  // far

  // Ensure plane normals point inward based on a known inside point
  for (auto& plane : planes) {
    float dist = glm::dot(plane.normal, insidePoint) + plane.d;
    if (dist < 0.0f) {
      plane.normal = -plane.normal;
      plane.d = -plane.d;
    }
  }
  return planes;
}

bool SphereInFrustum(const std::array<Plane, 6>& planes, const glm::vec3& center, float radius) {
  for (const auto& plane : planes) {
    float dist = glm::dot(plane.normal, center) + plane.d;
    // Gribb/Hartmann: normals point inward, negative distance = outside
    if (dist < -radius) {
      return false;
    }
  }
  return true;
}

struct VectorGeometryCollector {
  std::vector<glm::vec3>& lineVerts;
  std::vector<glm::vec3>& pointVerts;
  std::vector<glm::vec3>& fillVerts;
  int extent = 4096;
  int z = 0;
  int x = 0;
  int y = 0;
  bool hasLast = false;
  bool hasFirst = false;
  glm::vec3 last = {};
  glm::vec3 first = {};
  std::vector<glm::vec3> ringPoints;
  using Point2 = std::array<double, 2>;
  std::vector<Point2> ringPoints2D;
  std::vector<std::vector<Point2>> currentPolygon;
  std::vector<std::vector<std::vector<Point2>>> polygons;

  glm::vec3 Convert(const vtzero::point& p) {
    return TilePointToSphere(p, extent, z, x, y);
  }

  glm::vec3 Convert2D(const Point2& p) {
    vtzero::point vp{static_cast<int32_t>(p[0]), static_cast<int32_t>(p[1])};
    return TilePointToSphere(vp, extent, z, x, y);
  }

  void BeginFeature() {
    currentPolygon.clear();
    polygons.clear();
  }

  void EndFeature() {
    if (!currentPolygon.empty()) {
      polygons.push_back(currentPolygon);
      currentPolygon.clear();
    }
    for (const auto& polygon : polygons) {
      if (polygon.empty()) {
        continue;
      }
      std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);
      if (indices.empty()) {
        continue;
      }
      std::vector<glm::vec3> flatPoints;
      size_t total = 0;
      for (const auto& ring : polygon) {
        total += ring.size();
      }
      flatPoints.reserve(total);
      for (const auto& ring : polygon) {
        for (const auto& p : ring) {
          flatPoints.push_back(Convert2D(p));
        }
      }
      for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        fillVerts.push_back(flatPoints[indices[i]]);
        fillVerts.push_back(flatPoints[indices[i + 1]]);
        fillVerts.push_back(flatPoints[indices[i + 2]]);
      }
    }
    polygons.clear();
  }

  void points_begin(uint32_t) {}
  void points_point(vtzero::point p) {
    pointVerts.push_back(Convert(p));
  }
  void points_end() {}

  void linestring_begin(uint32_t) {
    hasLast = false;
  }
  void linestring_point(vtzero::point p) {
    glm::vec3 v = Convert(p);
    if (hasLast) {
      lineVerts.push_back(last);
      lineVerts.push_back(v);
    }
    last = v;
    hasLast = true;
  }
  void linestring_end() {}

  void ring_begin(uint32_t) {
    hasLast = false;
    hasFirst = false;
    ringPoints.clear();
    ringPoints2D.clear();
  }
  void ring_point(vtzero::point p) {
    glm::vec3 v = Convert(p);
    ringPoints.push_back(v);
    ringPoints2D.push_back(Point2{static_cast<double>(p.x), static_cast<double>(p.y)});
    if (!hasFirst) {
      first = v;
      hasFirst = true;
    }
    if (hasLast) {
      lineVerts.push_back(last);
      lineVerts.push_back(v);
    }
    last = v;
    hasLast = true;
  }
  void ring_end(vtzero::ring_type type) {
    if (hasLast && hasFirst) {
      lineVerts.push_back(last);
      lineVerts.push_back(first);
    }
    if (ringPoints2D.size() >= 3) {
      const auto& first2 = ringPoints2D.front();
      const auto& last2 = ringPoints2D.back();
      if (first2[0] == last2[0] && first2[1] == last2[1]) {
        ringPoints2D.pop_back();
      }
    }
    if (ringPoints2D.size() >= 3) {
      if (type == vtzero::ring_type::outer) {
        if (!currentPolygon.empty()) {
          polygons.push_back(currentPolygon);
          currentPolygon.clear();
        }
        currentPolygon.push_back(ringPoints2D);
      } else if (type == vtzero::ring_type::inner) {
        if (!currentPolygon.empty()) {
          currentPolygon.push_back(ringPoints2D);
        }
      }
    }
    hasLast = false;
    hasFirst = false;
  }

  void ring_end(int64_t area) {
    if (area > 0) {
      ring_end(vtzero::ring_type::outer);
    } else if (area < 0) {
      ring_end(vtzero::ring_type::inner);
    } else {
      ring_end(vtzero::ring_type::invalid);
    }
  }
};

// Fog density code removed


// Estimate tile memory usage in bytes (accurate accounting)
size_t EstimateTileBytes(const Tile& tile) {
  size_t bytes = sizeof(Tile);
  
  // Texture memory: use actual decoded dimensions if available
  if (tile.ownsTexture && tile.texture != 0) {
    int texWidth = tile.decodedWidth > 0 ? tile.decodedWidth : 256;
    int texHeight = tile.decodedHeight > 0 ? tile.decodedHeight : 256;
    // Base texture + mipmaps (~1.33x for full mipmap chain)
    bytes += static_cast<size_t>(texWidth * texHeight * 4 * 1.33);
  }
  
  // Pending decoded data (RAM, waiting for GPU upload)
  bytes += tile.decodedData.size();
  
  // Support tile data
  bytes += tile.supportMainPixels.size();
  
  // Mesh GPU memory (vertices + indices)
  if (tile.mesh.vao != 0) {
    // More accurate: vertexCount * stride + indexCount * sizeof(uint32_t)
    // Vertex stride: position(3) + normal(3) + uv(2) = 8 floats = 32 bytes
    size_t vertexCount = tile.mesh.indexCount / 3 * 2;  // Rough estimate
    bytes += vertexCount * 32;  // Vertex data
    bytes += tile.mesh.indexCount * sizeof(uint32_t);  // Index data
  }
  
  // String allocations
  bytes += tile.layerId.capacity();
  bytes += tile.fallbackAncestorKey.capacity();
  
  return bytes;
}

std::filesystem::path GetCachePath(const GlobeConfig& config, const std::string& urlTemplate, int z, int x, int y) {
  uint64_t key = HashFnv1a64(urlTemplate);
  std::filesystem::path base = config.cacheDir;
  std::filesystem::path dir = base / HashToHex(key) / std::to_string(z) / std::to_string(x);
  std::filesystem::path file = dir / (std::to_string(y) + ".bin");
  return file;
}

bool LoadTileData(const GlobeConfig& config, const std::string& urlTemplate, int z, int x, int y, std::vector<unsigned char>& out) {
  if (config.useDiskCache) {
    std::filesystem::path cached = GetCachePath(config, urlTemplate, z, x, y);
    if (ReadFile(cached, out)) {
      return true;
    }
  }
  std::string url = BuildTileUrl(urlTemplate, z, x, y);
  
  // Add Referer/Origin headers for authentication (some tile servers require this)
  std::vector<std::string> headers;
  std::string origin = ExtractOrigin(url);
  if (!origin.empty()) {
    headers.push_back("Origin: " + origin);
    headers.push_back("Referer: " + origin + "/");
  }
  
  if (!DownloadUrl(url, out, DOWNLOAD_TIMEOUT_SECONDS, headers)) {
    return false;
  }
  if (config.useDiskCache) {
    std::filesystem::path cached = GetCachePath(config, urlTemplate, z, x, y);
    WriteFile(cached, out);
  }
  return true;
}

// EnqueueDownload - Thread-safe download job enqueue
// LOCK HIERARCHY: Acquires downloadMutex first, then pendingMutex (Level 2 → Level 3)
// Caller must NOT hold pendingMutex when calling this function!
void EnqueueDownload(std::unordered_set<std::string>& pending,
                     std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue,
                     std::mutex& downloadMutex,
                     std::condition_variable& cv,
                     DownloadJob job,
                     std::mutex* pendingMutex = nullptr) {
  std::string key = MakeTileKey(job.z, job.x, job.y);
  job.queueTime = glfwGetTime();
  
  // Acquire locks in hierarchy order: downloadMutex (L2) → pendingMutex (L3)
  std::lock_guard<std::mutex> dlLock(downloadMutex);
  
  if (pendingMutex) {
    std::lock_guard<std::mutex> pendLock(*pendingMutex);
    if (pending.find(key) != pending.end()) {
      return;
    }
    pending.insert(key);
  } else {
    // Caller guarantees thread safety for pending set
    if (pending.find(key) != pending.end()) {
      return;
    }
    pending.insert(key);
  }
  
  queue.push(std::move(job));
  cv.notify_one();
}

// ============================================================================
// LEGACY TILE SELECTION REMOVED (2026-02-04)
// The following functions were removed as they are now replaced by TileLodSelector:
// - IsTileReady(), AreChildrenReady()
// - CollectVisibleTilesRecursive(), CollectVisibleTilesRecursiveFixed()
// - BuildVisibleTileSets(), BuildVisibleTileSetsFixed()
// See tile_lod_selector.h for the modern implementation.
// ============================================================================

// Sync tiles for a specific raster layer
void SyncLayerTiles(DeferredQueue* queue,
                    RasterLayerTiles& layerData,
                    const RasterLayerConfig& layerConfig,
                    GLuint loadingTexture,
                    const std::unordered_set<std::string>& required,
                    const std::vector<std::string>& leaves,
                    TileScheduler* scheduler,
                    int segments,
                    const HeightSampler* heightSampler,
                    int currentZoom,
                    std::unordered_map<std::string, std::unordered_set<std::string>>& pendingLayerDownloads,
                    size_t& meshRebuilds,
                    size_t& queueSize,
                    size_t& textureUploads,
                    size_t maxUploads,
                    size_t maxRebuilds,
                    std::mutex* pendingMutex = nullptr,
                    bool debug = false,
                    const glm::vec3* cameraPos = nullptr,
                    const glm::mat4* mvp = nullptr) {
  if (!layerConfig.visible) {
    layerData.visibleTiles.clear();
    return;
  }
  if (currentZoom < layerConfig.minZoom || currentZoom > layerConfig.maxZoom) {
    layerData.visibleTiles.clear();
    return;
  }

  std::unordered_set<std::string> leafSet(leaves.begin(), leaves.end());
  const bool canSampleDem = heightSampler && *heightSampler;
  const double now = canSampleDem ? glfwGetTime() : 0.0;

  // Precompute required keys and ancestors for optimization
  std::vector<std::pair<TileKey, std::string>> orderedRequired;
  orderedRequired.reserve(required.size());
  std::unordered_set<std::string> requiredAncestors;
  
  for (const auto& key : required) {
      TileKey k = TileKey::FromString(key);
      orderedRequired.emplace_back(k, key);
      
      // Populate ancestors
      TileKey parent = k;
      while (parent.level > 0) {
          parent = parent.GetParent();
          requiredAncestors.insert(parent.ToString());
      }
  }

  for (const auto& item : orderedRequired) {
    const std::string& key = item.second;
    const TileKey& k = item.first;
    
    auto existing = layerData.tiles.find(key);
    if (existing == layerData.tiles.end()) {
      Tile tile(k.level, k.x, k.y);
      tile.texture = loadingTexture;
      tile.ownsTexture = false;
      tile.center = TileCenterNormal(k.level, k.x, k.y);
      tile.angularRadius = TileAngularRadius(k.level, k.x, k.y);
      tile.radius = TileBoundingRadius(k.level, k.x, k.y);
      tile.fade = 0.0f;
      layerData.tiles.emplace(key, std::move(tile));
      existing = layerData.tiles.find(key);
    }

    if (scheduler && !existing->second.ownsTexture) {
        Tile& tile = existing->second;
        
        // OPTIMIZATION: Only schedule LEAF tiles to avoid bandwidth waste
        bool isLeaf = leafSet.count(key) > 0;
        
        if (isLeaf) {
            LoadPriority priority = LoadPriority::URGENT;

            bool shouldSchedule = (tile.loadState == TileLoadState::UNLOADED || 
                                   tile.loadState == TileLoadState::FAILED);
        
        // Promotion check
        if (tile.loadState == TileLoadState::SCHEDULED) {
            if (priority < tile.loadPriority) {
                shouldSchedule = true;
            }
        }

        if (shouldSchedule) {
             bool canRetry = true;
             if (tile.loadState == TileLoadState::FAILED) {
                 if (layerConfig.supportEmptyContent && !tile.supportPending && !layerConfig.supportUrl.empty()) {
                     tile.loadState = TileLoadState::UNLOADED;
                     tile.supportPending = true;
                     tile.supportMode = SupportMode::EMPTY_CONTENT;
                     canRetry = false;
                 } else if (tile.retryCount >= 3) {
                     canRetry = false;
                 }
             }

             if (canRetry) {
                 tile.loadPriority = priority;
                 TaskParams params;
                 
                 if (tile.supportPending && !layerConfig.supportUrl.empty()) {
                     params.urlTemplate = layerConfig.supportUrl;
                 } else {
                     if (layerConfig.type == RasterLayerType::WMS) {
                       params.urlTemplate = BuildWMSUrl(layerConfig.url, layerConfig.wms, 
                                                      tile.z, tile.x, tile.y);
                     } else if (layerConfig.type == RasterLayerType::TMS) {
                       int tmsY = (1 << tile.z) - 1 - tile.y;
                       params.urlTemplate = BuildTileUrl(layerConfig.url, tile.z, tile.x, tmsY);
                     } else {
                       params.urlTemplate = layerConfig.url;
                     }
                 }
                 params.layerId = layerConfig.id;
                 
                 // Google Earth style priority scoring for overlay layers
                 if (cameraPos) {
                   float score = 0.0f;
                   float distance = glm::length(tile.center - *cameraPos);
                   float maxDist = 3.0f * GLOBE_RADIUS;
                   score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
                   if (mvp) {
                     glm::vec4 clipPos = (*mvp) * glm::vec4(tile.center, 1.0f);
                     if (clipPos.w > 0.001f) {
                       glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
                       float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
                       score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
                     }
                   }
                   int levelDiff = std::abs(tile.z - currentZoom);
                   score += std::max(0.0f, 5.0f - levelDiff) * 4.0f;
                   if (leafSet.count(key) > 0) score += 10.0f;
                   params.priorityScore = score;
                 }
                 
                 scheduler->Schedule(&tile, params);
             }
            }
        }  // end if (isLeaf)
        
        // Process texture upload for all tiles (leaf and non-leaf)
        if (tile.loadState == TileLoadState::READY) {
             if (!tile.decodedData.empty() && textureUploads < maxUploads) {
                 bool needsSupport = false;
                 if (!tile.supportPending && (layerConfig.supportTransparentPixel || layerConfig.supportOutOfBBOX)) {
                     bool anyTrans = false, allTrans = false;
                     if (tile.decodedWidth > 0) {
                         AnalyzeAlpha(tile.decodedData, anyTrans, allTrans);
                     }
                     if (layerConfig.supportTransparentPixel && anyTrans) needsSupport = true;
                     if (layerConfig.supportOutOfBBOX && allTrans) needsSupport = true;
                 }
                 
                 if (needsSupport && !layerConfig.supportUrl.empty()) {
                     tile.loadState = TileLoadState::UNLOADED;
                     tile.supportPending = true;
                     tile.supportMainPixels = std::move(tile.decodedData);
                     tile.supportMainWidth = tile.decodedWidth;
                     tile.supportMainHeight = tile.decodedHeight;
                     tile.decodedData.clear();
                 } else {
                     if (tile.supportPending && !tile.supportMainPixels.empty()) {
                         const size_t count = tile.decodedData.size();
                         if (count == tile.supportMainPixels.size()) {
                             constexpr unsigned char kAlphaVisibleThreshold = 10;
                             for (size_t i = 0; i + 3 < count; i += 4) {
                                 if (tile.supportMainPixels[i+3] <= kAlphaVisibleThreshold) {
                                     tile.supportMainPixels[i] = tile.decodedData[i];
                                     tile.supportMainPixels[i+1] = tile.decodedData[i+1];
                                     tile.supportMainPixels[i+2] = tile.decodedData[i+2];
                                     tile.supportMainPixels[i+3] = tile.decodedData[i+3];
                                 }
                             }
                             tile.texture = CreateTextureFromRGBA(
                                 tile.supportMainPixels.data(), 
                                 tile.supportMainWidth, 
                                 tile.supportMainHeight);
                         } else {
                             tile.texture = CreateTextureFromRGBA(
                                 tile.decodedData.data(), 
                                 tile.decodedWidth, 
                                 tile.decodedHeight);
                         }
                         tile.supportMainPixels.clear();
                         tile.supportPending = false;
                     } else {
                         tile.texture = CreateTextureFromRGBA(
                             tile.decodedData.data(), 
                             tile.decodedWidth, 
                             tile.decodedHeight);
                     }
                     if (tile.texture) {
                         tile.ownsTexture = true;
                         // Start unpop transition for overlay tiles
                         tile.unpopStartTime = glfwGetTime();
                         tile.unpopFactor = 0.0f;
                         tile.unpopComplete = false;
                         tile.fade = 0.0f;
                         ++textureUploads;
                     }
                     tile.decodedData.clear();
                 }
             }
        }
    }
  }

  // Pass 2: Mesh Build for Leaves (Strict Priority)
  for (const auto& key : leaves) {
      auto it = layerData.tiles.find(key);
      if (it == layerData.tiles.end()) continue;
      
      int edgeFlags = ComputeEdgeFlags(it->second.z, it->second.x,
                                       it->second.y, required, debug);
      
      bool demReady = false;
      if (canSampleDem && it->second.demPending &&
          (now - it->second.lastDemCheckTime) >= kDemMeshRecheckIntervalSec) {
        it->second.lastDemCheckTime = now;
        double lonCenter = (Tile2Lon(it->second.x, it->second.z) +
                            Tile2Lon(it->second.x + 1, it->second.z)) * 0.5;
        double latCenter = (Tile2Lat(it->second.y, it->second.z) +
                            Tile2Lat(it->second.y + 1, it->second.z)) * 0.5;
        double height = 0.0;
        if ((*heightSampler)(lonCenter, latCenter, it->second.z, height)) {
          demReady = true;
        }
      }
      if (it->second.mesh.vao == 0 || it->second.edgeFlags != edgeFlags || demReady) {
        if (meshRebuilds < maxRebuilds) {
          if (it->second.mesh.vao != 0) {
            glDeleteBuffers(1, &it->second.mesh.vbo);
            glDeleteBuffers(1, &it->second.mesh.ebo);
            glDeleteVertexArrays(1, &it->second.mesh.vao);
          }
          bool demUsed = false;
          bool demPending = false;
          it->second.mesh = BuildTileMesh(it->second.x, it->second.y,
                                               it->second.z, segments, edgeFlags,
                                               heightSampler, &demUsed, &demPending,
                                               debug);
          it->second.edgeFlags = edgeFlags;
          if (canSampleDem) {
            it->second.demUsed = demUsed;
            it->second.demPending = demPending;
          } else {
            it->second.demUsed = false;
            it->second.demPending = false;
          }
          ++meshRebuilds;
        }
      }
  }

  // Pass 3: Mesh Build for Non-Leaves (Fallback)
  // Sort high->low so child tiles get mesh before parents (faster AreChildrenReady)
  std::vector<std::string> orderedNonLeaves;
  orderedNonLeaves.reserve(required.size());
  
  // Sort orderedRequired by level descending
  std::sort(orderedRequired.begin(), orderedRequired.end(),
            [](const auto& a, const auto& b) {
              return a.first.level > b.first.level; // High zoom first
            });
            
  for (const auto& item : orderedRequired) {
      if (leafSet.find(item.second) == leafSet.end()) {
          orderedNonLeaves.push_back(item.second);
      }
  }
  
  for (const auto& key : orderedNonLeaves) {
      auto it = layerData.tiles.find(key);
      if (it == layerData.tiles.end()) continue;
      
      if ((it->second.textureState == TextureState::LOAD_OK || 
           it->second.textureState == TextureState::LOAD_OK_NO_DATA || 
           it->second.ownsTexture) && 
          it->second.mesh.vao == 0) {
          
          if (meshRebuilds < maxRebuilds) {
              bool demUsed = false;
              bool demPending = false;
              it->second.mesh = BuildTileMesh(it->second.x, it->second.y,
                                                   it->second.z, segments, EDGE_NONE,
                                                   heightSampler, &demUsed, &demPending,
                                                   debug);
              it->second.edgeFlags = EDGE_NONE;
              if (canSampleDem) {
                it->second.demUsed = demUsed;
                it->second.demPending = demPending;
              } else {
                it->second.demUsed = false;
                it->second.demPending = false;
              }
              ++meshRebuilds;
          }
      }
  }

  // Lazy tile cleanup with parent retention (same as main raster tiles)
  // Keep parent tiles until children are fully loaded to avoid pop-in/out
  std::vector<std::string> toRemove;
  for (auto& kv : layerData.tiles) {
    if (required.find(kv.first) == required.end()) {
      // Check if this tile is a parent of any required tile (retain for fallback)
      bool isParentOfRequired = requiredAncestors.find(kv.first) != requiredAncestors.end();
      if (!isParentOfRequired) {
        toRemove.push_back(kv.first);
      }
    }
  }
  // Cache cap: limit total overlay tiles to prevent unbounded memory growth
  constexpr size_t kMaxLayerTiles = 256;
  constexpr size_t kMaxLayerRemovalsPerFrame = 16;
  
  // If cache is over limit, remove more aggressively
  size_t removalsNeeded = toRemove.size();
  if (layerData.tiles.size() > kMaxLayerTiles) {
    removalsNeeded = std::min(toRemove.size(), layerData.tiles.size() - kMaxLayerTiles + 16);
  }
  removalsNeeded = std::min(removalsNeeded, kMaxLayerRemovalsPerFrame);
  
  size_t removed = 0;
  for (const auto& key : toRemove) {
    if (removed >= removalsNeeded) break;
    auto it = layerData.tiles.find(key);
    if (it != layerData.tiles.end()) {
      DestroyTile(it->second, queue);
      layerData.tiles.erase(it);
      ++removed;
    }
  }

  // Build visible list
  layerData.visibleTiles.clear();
  layerData.visibleTiles.reserve(leaves.size());
  for (const auto& key : leaves) {
    auto it = layerData.tiles.find(key);
    if (it != layerData.tiles.end()) {
      layerData.visibleTiles.push_back(&it->second);
    }
  }
}

// ============================================================================
// MODULAR TILE SYNC HELPERS
// ============================================================================

// Context struct to reduce parameter passing
struct TileSyncContext {
  std::unordered_map<std::string, Tile>& tiles;
  const GlobeConfig& config;
  GLuint loadingTexture;
  const std::unordered_set<std::string>& required;
  const std::vector<std::string>& leaves;
  std::unordered_set<std::string>& pending;
  std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& downloadQueue;
  std::mutex& downloadMutex;
  std::condition_variable& downloadCv;
  int segments;
  const HeightSampler* heightSampler;
  size_t& meshRebuilds;
  size_t maxRebuilds;
  size_t& queueSize;
  size_t& textureUploads;
  size_t maxUploads;
  uint32_t currentFrame;
  std::mutex* pendingMutex;
  const glm::vec3* cameraPos;
  int currentZoom;
  const glm::mat4* mvp;
  
  // Precomputed data
  std::unordered_set<std::string> leafSet;
  int baseMinZoom;
  int baseMaxZoom;
  double now;
  bool canSampleDem;
};

// Helper: Compute priority score for download job (Google Earth style)
float ComputeDownloadPriorityScore(const Tile& tile, const glm::vec3* cameraPos, 
                                    const glm::mat4* mvp, int currentZoom,
                                    bool isLeaf) {
  float score = 0.0f;
  if (!cameraPos) return score;
  
  // 1. Distance scoring (closer = higher priority) - max 40 points
  float distance = glm::length(tile.center - *cameraPos);
  float maxDist = 3.0f * GLOBE_RADIUS;
  score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
  
  // 2. Viewport Overlap scoring (center of screen = higher priority) - max 30 points
  if (mvp) {
    glm::vec4 clipPos = (*mvp) * glm::vec4(tile.center, 1.0f);
    if (clipPos.w > 0.001f) {
      glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
      float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
      score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
    }
  }
  
  // 3. Level proximity - max 20 points
  int levelDiff = std::abs(tile.z - currentZoom);
  score += std::max(0.0f, 5.0f - levelDiff) * 4.0f;
  
  // 4. Leaf tile bonus - max 10 points
  if (isLeaf) score += 10.0f;
  
  return score;
}

// Helper: Queue a tile for download
void QueueTileDownload(TileSyncContext& ctx, Tile& tile, const std::string& key, bool isLeaf) {
  DownloadJob job;
  job.urlTemplate = ctx.config.tileUrl;
  job.z = tile.z;
  job.x = tile.x;
  job.y = tile.y;
  job.isVector = false;
  job.priority = PRIORITY_VISIBLE_LEAF;
  job.priorityScore = ComputeDownloadPriorityScore(tile, ctx.cameraPos, ctx.mvp, ctx.currentZoom, isLeaf);
  
  // Pass pendingMutex to EnqueueDownload - it handles lock ordering internally
  EnqueueDownload(ctx.pending, ctx.downloadQueue, ctx.downloadMutex, ctx.downloadCv, job, ctx.pendingMutex);
  tile.textureState = TextureState::LOADING;
  ++ctx.queueSize;
}

// Helper: Build mesh for a tile
void BuildTileMeshIfNeeded(TileSyncContext& ctx, Tile& tile, int edgeFlags, bool demReady) {
  if (tile.mesh.vao == 0 || tile.edgeFlags != edgeFlags || demReady) {
    if (ctx.meshRebuilds < ctx.maxRebuilds) {
      if (tile.mesh.vao != 0) {
        glDeleteBuffers(1, &tile.mesh.vbo);
        glDeleteBuffers(1, &tile.mesh.ebo);
        glDeleteVertexArrays(1, &tile.mesh.vao);
      }
      bool demUsed = false, demPending = false;
      tile.mesh = BuildTileMesh(tile.x, tile.y, tile.z, ctx.segments, edgeFlags,
                                ctx.heightSampler, &demUsed, &demPending, ctx.config.demDebug);
      tile.edgeFlags = edgeFlags;
      if (ctx.canSampleDem) {
        tile.demUsed = demUsed;
        tile.demPending = demPending;
      } else {
        tile.demUsed = false;
        tile.demPending = false;
      }
      ++ctx.meshRebuilds;
    }
  }
}

// Pass 3: Non-leaf fallback mesh building (called after overlay layers)
// Ensures overlay leaf meshes get priority over base fallback meshes
void SyncRasterTilesPass3(std::unordered_map<std::string, Tile>& tiles,
                          const GlobeConfig& config,
                          const std::unordered_set<std::string>& required,
                          const std::vector<std::string>& leaves,
                          int segments,
                          const HeightSampler* heightSampler,
                          size_t& meshRebuilds,
                          size_t maxRebuilds) {
  std::unordered_set<std::string> leafSet(leaves.begin(), leaves.end());
  const bool canSampleDem = heightSampler && *heightSampler;
  
  // P1 Optimization: Parse keys once into TileKey and sort
  // Build ordered list high->low so child tiles get mesh before parents
  std::vector<std::pair<TileKey, std::string>> orderedRequired;
  orderedRequired.reserve(required.size());
  for (const auto& key : required) {
    orderedRequired.emplace_back(TileKey::FromString(key), key);
  }
  
  std::sort(orderedRequired.begin(), orderedRequired.end(),
            [](const auto& a, const auto& b) {
              return a.first.level > b.first.level; // High zoom first
            });
  
  for (const auto& item : orderedRequired) {
      const std::string& key = item.second;
      if (leafSet.find(key) != leafSet.end()) continue; // Handled in Pass 2
      
      auto it = tiles.find(key);
      if (it == tiles.end()) continue;
      
      if ((it->second.textureState == TextureState::LOAD_OK || 
           it->second.textureState == TextureState::LOAD_OK_NO_DATA || 
           it->second.ownsTexture) && 
          it->second.mesh.vao == 0) {
          
          if (meshRebuilds < maxRebuilds) {
              bool demUsed = false;
              bool demPending = false;
              it->second.mesh = BuildTileMesh(it->second.x, it->second.y,
                                                   it->second.z, segments, EDGE_NONE,
                                                   heightSampler, &demUsed, &demPending,
                                                   config.demDebug);
              it->second.edgeFlags = EDGE_NONE;
              if (canSampleDem) {
                it->second.demUsed = demUsed;
                it->second.demPending = demPending;
              } else {
                it->second.demUsed = false;
                it->second.demPending = false;
              }
              ++meshRebuilds;
          }
      }
  }
}

void SyncRasterTiles(DeferredQueue* queue,
                     std::unordered_map<std::string, Tile>& tiles,
                     std::vector<Tile*>& visible,
                     const GlobeConfig& config,
                     GLuint loadingTexture,
                     const std::unordered_set<std::string>& required,
                     const std::vector<std::string>& leaves,
                     std::unordered_set<std::string>& pending,
                     std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& downloadQueue,
                     std::mutex& downloadMutex,
                     std::condition_variable& downloadCv,
                     int segments,
                     const HeightSampler* heightSampler,
                     size_t& meshRebuilds,
                     size_t maxRebuilds,
                     size_t& queueSize,
                     size_t& textureUploads,
                     size_t maxUploads,
                     TileScheduler* scheduler = nullptr,
                     uint32_t currentFrame = 0,
                     std::mutex* pendingMutex = nullptr,
                     const glm::vec3* cameraPos = nullptr,
                     int currentZoom = 0,
                     const glm::mat4* mvp = nullptr) {
  // P1 Optimization: Avoid repeated sscanf by parsing once into TileKey
  // and using native integer comparison for sorting.
  std::vector<std::pair<TileKey, std::string>> orderedRequired;
  orderedRequired.reserve(required.size());
  for (const auto& key : required) {
    orderedRequired.emplace_back(TileKey::FromString(key), key);
  }
  
  // Sort by TileKey (matches (level, y, x) order of previous implementation)
  std::sort(orderedRequired.begin(), orderedRequired.end(),
            [](const auto& a, const auto& b) {
              return a.first < b.first;
            });

  std::unordered_set<std::string> leafSet;
  leafSet.reserve(leaves.size());
  for (const auto& key : leaves) {
    leafSet.insert(key);
  }
  const bool canSampleDem = heightSampler && *heightSampler;
  const double now = glfwGetTime();
  int baseMinZoom = config.baseRasterMinZoom >= 0 ? config.baseRasterMinZoom : config.minZoom;
  int baseMaxZoom = config.baseRasterMaxZoom >= 0 ? config.baseRasterMaxZoom : config.maxZoom;
  if (baseMinZoom > baseMaxZoom) {
    std::swap(baseMinZoom, baseMaxZoom);
  }

  for (const auto& item : orderedRequired) {
    const TileKey& tileKey = item.first;
    const std::string& key = item.second;
    
    auto existing = tiles.find(key);
    if (existing == tiles.end()) {
      Tile tile(tileKey.level, tileKey.x, tileKey.y);
      tile.texture = loadingTexture;
      tile.ownsTexture = false;
      tile.center = TileCenterNormal(tileKey.level, tileKey.x, tileKey.y);
      tile.angularRadius = TileAngularRadius(tileKey.level, tileKey.x, tileKey.y);
      tile.radius = TileBoundingRadius(tileKey.level, tileKey.x, tileKey.y);
      tile.fade = 0.0f;
      tiles.emplace(key, std::move(tile));
      existing = tiles.find(key);
    }

    // P4: Update usage stats for LRU
    if (currentFrame > 0) {
        existing->second.lastFrameUsed = currentFrame;
        existing->second.accessCount++;
    }

    const bool baseZoomAllowed =
        (existing->second.z >= baseMinZoom && existing->second.z <= baseMaxZoom);
    if (!baseZoomAllowed) {
      if (existing->second.ownsTexture && existing->second.texture != 0) {
        glDeleteTextures(1, &existing->second.texture);
      }
      existing->second.texture = loadingTexture;
      existing->second.ownsTexture = false;
      existing->second.textureState = TextureState::LOAD_OK_NO_DATA;
      continue;
    }

    // JS parity: Only queue if not already loaded or loading, and check state machine
    if (scheduler) {
        // Integration: Use Scheduler
        // FIX (2026-02-04): Schedule ALL required tiles, not just leaves.
        // TileLodSelector adds children to 'required' when parent is fallback,
        // and those children need to be loaded for progressive refinement.
        bool isLeaf = leafSet.count(key) > 0;
        
        // Schedule all required tiles (leaf gets higher priority)
        {
            LoadPriority priority = isLeaf ? LoadPriority::URGENT : LoadPriority::NORMAL;

            bool shouldSchedule = (existing->second.loadState == TileLoadState::UNLOADED || 
                                   existing->second.loadState == TileLoadState::FAILED);
        
        // Promotion check: If scheduled but new priority is higher (lower value), reschedule
        if (existing->second.loadState == TileLoadState::SCHEDULED) {
            if (priority < existing->second.loadPriority) {
                shouldSchedule = true;
            }
        }

            if (shouldSchedule) {
                 bool canRetry = true;
                 if (existing->second.loadState == TileLoadState::FAILED) {
                     if (existing->second.retryCount >= 3) canRetry = false;
                     else {
                         double backoff = std::pow(2.0, existing->second.retryCount);
                         if (now - existing->second.lastRetryTime < backoff) canRetry = false;
                     }
                 }

                 if (canRetry) {
                     existing->second.loadPriority = priority;
                     TaskParams params;
                     params.urlTemplate = config.tileUrl;
                     
                     // Google Earth style priority scoring for scheduler path
                     if (cameraPos) {
                       float score = 0.0f;
                       float distance = glm::length(existing->second.center - *cameraPos);
                       float maxDist = 3.0f * GLOBE_RADIUS;
                       score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
                       if (mvp) {
                         glm::vec4 clipPos = (*mvp) * glm::vec4(existing->second.center, 1.0f);
                         if (clipPos.w > 0.001f) {
                           glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
                           float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
                           score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
                         }
                       }
                       int levelDiff = std::abs(existing->second.z - currentZoom);
                       score += std::max(0.0f, 5.0f - levelDiff) * 4.0f;
                       params.priorityScore = score;
                     }
                     
                     scheduler->Schedule(&existing->second, params);
                 }
            }
        }  // end scheduling block
        
        // Sync textureState for legacy rendering code
        if (existing->second.loadState == TileLoadState::SCHEDULED || 
            existing->second.loadState == TileLoadState::FETCHING ||
            existing->second.loadState == TileLoadState::DECODING) {
            existing->second.textureState = TextureState::LOADING;
        } else if (existing->second.loadState == TileLoadState::READY) {
             if (!existing->second.ownsTexture && !existing->second.decodedData.empty()) {
                 if (textureUploads < maxUploads) {
                     // Check for transparency (No-Data)
                     bool anyTrans = false, allTrans = false;
                     if (existing->second.decodedWidth > 0) {
                         AnalyzeAlpha(existing->second.decodedData, anyTrans, allTrans);
                     }

                     if (allTrans) {
                         if (existing->second.ownsTexture && existing->second.texture != 0) {
                             glDeleteTextures(1, &existing->second.texture);
                         }
                         existing->second.texture = loadingTexture;
                         existing->second.textureState = TextureState::LOAD_OK_NO_DATA;
                         existing->second.ownsTexture = false;
                         existing->second.decodedData.clear();
                     } else {
                         if (existing->second.decodedWidth > 0) {
                             existing->second.texture = CreateTextureFromRGBA(
                                 existing->second.decodedData.data(), 
                                 existing->second.decodedWidth, 
                                 existing->second.decodedHeight);
                         } else {
                             existing->second.texture = CreateTextureFromMemory(existing->second.decodedData);
                         }
                         
                         if (existing->second.texture) {
                             existing->second.ownsTexture = true;
                             existing->second.textureState = TextureState::LOAD_OK;
                             // Reset unpop transition state for fresh transition
                             existing->second.unpopStartTime = glfwGetTime();
                             existing->second.unpopFactor = 0.0f;
                             existing->second.unpopComplete = false;
                             existing->second.fade = 0.0f;  // Reset fade for consistency
                             ++textureUploads;
                         } else {
                             existing->second.textureState = TextureState::LOAD_OK_NO_DATA;
                         }
                         existing->second.decodedData.clear();
                     }
                 }
             }
        } else if (existing->second.loadState == TileLoadState::FAILED) {
            existing->second.textureState = TextureState::LOAD_NO_INTERNET;
        }
    } else {
        // Legacy Logic
        bool isPending = false;
        if (pendingMutex) {
            std::lock_guard<std::mutex> lock(*pendingMutex);
            isPending = pending.find(key) != pending.end();
        } else {
            isPending = pending.find(key) != pending.end();
        }

        bool shouldQueue = !existing->second.ownsTexture && 
                           existing->second.textureState != TextureState::LOADING &&
                           existing->second.textureState != TextureState::LOAD_OK_NO_DATA &&
                           !isPending;
        
        // P3: Allow retry for failed tiles with exponential backoff
        if (existing->second.textureState == TextureState::LOAD_NO_INTERNET && 
            existing->second.retryCount < 3) {
          double backoffSeconds = std::pow(2.0, existing->second.retryCount);
          double currentTime = glfwGetTime();
          if (currentTime - existing->second.lastRetryTime >= backoffSeconds) {
            shouldQueue = true;
          }
        }
        
        if (shouldQueue && queueSize < kMaxDownloadQueueSize) {
          // All required tiles are URGENT to prevent coverage starvation
          int priority = PRIORITY_VISIBLE_LEAF;

          DownloadJob job;
          job.urlTemplate = config.tileUrl;
          job.z = existing->second.z;
          job.x = existing->second.x;
          job.y = existing->second.y;
          job.isVector = false;
          job.priority = priority;
          
          // Google Earth style priority scoring: Distance + Importance + Viewport Overlap
          if (cameraPos) {
            float score = 0.0f;
            
            // 1. Distance scoring (closer = higher priority) - max 40 points
            float distance = glm::length(existing->second.center - *cameraPos);
            float maxDist = 3.0f * GLOBE_RADIUS;
            float distanceScore = std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
            score += distanceScore;
            
            // 2. Viewport Overlap scoring (center of screen = higher priority) - max 30 points
            if (mvp) {
              glm::vec4 clipPos = (*mvp) * glm::vec4(existing->second.center, 1.0f);
              if (clipPos.w > 0.001f) {
                glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
                // ndc.xy in [-1, 1], center at (0,0)
                float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
                float viewportScore = std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
                score += viewportScore;
              }
            }
            
            // 3. Level proximity (tiles near current zoom are more important) - max 20 points
            int levelDiff = std::abs(existing->second.z - currentZoom);
            float levelScore = std::max(0.0f, 5.0f - levelDiff) * 4.0f;
            score += levelScore;
            
            // 4. SSE-based importance bonus for leaf tiles - max 10 points
            if (leafSet.count(key) > 0) {
              score += 10.0f;  // Leaf tiles get bonus
            }
            
            job.priorityScore = score;
          }
          
          // Pass pendingMutex to EnqueueDownload - it handles lock ordering internally
          EnqueueDownload(pending, downloadQueue, downloadMutex, downloadCv, job, pendingMutex);
          existing->second.textureState = TextureState::LOADING;  // JS parity: set state
          existing->second.loadState = TileLoadState::FETCHING;   // Update loadState for stats
          ++queueSize;
        }
    }
  }

  // Pass 2: Mesh Build for Leaves (Strict Priority)
  for (const auto& key : leaves) {
      auto it = tiles.find(key);
      if (it == tiles.end()) continue;
      
      // Compute edge flags for seam stitching with lower LOD neighbors
      int edgeFlags = ComputeEdgeFlags(it->second.z, it->second.x, 
                                       it->second.y, required, config.demDebug);
      bool demReady = false;
      if (canSampleDem && it->second.demPending &&
          (now - it->second.lastDemCheckTime) >= kDemMeshRecheckIntervalSec) {
        it->second.lastDemCheckTime = now;
        double lonCenter = (Tile2Lon(it->second.x, it->second.z) +
                            Tile2Lon(it->second.x + 1, it->second.z)) * 0.5;
        double latCenter = (Tile2Lat(it->second.y, it->second.z) +
                            Tile2Lat(it->second.y + 1, it->second.z)) * 0.5;
        double height = 0.0;
        if ((*heightSampler)(lonCenter, latCenter, it->second.z, height)) {
          demReady = true;
        }
      }
      
      // Rebuild mesh if edge flags changed or mesh doesn't exist (limit per frame)
      if (it->second.mesh.vao == 0 || it->second.edgeFlags != edgeFlags || demReady) {
        if (meshRebuilds < maxRebuilds) {
          if (it->second.mesh.vao != 0) {
            glDeleteBuffers(1, &it->second.mesh.vbo);
            glDeleteBuffers(1, &it->second.mesh.ebo);
            glDeleteVertexArrays(1, &it->second.mesh.vao);
          }
          bool demUsed = false;
          bool demPending = false;
          it->second.mesh = BuildTileMesh(it->second.x, it->second.y,
                                               it->second.z, segments, edgeFlags,
                                               heightSampler, &demUsed, &demPending,
                                               config.demDebug);
          it->second.edgeFlags = edgeFlags;
          if (canSampleDem) {
            it->second.demUsed = demUsed;
            it->second.demPending = demPending;
          } else {
            it->second.demUsed = false;
            it->second.demPending = false;
          }
          ++meshRebuilds;
        }
      }
  }

  // NOTE: Pass 3 (non-leaf fallback mesh) moved to SyncRasterTilesPass3() 
  // Called after overlay layers to ensure overlay leaf meshes get priority

  // Cesium-style: Lazy tile cleanup with parent retention + byte-based cache
  // Keep parent tiles until children are fully loaded to avoid pop-in/out
  std::vector<std::pair<std::string, size_t>> toRemove;  // key + estimated bytes
  size_t totalCachedBytes = 0;
  
  // P1 Optimization: Precompute required ancestors to avoid O(N*M) check in eviction loop
  std::unordered_set<std::string> requiredAncestors;
  for (const auto& item : orderedRequired) {
      TileKey k = item.first;
      while (k.level > 0) {
          k = k.GetParent();
          requiredAncestors.insert(k.ToString());
      }
  }

  for (auto& kv : tiles) {
    size_t tileBytes = EstimateTileBytes(kv.second);
    kv.second.estimatedBytes = tileBytes;
    totalCachedBytes += tileBytes;
    
    if (required.find(kv.first) == required.end()) {
      // Check if this tile is a parent of any required tile (retain for fallback)
      bool isParentOfRequired = requiredAncestors.find(kv.first) != requiredAncestors.end();
      // Google Earth style: Skip pinned tiles (protect important tiles from eviction)
      if (!isParentOfRequired && !kv.second.pinned) {
        toRemove.push_back({kv.first, tileBytes});
      }
    }
  }
  
  // Cesium-style: Byte-based cache limit with time-budgeted unload
  if (totalCachedBytes > config.maximumCachedBytes) {
    // Sort by: 1) lastFrameUsed (LRU), 2) zoom level (higher LOD first)
    std::sort(toRemove.begin(), toRemove.end(), 
      [&tiles](const std::pair<std::string, size_t>& a, 
               const std::pair<std::string, size_t>& b) {
        auto itA = tiles.find(a.first);
        auto itB = tiles.find(b.first);
        if (itA == tiles.end() || itB == tiles.end()) return false;
        // LRU: older frames first
        if (itA->second.lastFrameUsed != itB->second.lastFrameUsed) {
          return itA->second.lastFrameUsed < itB->second.lastFrameUsed;
        }
        // Then higher LOD first
        return itA->second.z > itB->second.z;
      });
    
    double unloadStartTime = glfwGetTime();
    double unloadTimeLimit = config.tileCacheUnloadTimeLimitMs / 1000.0;
    size_t bytesFreed = 0;
    size_t targetFree = totalCachedBytes - config.maximumCachedBytes;
    
    for (const auto& [key, bytes] : toRemove) {
      // Time budget check
      if (glfwGetTime() - unloadStartTime > unloadTimeLimit) break;
      // Byte target check
      if (bytesFreed >= targetFree) break;
      
      auto it = tiles.find(key);
      if (it != tiles.end()) {
        DestroyTile(it->second, queue);
        bytesFreed += bytes;
        tiles.erase(it);
      }
    }
  }

  // Google Earth style: Pin visible leaf tiles, unpin non-visible ones
  for (auto& kv : tiles) {
    bool isVisibleLeaf = (leafSet.count(kv.first) > 0);
    kv.second.pinned = isVisibleLeaf;
    if (isVisibleLeaf) {
      kv.second.accessCount++;  // Track access for importance scoring
    }
  }

  visible.clear();
  visible.reserve(leaves.size());
  for (const auto& key : leaves) {
    auto it = tiles.find(key);
    if (it != tiles.end()) {
      visible.push_back(&it->second);
    }
  }
}

bool BuildVectorTileFromData(const GlobeConfig& config,
                             int z,
                             int x,
                             int y,
                             const std::vector<unsigned char>& buffer,
                             VectorTile& out) {
  if (buffer.empty()) {
    return false;
  }

  vtzero::data_view view{reinterpret_cast<const char*>(buffer.data()), buffer.size()};
  vtzero::vector_tile tile{view};

  std::vector<glm::vec3> lineVerts;
  std::vector<glm::vec3> pointVerts;
  std::vector<glm::vec3> fillVerts;

  while (auto layer = tile.next_layer()) {
    if (!config.vectorLayerName.empty()) {
      std::string layerName(layer.name().data(), layer.name().size());
      if (layerName != config.vectorLayerName) {
        continue;
      }
    }
    VectorGeometryCollector collector{lineVerts, pointVerts, fillVerts};
    collector.extent = static_cast<int>(layer.extent());
    collector.z = z;
    collector.x = x;
    collector.y = y;

    while (auto feature = layer.next_feature()) {
      collector.BeginFeature();
      vtzero::decode_geometry(feature.geometry(), collector);
      collector.EndFeature();
    }
  }

  out.x = x;
  out.y = y;
  out.z = z;
  if (!lineVerts.empty()) {
    CreateVectorBuffer(out.lineVao, out.lineVbo, lineVerts);
    out.lineVertexCount = static_cast<GLsizei>(lineVerts.size());
  }
  if (!pointVerts.empty()) {
    CreateVectorBuffer(out.pointVao, out.pointVbo, pointVerts);
    out.pointVertexCount = static_cast<GLsizei>(pointVerts.size());
  }
  if (!fillVerts.empty()) {
    CreateVectorBuffer(out.fillVao, out.fillVbo, fillVerts);
    out.fillVertexCount = static_cast<GLsizei>(fillVerts.size());
  }
  return out.lineVertexCount > 0 || out.pointVertexCount > 0 || out.fillVertexCount > 0;
}

void SyncVectorTiles(DeferredQueue* queue,
                     std::unordered_map<std::string, VectorTile>& tiles,
                     std::vector<VectorTile*>& visible,
                     const GlobeConfig& config,
                     const std::vector<std::string>& leaves,
                     TileScheduler* scheduler,
                     const glm::vec3* cameraPos = nullptr,
                     int currentZoom = 0,
                     const glm::mat4* mvp = nullptr) {
  std::unordered_set<std::string> required;
  required.reserve(leaves.size());
  for (const auto& key : leaves) {
    required.insert(key);
  }

  for (const auto& key : required) {
    auto existing = tiles.find(key);
    if (existing == tiles.end()) {
      int z = 0;
      int x = 0;
      int y = 0;
      std::sscanf(key.c_str(), "%d/%d/%d", &z, &x, &y);
      VectorTile vt;
      vt.x = x;
      vt.y = y;
      vt.z = z;
      vt.schedulerTile = Tile(z, x, y);
      vt.center = TileCenterNormal(z, x, y);
      vt.angularRadius = TileAngularRadius(z, x, y);
      vt.radius = TileBoundingRadius(z, x, y);
      tiles.emplace(key, std::move(vt));
      existing = tiles.find(key);
    }

    if (existing->second.lineVertexCount == 0 && existing->second.pointVertexCount == 0 && existing->second.fillVertexCount == 0) {
        if (scheduler) {
            Tile& tile = existing->second.schedulerTile;
            if (tile.loadState == TileLoadState::UNLOADED || 
                tile.loadState == TileLoadState::FAILED) {
                 
                 bool canRetry = true;
                 if (tile.loadState == TileLoadState::FAILED) {
                     if (tile.retryCount >= 3) canRetry = false;
                     else {
                         double backoff = std::pow(2.0, tile.retryCount);
                         if (glfwGetTime() - tile.lastRetryTime < backoff) canRetry = false;
                     }
                 }

                 if (canRetry) {
                     tile.loadPriority = LoadPriority::URGENT;
                     TaskParams params;
                     params.urlTemplate = config.vectorTileUrl;
                     params.isVector = true;
                     
                     // Google Earth style priority scoring for vector tiles
                     if (cameraPos) {
                       float score = 0.0f;
                       float distance = glm::length(existing->second.center - *cameraPos);
                       float maxDist = 3.0f * GLOBE_RADIUS;
                       score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
                       if (mvp) {
                         glm::vec4 clipPos = (*mvp) * glm::vec4(existing->second.center, 1.0f);
                         if (clipPos.w > 0.001f) {
                           glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
                           float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
                           score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
                         }
                       }
                       int levelDiff = std::abs(existing->second.z - currentZoom);
                       score += std::max(0.0f, 5.0f - levelDiff) * 4.0f;
                       score += 10.0f;  // Vector tiles are always leaves
                       params.priorityScore = score;
                     }
                     
                     scheduler->Schedule(&tile, params);
                 }
            } else if (tile.loadState == TileLoadState::READY) {
                 if (!tile.decodedData.empty()) {
                     if (!BuildVectorTileFromData(config, tile.z, tile.x, tile.y, 
                                             tile.decodedData, existing->second)) {
                         tile.loadState = TileLoadState::FAILED;
                         tile.retryCount++;
                         tile.lastRetryTime = glfwGetTime();
                     }
                     tile.decodedData.clear();
                 }
            }
        }
    }
  }

  // Lazy vector tile cleanup with cache cap
  std::vector<std::string> toRemove;
  for (auto& kv : tiles) {
    if (required.find(kv.first) == required.end()) {
      toRemove.push_back(kv.first);
    }
  }
  
  // Cache cap: limit total vector tiles to prevent unbounded memory growth
  constexpr size_t kMaxVectorTiles = 256;
  constexpr size_t kMaxVectorRemovalsPerFrame = 16;
  
  // If cache is over limit, remove more aggressively
  size_t removalsNeeded = toRemove.size();
  if (tiles.size() > kMaxVectorTiles) {
    removalsNeeded = std::min(toRemove.size(), tiles.size() - kMaxVectorTiles + 16);
  }
  removalsNeeded = std::min(removalsNeeded, kMaxVectorRemovalsPerFrame);
  
  size_t removed = 0;
  for (const auto& key : toRemove) {
    if (removed >= removalsNeeded) break;
    auto it = tiles.find(key);
    if (it != tiles.end()) {
      DestroyVectorTile(it->second, queue);
      tiles.erase(it);
      ++removed;
    }
  }

  visible.clear();
  visible.reserve(leaves.size());
  for (const auto& key : leaves) {
    auto it = tiles.find(key);
    if (it != tiles.end()) {
      visible.push_back(&it->second);
    }
  }
}

bool ResolveAncestorTexture(const std::unordered_map<std::string, Tile>& tiles,
                            int z,
                            int x,
                            int y,
                            bool includeSelf,
                            ResolvedTexture& out) {
  glm::vec2 offset(0.0f);
  glm::vec2 scale(1.0f);
  int cz = z;
  int cx = x;
  int cy = y;
  bool first = true;
  while (true) {
    if (includeSelf || !first) {
      std::string key = MakeTileKey(cz, cx, cy);
      auto it = tiles.find(key);
      if (it != tiles.end() && it->second.ownsTexture && it->second.texture) {
        out.texture = it->second.texture;
        out.uvOffset = offset;
        out.uvScale = scale;
        out.valid = true;
        return true;
      }
    }
    if (cz == 0) {
      break;
    }
    int quadX = cx & 1;
    int quadY = cy & 1;
    scale *= 0.5f;
    offset.x = offset.x * 0.5f + static_cast<float>(quadX) * 0.5f;
    offset.y = offset.y * 0.5f + static_cast<float>(1 - quadY) * 0.5f;
    cx >>= 1;
    cy >>= 1;
    cz -= 1;
    first = false;
  }
  out = ResolvedTexture{};
  return false;
}

}  // namespace

// JS mouse mode state machine
enum class MouseMode {
  IDLE = 0,
  ROTATE,      // Left drag: arcball rotation
  TILT,        // Middle drag: tilt + north
  ZOOM,        // Right drag: zoom
  SELECTION,   // Selection mode (user events)
  EDIT,        // Edit mode (draw order/plugin)
};

// JS camera mode state machine (f.a/f.c/f.f/f.g/f.h/f.i/f.j/f.k constants)
// Maps to CameraMode in webglobe_deobfuscated/core/camera.js:18-27
enum class FCamMode {
  IDLE = 0,           // f.a - default idle state
  FLYING = 1,         // f.c - flying to target (SetFlyToPoint)
  ZOOMING = 2,        // f.f - zoom wheel animation
  DOUBLE_CLICK = 3,   // f.g - double click zoom
  MID_ROTATE = 4,     // f.h - middle button rotation
  TURNING = 5,        // f.i - turning animation (inertia)
  ZOOM_PINCH = 6,     // f.j - Pinch zoom on touch
  COMPASS_ROTATE = 7  // f.k - Compass rotation
};

// Integration: Adapter for legacy DownloadQueue
class GlobeTileFetcher : public ITileFetcher {
public:
  GlobeTileFetcher(std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue, 
                   std::mutex& mutex, std::condition_variable& cv, 
                   const std::string& urlTemplate, TileScheduler* scheduler,
                   std::mutex& cancelMutex, std::unordered_set<SchedulerKey, SchedulerKey::Hash>& cancelledKeys)
      : queue_(queue), mutex_(mutex), cv_(cv), urlTemplate_(urlTemplate), scheduler_(scheduler),
        sharedCancelMutex_(cancelMutex), sharedCancelledKeys_(cancelledKeys) {}

  void SetUrlTemplate(const std::string& url) {
      urlTemplate_ = url;
  }

  void SetScheduler(TileScheduler* scheduler) {
      scheduler_ = scheduler;
  }

  void Fetch(const SchedulerKey& key, const TaskParams& params, int priority) override {
    DownloadJob job;
    job.urlTemplate = params.urlTemplate.empty() ? urlTemplate_ : params.urlTemplate;
    job.layerId = params.layerId;
    job.isVector = params.isVector;
    job.z = key.tileKey.level;
    job.x = key.tileKey.x;
    job.y = key.tileKey.y;
    job.priority = priority;
    job.priorityScore = params.priorityScore;  // Google Earth style priority scoring
    job.queueTime = glfwGetTime(); // Use global time
    
    // Set callback to notify Scheduler
    TileScheduler* sched = scheduler_;
    job.callback = [sched, key](std::vector<unsigned char> data, bool success) {
        if (sched) {
            sched->OnFetchComplete(key, std::move(data), success);
        }
    };
    
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(job));
    cv_.notify_one();
  }

  void Cancel(const SchedulerKey& key) override {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    sharedCancelledKeys_.insert(key);
  }
  
  bool IsCancelled(const SchedulerKey& key) {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    return sharedCancelledKeys_.count(key) > 0;
  }
  
  void ClearCancelled(const SchedulerKey& key) {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    sharedCancelledKeys_.erase(key);
  }

private:
  std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue_;
  std::mutex& mutex_;
  std::condition_variable& cv_;
  std::string urlTemplate_;
  TileScheduler* scheduler_;
  std::mutex& sharedCancelMutex_;
  std::unordered_set<SchedulerKey, SchedulerKey::Hash>& sharedCancelledKeys_;
};

class GlobeImageDecoder : public ITileDecoder, public std::enable_shared_from_this<GlobeImageDecoder> {
public:
    GlobeImageDecoder(TileScheduler* scheduler) : scheduler_(scheduler), running_(true) {
        worker_ = std::thread([this]() { WorkerLoop(); });
    }

    ~GlobeImageDecoder() {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            running_ = false;
            // Clear queue for faster shutdown
            std::queue<std::pair<SchedulerKey, std::vector<unsigned char>>> empty;
            std::swap(queue_, empty);
        }
        queueCv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void SetScheduler(TileScheduler* scheduler) {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        scheduler_ = scheduler;
    }
    
    void Invalidate() {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        scheduler_ = nullptr;
    }

    void Decode(const SchedulerKey& key, std::vector<unsigned char> data) override {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (!running_) return;
        queue_.push({key, std::move(data)});
        queueCv_.notify_one();
    }

private:
    void WorkerLoop() {
        while (true) {
            std::pair<SchedulerKey, std::vector<unsigned char>> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait(lock, [this]() { return !queue_.empty() || !running_; });
                if (!running_) return; // Exit immediately on shutdown
                if (queue_.empty()) continue;
                task = std::move(queue_.front());
                queue_.pop();
            }

            // Decode
            int width = 0, height = 0, channels = 0;
            // stbi_set_flip handled globally in Constructor
            unsigned char* pixels = stbi_load_from_memory(
                task.second.data(), static_cast<int>(task.second.size()), &width, &height, &channels, 4);
            
            bool success = (pixels != nullptr);
            std::vector<unsigned char> decoded;
            if (success) {
                decoded.assign(pixels, pixels + (width * height * 4));
                stbi_image_free(pixels);
            }
            
            std::lock_guard<std::mutex> lock(schedulerMutex_);
            if (scheduler_) {
                scheduler_->OnDecodeComplete(task.first, std::move(decoded), width, height, success);
            }
        }
    }

    std::mutex schedulerMutex_;
    TileScheduler* scheduler_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<std::pair<SchedulerKey, std::vector<unsigned char>>> queue_;
    std::thread worker_;
    bool running_;
};

struct GlobeEngine::Impl {
  Impl() : m_flightController(m_newCamera) {}

  GlobeEngine* owner = nullptr;
  GlobeConfig config;
  GLFWwindow* window = nullptr;
  bool valid = false;
  bool uiInitialized = false;  // Track ImGui init state to prevent double shutdown
  
  // Mouse state - JS parity
  MouseMode mouseMode = MouseMode::IDLE;
  FCamMode camMode = FCamMode::IDLE;  // JS: FCamera.FCamMode
  bool dragging = false;       // Left mouse: pan/rotate
  bool tiltDragging = false;   // Middle mouse: tilt + north angle
  bool zoomDragging = false;   // Right mouse: zoom
  double lastX = 0.0;
  double lastY = 0.0;
  double cursorX = 0.0;
  double cursorY = 0.0;
  double pendingCursorX = 0.0;
  double pendingCursorY = 0.0;
  
  void ProcessPendingSets() {} // Placeholder implementation
  double dragStartX = 0.0;     // Drag start position for arcball
  double dragStartY = 0.0;
  bool lastMouseOnGlobe = false;
  double lastMouseLat = 0.0;
  double lastMouseLon = 0.0;
  double lastMouseZ = 0.0;
  
  // Double-click detection
  double lastClickTime = 0.0;
  double lastClickX = 0.0;
  double lastClickY = 0.0;
  bool waitingForDoubleClick = false;
  
  // Navigation settings - JS parity
  bool lockNorth = false;              // JS parity: uo.GLOBE_LOCK_NORTH = false
  bool mouseWheelZoomToCursor = true; // api_SetMouseWheelMode (true = zoom to cursor)
  bool mouseWheelReverse = false;      // api_SetMouseWheelDirection
  double navigationSpeed = NAVIGATION_SPEED_DEFAULT;
  double arrowKeySpeed = ARROW_KEY_SPEED_DEFAULT;
  std::string language = "tr";
  int flashPeriod = 800;
  
  // Phase 16: Coords for cursor sync
  // These can be changed by api_SetMinNavigationLOD, api_SetMaxNavigationLOD, etc.
  int navMinLOD = GLOBE_DEFAULT_MIN_LOD;      // JS: Ta.GLOBE_MIN_LOD (changeable)
  int navMaxLOD = GLOBE_DEFAULT_MAX_LOD;      // JS: Ta.GLOBE_MAX_LOD (changeable)
  double navMinDist = GLOBE_DEFAULT_MIN_DIST; // JS: Ta.GLOBE_MIN_DIST (changeable)
  double navMaxDist = GLOBE_DEFAULT_MAX_DIST; // JS: Ta.GLOBE_MAX_DIST (changeable)
  // 2D mode flat distances - JS parity: MIN is zoomed out (large), MAX is zoomed in (small)
  double navMinFlatDist = 40075016.68;        // JS: Ta.GLOBE_MIN_FLAT_DIST (zoomed out limit)
  double navMaxFlatDist = 38.21851414258813;  // JS: Ta.GLOBE_MAX_FLAT_DIST (zoomed in limit)
  int clampCount = 0;                         // Total clamp count
  double lastClampMin = 0.0;                  // Last clamp min (normalized)
  double lastClampMax = 0.0;                  // Last clamp max (normalized)
  
  // FAZ 1: Cell/Tile creation limits - JS parity (dynamic values)
  // JS: MaxCellCanBeCreated değişkeni duruma göre değişir:
  //   - Mouse down: 5 (satır 15677)
  //   - Zoom in: 2 (satır 15737, 16150)
  //   - Zoom out: 100 (satır 15737, 16236)
  //   - Double click: 2 (satır 8816)
  //   - Idle: 100 (default)
  int maxCellCanBeCreated = GLOBE_MAX_CELL_CREATE;  // JS: MaxCellCanBeCreated (dynamic)
  int cellDivisionCount = 0;                         // JS: isCellDivision (per-frame counter)
  int maxCellCreatedToday = 0;                       // JS: MaxCellCreatedToday (peak tracking)
  
  // FAZ 1: Dinamik MaxCellCanBeCreated hesaplama
  int GetDynamicMaxCellCanBeCreated() const {
    // JS parity: farklı durumlara göre limit değişir
    // NOTE: JS uses very low values (2-5) for performance on weak devices
    // We use higher values for better visual quality during interaction
    if (dblClickActive) return 20;                   // Double click zoom
    if (dragging) return 50;                         // Mouse down/drag (was 5)
    if (wheelZoomActive) {
      // Zoom in/out - moderate limit for smooth zoom
      return 30;
    }
    if (zoomDragging) return 30;                     // Right-drag zoom
    return GLOBE_MAX_CELL_CREATE;                    // Idle = 100
  }
  
  // P3: Mesh update timing - JS parity
  double lastMeshCheckTime = 0.0;
  double lastCameraPosChangedTime = 0.0;
  
  // Inertia state for zoom drag
  double zoomInertiaVelocity = 0.0;
  bool zoomInertiaActive = false;
  
  // Left-drag rotation inertia (XuzxcV) - JS parity: arcball-based
  double rotateInertiaAngleDeg = 0.0;  // JS: FStartTurnAngleDeg (half-angle)
  bool rotateInertiaActive = false;
  double lastDragDx = 0.0;
  double lastDragDy = 0.0;
  double lastDragMoveTime = 0.0;       // JS: last drag move timestamp
  double lastSegmentStartX = 0.0;      // JS: drag segment start for inertia
  double lastSegmentStartY = 0.0;
  double lastSegmentEndX = 0.0;
  double lastSegmentEndY = 0.0;
  
  // Mid-turn animation state (XfqA5S)
  bool midTurnActive = false;
  double midTurnAngleDeg = 0.0;

  // Double-click fly animation (XLzc8e)
  bool dblClickActive = false;
  bool dblClickDistStop = false;
  double dblClickTargetLon = 0.0;
  double dblClickTargetLat = 0.0;
  double dblClickDeltaDist = 0.0;
  double dblClickTargetDist = 0.0;
  glm::dquat dblClickTargetQuat{1.0, 0.0, 0.0, 0.0};
  
  // Deferred GL Deletion Queue (P2: Smoothness)
  DeferredQueue deferredQueue;
  
  void ProcessDeferredDeletions(int limit) {
      if (limit <= 0) limit = 1000; // Safety cap
      
      int processed = 0;
      
      // Textures
      if (!deferredQueue.textures.empty()) {
          int count = std::min(static_cast<int>(deferredQueue.textures.size()), limit);
          glDeleteTextures(count, deferredQueue.textures.data());
          if (count == deferredQueue.textures.size()) {
              deferredQueue.textures.clear();
          } else {
              deferredQueue.textures.erase(deferredQueue.textures.begin(), deferredQueue.textures.begin() + count);
          }
          processed += count;
          limit -= count;
      }
      
      if (limit <= 0) return;
      
      // Buffers
      if (!deferredQueue.buffers.empty()) {
          int count = std::min(static_cast<int>(deferredQueue.buffers.size()), limit);
          glDeleteBuffers(count, deferredQueue.buffers.data());
          if (count == deferredQueue.buffers.size()) {
              deferredQueue.buffers.clear();
          } else {
              deferredQueue.buffers.erase(deferredQueue.buffers.begin(), deferredQueue.buffers.begin() + count);
          }
          processed += count;
          limit -= count;
      }
      
      if (limit <= 0) return;
      
      // VAOs
      if (!deferredQueue.vaos.empty()) {
          int count = std::min(static_cast<int>(deferredQueue.vaos.size()), limit);
          glDeleteVertexArrays(count, deferredQueue.vaos.data());
          if (count == deferredQueue.vaos.size()) {
              deferredQueue.vaos.clear();
          } else {
              deferredQueue.vaos.erase(deferredQueue.vaos.begin(), deferredQueue.vaos.begin() + count);
          }
      }
  }

  // JS: SetFlyToPoint timer-based animation (XAZfZF step function)
  // Maps to camera.js:203-252 SetFlyToPoint and _flyAnimationStep
  bool flyActive = false;              // Is fly animation running?
  bool flyDistStop = false;            // Has distance reached target?
  bool flyReady = false;               // Has orientation reached target?
  bool flyToReTried = false;           // Retry flag for orbit finding
  double flyTargetLonRad = 0.0;        // Target longitude in radians
  double flyTargetLatRad = 0.0;        // Target latitude in radians
  double flyFinalDist = 0.0;           // FFLY_FINAL_DIST - target distance
  double flyNorthAngleDeg = 0.0;       // FFlyNorthAngleDeg - target north angle
  double flyTiltAngleDeg = 0.0;        // TiltAngleDeg - target tilt
  double flyDeltaDist = 0.0;           // FDeltaDist - distance change per step
  glm::dvec3 flyTargetPoint{0.0};      // Target point on sphere (normalized)
  std::function<void()> flyCallback;   // OnFlyStopNatural callback
  
  // Wheel zoom target (for zoom-to-cursor)
  double wheelZoomTargetLon = 0.0;
  double wheelZoomTargetLat = 0.0;
  bool wheelZoomTargetValid = false;
  bool wheelZoomActive = false;
  bool wheelZoomIn = false;
  int wheelZoomStepsLeft = 0;         // Legacy batched steps (kept for fallback)
  double wheelZoomDeltaDist = 0.0;
  double wheelZoomTargetDist = 0.0;
  glm::dvec3 wheelZoomOrbitWorld{0.0, 0.0, 0.0};
  bool wheelZoomOrbitValid = false;
  
  // JS parity: Continuous wheel zoom inertia (replaces batched steps)
  double wheelZoomVelocity = 0.0;       // Current zoom velocity
  double wheelZoomStartDist = 0.0;      // Starting distance for easing
  double wheelZoomProgress = 0.0;       // Animation progress [0, 1]
  static constexpr double WHEEL_ZOOM_DURATION = 0.25;  // Animation duration in seconds
  
  JsCamera camera; // Legacy input state holder
  earth::PerspectiveCamera m_newCamera; // New 6DOF Camera System
  earth::FlightController m_flightController; // New Flight Controller
  bool is2D = false;
  
  // ============================================================================
  // 2D/FLAT MODE STATE (JS FlatNavigation parity)
  // ============================================================================
  // Mercator view bounds (in meters)
  double flat2DCenterX = 0.0;           // Center X in Mercator meters
  double flat2DCenterY = 0.0;           // Center Y in Mercator meters
  double flat2DViewWidth = Mercator::WORLD_SIZE;   // View width in meters
  double flat2DViewHeight = Mercator::WORLD_SIZE;  // View height in meters
  double flat2DZoom = 2.0;              // Current zoom level
  
  // 2D animation state (JS FlatNavigation._animateZoomFlight)
  bool flat2DAnimActive = false;
  double flat2DAnimProgress = 0.0;
  double flat2DAnimDuration = 1.0;
  double flat2DStartX = 0.0, flat2DStartY = 0.0;
  double flat2DEndX = 0.0, flat2DEndY = 0.0;
  double flat2DStartSize = 0.0, flat2DEndSize = 0.0;
  std::function<void()> flat2DCallback;
  
  // 2D pan state
  bool flat2DDragging = false;
  double flat2DDragStartX = 0.0, flat2DDragStartY = 0.0;
  double flat2DDragCenterX = 0.0, flat2DDragCenterY = 0.0;

  GLuint program = 0;
  GLint mvpLoc = -1;
  GLint texLoc = -1;
  GLint alphaLoc = -1;
  GLint colorLoc = -1;
  GLint uvScaleLoc = -1;
  GLint uvOffsetLoc = -1;
  GLint uvScale2Loc = -1;
  GLint uvOffset2Loc = -1;
  GLint tex2Loc = -1;
  GLint blendFactorLoc = -1;
  GLint edgeBlendLoc = -1;
  GLuint vectorProgram = 0;
  GLint vectorMvpLoc = -1;
  GLint vectorColorLoc = -1;
  GLint vectorPointSizeLoc = -1;
  GLint vectorScaleLoc = -1;
  GLuint loadingTexture = 0;

  // HS-style pole rendering
  PoleMesh northPoleMesh;
  PoleMesh southPoleMesh;
  GLuint poleProgram = 0;
  GLint poleMvpLoc = -1;
  GLint poleColorLoc = -1;
  
  // Modular Tile System (high-performance)
  earth::TileMeshBuilder meshBuilder;
  earth::TileLodSelector lodSelector;
  
  // Pivot Gizmo (Google Earth style target)
  GLuint pivotVao = 0;
  GLuint pivotVbo = 0;
  GLsizei pivotIndexCount = 0;
  
  // Atmosphere and Lighting removed


  std::unordered_map<std::string, Tile> tiles;  // Base layer tiles
  std::vector<Tile*> visibleTiles;
  std::unordered_map<std::string, RasterLayerTiles> layerTiles;  // Multi-layer tiles
  GLStateTracker glState;  // WebGL state tracker for performance
  int currentZoom = -1;
  double currentZoomExact = 0.0;
  int maxDrawnZoom = 0; // JS Parity: FDRAWED_MAX_LEVEL
  int drawnMaxLevel = -1;
  float drawnMaxLevelFloat = 0.0f;
  int currentCenterX = -1;
  int currentCenterY = -1;
  double lastFpsTime = 0.0;
  int frameCount = 0;
  double fpsValue = 0.0;

  std::unordered_map<std::string, VectorTile> vectorTiles;
  std::vector<VectorTile*> visibleVectorTiles;

  std::vector<std::string> meshUrls;
  size_t meshUrlIndex = 0;
  std::mutex meshUrlMutex;

  // ============================================================================
  // MUTEX LOCK HIERARCHY (to prevent deadlocks)
  // Always acquire in this order: configMutex → downloadMutex → pendingMutex → cancelMutex
  // Never hold a lower mutex while acquiring a higher one.
  // ============================================================================
  mutable std::mutex configMutex;  // Level 1: Guards config.rasterLayers access across threads
  std::mutex downloadMutex;        // Level 2: Guards downloadQueue and readyQueue
  std::condition_variable downloadCv;
  std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator> downloadQueue;
  std::queue<DownloadResult> readyQueue;
  std::atomic<bool> workerRunning{false};
  static constexpr int kNumDownloadWorkers = 8;  // Multiple concurrent downloads
  std::vector<std::thread> workers;
  std::mutex pendingMutex;           // Level 3: Guards pending* sets
  std::unordered_set<std::string> pendingRaster;
  std::unordered_set<std::string> pendingVector;
  std::unordered_map<std::string, std::unordered_set<std::string>> pendingLayerDownloads;  // Per-layer pending
  std::unordered_map<std::string, std::unordered_set<std::string>> pendingLayerSupportDownloads;
  
  // Cancelled job tracking for bandwidth optimization
  std::mutex cancelMutex;            // Level 4: Guards cancelledKeys
  std::unordered_set<SchedulerKey, SchedulerKey::Hash> cancelledKeys;
  std::mutex demMutex;
  std::condition_variable demCv;
  std::queue<DemJob> demQueue;
  std::unordered_set<std::string> pendingDemBatches;
  std::unordered_set<std::string> pendingDemCells;  // JS parity: per-cell queue (LoadCellMain)
  std::vector<DemCell> demPendingCells;
  double demBatchStartTime = 0.0;
  int demPendingMeshLevel = -1;
  int demPendingMeshN = -1;
  uint64_t demBatchCounter = 0;
  std::atomic<bool> demWorkerRunning{false};
  // std::atomic<bool> demDataUpdated{false};  // Removed
  std::vector<std::string> updatedDemKeys;
  std::mutex updatedDemMutex;
  std::thread demWorker;
  bool showUi = true;
  bool showNetworkDebug = false;
  bool vectorEnabled = false;
  bool cacheEnabledUi = true;
  bool wireframeMode = false;
  bool wireframeKeyPressed = false;  // For edge detection
  double lastFrameTime = 0.0;

  glm::dvec3 eyeLocal{0.0, 0.0, 0.0};
  glm::dvec3 eyeLocal2{0.0, 0.0, 0.0};
  glm::dvec3 upLocal{0.0, 0.0, 0.0};
  glm::dvec3 eyeJs{0.0, 0.0, 0.0};
  glm::dvec3 targetJs{0.0, 0.0, 0.0};
  glm::dvec3 fp2Js{0.0, 0.0, 0.0};  // JS: Fp2 = target - eye
  glm::dvec3 upJs{0.0, 0.0, 0.0};
  glm::dvec3 eyeWorld{0.0, 0.0, 0.0};
  glm::dvec3 targetWorld{0.0, 0.0, 0.0};
  glm::dvec3 fp2World{0.0, 0.0, 0.0};  // JS: Fp2 in world coords
  glm::dvec3 upWorld{0.0, 1.0, 0.0};
  double altitudeWorld = 0.0;
  double realViewDist = 0.0;
  double nearViewDist = 0.0;
  double tanq = 0.0;

  // Network debug log
  std::vector<NetRequestEntry> networkLog;
  std::mutex networkLogMutex;
  
  // Screen position history (JS FScreenLocPrevNext parity)
  ScreenPositionHistory positionHistory;
  
  // Image Overlays (Phase 19)
  std::vector<ImageOverlay> imageOverlays;
  std::mutex overlayMutex;
  std::vector<std::function<void()>> mainThreadTasks;
  std::mutex taskMutex;
  
  uint32_t overlayProgram = 0;
  int32_t overlayMvpLoc = -1;
  int32_t overlayScaleLoc = -1;
  int32_t overlayTexLoc = -1;
  int32_t overlayOpacityLoc = -1;
  int32_t overlayColorLoc = -1;

  // Animation state
  bool animating = false;
  double animStartTilt = 0.0;
  double animStartDist = 0.0;
  double animEndTilt = 0.0;
  double animEndDist = 0.0;
  glm::dquat animStartQuat{1.0, 0.0, 0.0, 0.0};
  glm::dquat animEndQuat{1.0, 0.0, 0.0, 0.0};
  double animDuration = 1.0;
  double animElapsed = 0.0;

  // Current view center (for GetCenterLat/Lon)
  double centerLat = 0.0;
  double centerLon = 0.0;

  // Orbit state for tilt (JS parity: tilt maintains screen center)
  bool orbitValid = false;
  glm::dvec3 orbitPoint{0.0, 0.0, 0.0};  // Point on globe at screen center
  double orbitDistFromCenter = 0.0;       // Distance from globe center
  double orbitDeltaX = 0.0;               // Orbit horizontal offset
  double orbitDeltaY = 0.0;               // Orbit vertical offset

  // Layer management
  LayerManager layerManager;
  std::unique_ptr<LabelManager> labelManager; // Phase 7
  std::unordered_map<std::string, earth::IconMap> iconMaps;
  
  // Phase 2: Scheduler Integration
  std::unique_ptr<TileScheduler> scheduler;
  std::unique_ptr<GlobeTileFetcher> tileFetcher;
  std::shared_ptr<GlobeImageDecoder> imageDecoder; // Phase 5: shared for thread safety
  
  // Phase 4: Async Elevation
  struct PendingElevationQuery {
    double lat;
    double lon;
    int lod;
    ElevationCallback callback;
    double timestamp;
  };
  std::vector<PendingElevationQuery> pendingElevationQueries;

  // Phase 6: Render Pipeline
  std::string FetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "native_globe/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    return response;
  }

  static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
  }

  struct PendingIconMap {
    std::string name;
    unsigned char* pixels;
    int w, h;
    earth::IconMap map;
    std::function<void(bool)> callback;
  };
  std::vector<PendingIconMap> pendingIconMaps;
  std::mutex iconMapMutex;
  
  void ProcessPendingIconMaps();

  void Render(double dt);
  void RenderPoles(const glm::mat4& mvp);
  void RenderTiles(const glm::mat4& mvp, const std::array<Plane, 6>& frustum, double dt);
  void RenderRasterOverlays(const glm::mat4& mvp, const std::array<Plane, 6>& frustum, double dt);
  void RenderImageOverlays(const glm::mat4& mvp); // Phase 19
  void RenderVectors(const glm::mat4& mvp, const std::array<Plane, 6>& frustum);
  void InitPivotGizmo();
  void RenderPivot(const glm::mat4& viewProj);
  void RenderAtmosphere(const glm::mat4& proj, const glm::mat4& view);

  struct DemTile {
    double llx = 0.0;
    double lly = 0.0;
    double urx = 0.0;
    double ury = 0.0;
    std::vector<double> grid;
    std::list<std::string>::iterator lruIt;
    
    // JS parity: FMB2[4][meshN*2-1] - neighbor boundary arrays for seam stitching
    // 0=left, 1=right, 2=bottom, 3=top
    // Size is meshN*2-1 (e.g., 9 for meshN=5) to hold both child tile edges
    // First meshN elements: first child edge, next meshN-1: second child edge
    std::array<std::vector<double>, 4> boundaries;
    bool boundariesValid[4] = {false, false, false, false};
    bool childBoundariesValid[4] = {false, false, false, false};  // JS: child edge data present
    
    // Immutable original edges (extracted from grid, never modified)
    // Used for sharing with neighbors to avoid stale data after eviction/reload
    std::array<std::vector<double>, 4> originalEdges;
    bool originalEdgesExtracted = false;
    
    // Extract edge values from this tile's grid
    // rowsNorthToSouth: true if row 0 is north, false if row 0 is south
    void ExtractEdges(int meshN, bool rowsNorthToSouth) {
      if (grid.size() < static_cast<size_t>(meshN * meshN)) return;
      
      const int boundarySize = meshN * 2 - 1;  // JS: De*2-1 = 9 for De=5
      
      // Left edge (column 0)
      originalEdges[0].resize(meshN);
      for (int i = 0; i < meshN; ++i) {
        originalEdges[0][i] = grid[i * meshN];  // grid[row][0]
      }
      
      // Right edge (column meshN-1)
      originalEdges[1].resize(meshN);
      for (int i = 0; i < meshN; ++i) {
        originalEdges[1][i] = grid[i * meshN + (meshN - 1)];  // grid[row][meshN-1]
      }
      
      // Bottom/Top edges depend on row ordering
      if (rowsNorthToSouth) {
        // Row 0 = north (top), Row meshN-1 = south (bottom)
        // Bottom edge (row meshN-1)
        originalEdges[2].resize(meshN);
        for (int i = 0; i < meshN; ++i) {
          originalEdges[2][i] = grid[(meshN - 1) * meshN + i];
        }
        // Top edge (row 0)
        originalEdges[3].resize(meshN);
        for (int i = 0; i < meshN; ++i) {
          originalEdges[3][i] = grid[i];
        }
      } else {
        // Row 0 = south (bottom), Row meshN-1 = north (top)
        // Bottom edge (row 0)
        originalEdges[2].resize(meshN);
        for (int i = 0; i < meshN; ++i) {
          originalEdges[2][i] = grid[i];
        }
        // Top edge (row meshN-1)
        originalEdges[3].resize(meshN);
        for (int i = 0; i < meshN; ++i) {
          originalEdges[3][i] = grid[(meshN - 1) * meshN + i];
        }
      }
      
      // Initialize boundaries with extended size for child stitching
      for (int i = 0; i < 4; ++i) {
        boundaries[i].resize(boundarySize, 0.0);
        // Copy original edge to first meshN elements
        for (int j = 0; j < meshN && j < static_cast<int>(originalEdges[i].size()); ++j) {
          boundaries[i][j] = originalEdges[i][j];
        }
        childBoundariesValid[i] = false;
      }
      originalEdgesExtracted = true;
    }
  };

  struct DemCache {
    std::unordered_map<std::string, DemTile> tiles;
    std::list<std::string> lru;
  };

  mutable DemCache dem;

  // JS parity: Get neighbor tile key based on direction
  // Direction: 0=left (-x), 1=right (+x), 2=bottom (south), 3=top (north)
  // WebMercator: Y grows southward, so bottom=y+1, top=y-1
  // X wraps at dateline: x=-1 -> 2^z-1, x=2^z -> 0
  // WGS84: Now uses Mercator indexing (Phase 1 Parity), so logic is unified.
  std::string GetNeighborDemKey(int tileX, int tileY, int level, int direction, 
                                 MeshType meshType) const {
    (void)meshType; // Unused after unified Mercator logic
    int nx = tileX, ny = tileY;
    
    switch (direction) {
      case 0: nx = tileX - 1; break;  // left (west)
      case 1: nx = tileX + 1; break;  // right (east)
      case 2: ny = tileY + 1; break;  // bottom (south) - Y grows southward
      case 3: ny = tileY - 1; break;  // top (north)
    }
    
    // Mercator wrapping logic (Applies to both XYZ_MERCATOR and WGS84 now)
    int maxTile = (1 << level);  // 2^level = number of tiles at this level
    
    // X wraps at dateline (Mercator is cylindrical)
    if (nx < 0) nx = maxTile - 1;
    else if (nx >= maxTile) nx = 0;
    
    // Y does not wrap (poles are clipped in Mercator)
    if (ny < 0 || ny >= maxTile) {
      return "";  // Invalid neighbor (beyond poles)
    }
    
    return std::to_string(nx) + ":" + std::to_string(ny) + ":" + std::to_string(level);
  }

  // JS parity: Stitch DEM tile edges with neighbors (FMB2 equivalent)
  // This shares edge height values between adjacent tiles to eliminate seams
  // Also handles child tile stitching for smooth LOD transitions
  void StitchDemNeighbors(const std::string& tileKey, int meshN, MeshType meshType) {
    auto it = dem.tiles.find(tileKey);
    if (it == dem.tiles.end()) return;
    
    DemTile& tile = it->second;
    
    // Parse tile coordinates from key
    int tileX = 0, tileY = 0, level = 0;
    std::sscanf(tileKey.c_str(), "%d:%d:%d", &tileX, &tileY, &level);
    
    const int c = meshN - 1;  // JS: c = De - 1 = 4
    const int boundarySize = meshN * 2 - 1;  // JS: De*2-1 = 9
    
    // For each edge, find neighbor and share boundary data
    // Edge 0 (left): neighbor is to the left (-x), shares its right edge
    // Edge 1 (right): neighbor is to the right (+x), shares its left edge
    // Edge 2 (bottom): neighbor is below (-y), shares its top edge
    // Edge 3 (top): neighbor is above (+y), shares its bottom edge
    
    const int neighborEdge[4] = {1, 0, 3, 2};  // Opposite edges
    
    // JS child mapping for each direction (Corrected for Y-Down Quadtree):
    // dir 0 (left): Neighbor's Right Edge -> Children TR(1), BR(3)
    // dir 1 (right): Neighbor's Left Edge -> Children TL(0), BL(2)
    // dir 2 (bottom): Neighbor's Top Edge -> Children TL(0), TR(1)
    // dir 3 (top): Neighbor's Bottom Edge -> Children BL(2), BR(3)
    const int childPairs[4][2] = {
      {1, 3},  // left: child 1 (TR), child 3 (BR)
      {0, 2},  // right: child 0 (TL), child 2 (BL)
      {0, 1},  // bottom: child 0 (TL), child 1 (TR)
      {2, 3}   // top: child 2 (BL), child 3 (BR)
    };
    // Which edge to take from each child (Neighbor's edge relative to child)
    // 0=Left, 1=Right, 2=Bottom, 3=Top
    // If Dir=0 (My Left), I want Neighbor's Right (1).
    // If Dir=1 (My Right), I want Neighbor's Left (0).
    // If Dir=2 (My Bottom), I want Neighbor's Top (3).
    // If Dir=3 (My Top), I want Neighbor's Bottom (2).
    const int childEdges[4] = {1, 0, 3, 2};  // right, left, top, bottom
    
    for (int dir = 0; dir < 4; ++dir) {
      std::string neighborKey = GetNeighborDemKey(tileX, tileY, level, dir, meshType);
      if (neighborKey.empty()) continue;  // Invalid neighbor (beyond poles)
      auto nit = dem.tiles.find(neighborKey);
      if (nit == dem.tiles.end()) {
        if (config.demDebug) {
          fprintf(stderr, "[Stitch] %s: Neighbor %s (dir %d) not in cache yet\n", 
                  tileKey.c_str(), neighborKey.c_str(), dir);
        }
        continue;
      }
      {
        DemTile& neighbor = nit->second;
        int oppEdge = neighborEdge[dir];
        
        // Ensure boundaries are properly sized
        if (tile.boundaries[dir].size() < static_cast<size_t>(boundarySize)) {
          tile.boundaries[dir].resize(boundarySize, 0.0);
        }
        if (neighbor.boundaries[oppEdge].size() < static_cast<size_t>(boundarySize)) {
          neighbor.boundaries[oppEdge].resize(boundarySize, 0.0);
        }
        
        // Share this tile's original edge to neighbor's boundary (first meshN elements)
        if (tile.originalEdgesExtracted && 
            tile.originalEdges[dir].size() == static_cast<size_t>(meshN)) {
          for (int i = 0; i < meshN; ++i) {
            neighbor.boundaries[oppEdge][i] = tile.originalEdges[dir][i];
          }
          neighbor.boundariesValid[oppEdge] = true;
        } else if (config.demDebug) {
             fprintf(stderr, "[Stitch] %s -> %s (dir %d): My edges not ready (extracted=%d, size=%zu, expected=%d)\n",
                     tileKey.c_str(), neighborKey.c_str(), dir, 
                     tile.originalEdgesExtracted, tile.originalEdges[dir].size(), meshN);
        }
        
        // Get neighbor's original edge for this tile's boundary
        if (neighbor.originalEdgesExtracted && 
            neighbor.originalEdges[oppEdge].size() == static_cast<size_t>(meshN)) {
          for (int i = 0; i < meshN; ++i) {
            tile.boundaries[dir][i] = neighbor.originalEdges[oppEdge][i];
          }
          tile.boundariesValid[dir] = true;
          
          if (config.demDebug) {
              fprintf(stderr, "[Stitch] %s: Acquired edge %d from %s\n", 
                      tileKey.c_str(), dir, neighborKey.c_str());
          }
        } else if (config.demDebug) {
             fprintf(stderr, "[Stitch] %s <- %s (dir %d): Neighbor edges not ready (extracted=%d, size=%zu, expected=%d)\n",
                     tileKey.c_str(), neighborKey.c_str(), dir, 
                     neighbor.originalEdgesExtracted, neighbor.originalEdges[oppEdge].size(), meshN);
        }
        
        // JS parity: Child tile stitching (FMB2[dir][t+c] pattern)
        // Check if neighbor has child tiles and stitch their edges
        int childLevel = level + 1;
        int child1Idx = childPairs[dir][0];
        int child2Idx = childPairs[dir][1];
        
        // Calculate child tile coordinates
        int neighborChildX[4] = {
          2 * (tileX + (dir == 1 ? 1 : (dir == 0 ? -1 : 0))),
          2 * (tileX + (dir == 1 ? 1 : (dir == 0 ? -1 : 0))) + 1,
          2 * (tileX + (dir == 1 ? 1 : (dir == 0 ? -1 : 0))),
          2 * (tileX + (dir == 1 ? 1 : (dir == 0 ? -1 : 0))) + 1
        };
        int neighborChildY[4] = {
          2 * (tileY + (dir == 2 ? 1 : (dir == 3 ? -1 : 0))),
          2 * (tileY + (dir == 2 ? 1 : (dir == 3 ? -1 : 0))),
          2 * (tileY + (dir == 2 ? 1 : (dir == 3 ? -1 : 0))) + 1,
          2 * (tileY + (dir == 2 ? 1 : (dir == 3 ? -1 : 0))) + 1
        };
        
        // Get child keys based on neighbor position
        int nX = tileX + (dir == 1 ? 1 : (dir == 0 ? -1 : 0));
        int nY = tileY + (dir == 2 ? 1 : (dir == 3 ? -1 : 0));
        std::string child1Key = std::to_string(2*nX + (child1Idx & 1)) + ":" + 
                                std::to_string(2*nY + (child1Idx >> 1)) + ":" + 
                                std::to_string(childLevel);
        std::string child2Key = std::to_string(2*nX + (child2Idx & 1)) + ":" + 
                                std::to_string(2*nY + (child2Idx >> 1)) + ":" + 
                                std::to_string(childLevel);
        
        auto c1it = dem.tiles.find(child1Key);
        auto c2it = dem.tiles.find(child2Key);
        
        int edgeIdx = childEdges[dir];
        
        // JS: FMB2[dir][t] = child1.FMesh[t][edge], FMB2[dir][t+c] = child2.FMesh[t][edge]
        if (c1it != dem.tiles.end() && c1it->second.originalEdgesExtracted &&
            c2it != dem.tiles.end() && c2it->second.originalEdgesExtracted) {
          const auto& child1Edge = c1it->second.originalEdges[edgeIdx];
          const auto& child2Edge = c2it->second.originalEdges[edgeIdx];
          
          if (child1Edge.size() == static_cast<size_t>(meshN) &&
              child2Edge.size() == static_cast<size_t>(meshN)) {
            // First child fills [0..meshN-1]
            for (int t = 0; t < meshN; ++t) {
              tile.boundaries[dir][t] = child1Edge[t];
            }
            // Second child fills [c..c+meshN-1] = [meshN-1..2*meshN-2]
            for (int t = 0; t < meshN; ++t) {
              tile.boundaries[dir][t + c] = child2Edge[t];
            }
            tile.childBoundariesValid[dir] = true;
          }
        }
      }
    }
  }

  // Thread-safe: Mark tile for mesh rebuild (GL cleanup happens on main thread)
  // Called from DEM worker thread - must NOT make OpenGL calls
  void InvalidateTileMesh(const std::string& key) {
    // Check base layer - just mark for rebuild, no GL calls
    auto it = tiles.find(key);
    if (it != tiles.end()) {
      Tile& tile = it->second;
      // Mark for rebuild by resetting edgeFlags - SyncRasterTiles will rebuild
      tile.edgeFlags = EDGE_NONE - 1;  // Invalid value triggers rebuild
      tile.demPending = true;  // Force DEM re-sample
    }
    
    // Check overlay layers
    for (auto& layerPair : layerTiles) {
      RasterLayerTiles& layer = layerPair.second;
      auto lit = layer.tiles.find(key);
      if (lit != layer.tiles.end()) {
        Tile& tile = lit->second;
        tile.edgeFlags = EDGE_NONE - 1;
        tile.demPending = true;
      }
    }
  }

  // Force mesh rebuild for a tile and its neighbors (used after DEM stitching)
  void InvalidateTileAndNeighbors(const std::string& tileKey) {
    int tileX = 0, tileY = 0, level = 0;
    if (std::sscanf(tileKey.c_str(), "%d:%d:%d", &tileX, &tileY, &level) != 3) {
      return;
    }
    
    auto invalidateWithChildren = [&](int z, int x, int y) {
        // Always invalidate the target level
        InvalidateTileMesh(MakeTileKey(z, x, y));
        
        // [P2 Fix] In WGS84 mode, if we are at the max DEM LOD, we must also invalidate 
        // higher-LOD raster tiles that implicitly use this DEM tile.
        if (config.meshType == MeshType::WGS84 && z >= config.wgs84MaxLOD) {
            std::vector<std::tuple<int, int, int>> queue;
            queue.emplace_back(z, x, y);
            
            // Limit depth to max zoom (e.g. 22)
            int maxDepth = config.maxZoom; 
            size_t head = 0;
            
            while(head < queue.size()) {
                auto [cz, cx, cy] = queue[head++];
                if (cz >= maxDepth) continue;
                
                int nz = cz + 1;
                int nx = cx * 2;
                int ny = cy * 2;
                
                // Only traverse if children exist in cache (Base or Overlays)
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        std::string key = MakeTileKey(nz, nx+dx, ny+dy);
                        
                        bool exists = (tiles.find(key) != tiles.end());
                        if (!exists && !layerTiles.empty()) {
                            for (const auto& kv : layerTiles) {
                                if (kv.second.tiles.find(key) != kv.second.tiles.end()) {
                                    exists = true;
                                    break;
                                }
                            }
                        }
                        
                        if (exists) {
                            InvalidateTileMesh(key);
                            queue.emplace_back(nz, nx+dx, ny+dy);
                        }
                    }
                }
            }
        }
    };
    
    // Phase 1 Parity: WGS84 now uses Mercator indexing, so we treat it same as XYZ_MERCATOR
    // for neighbor invalidation.
    // DEM tile coords match raster tile coords.
    invalidateWithChildren(level, tileX, tileY);
    
    int n = 1 << level;
    auto wrapX = [n](int x) { return (x + n) % n; };
    
    // Neighbors (Left, Right, Top, Bottom)
    invalidateWithChildren(level, wrapX(tileX - 1), tileY);
    invalidateWithChildren(level, wrapX(tileX + 1), tileY);
    if (tileY > 0) invalidateWithChildren(level, tileX, tileY - 1);
    if (tileY < n - 1) invalidateWithChildren(level, tileX, tileY + 1);
  }

  std::string NextMeshUrl() {
    std::lock_guard<std::mutex> lock(meshUrlMutex);
    if (!meshUrls.empty()) {
      if (meshUrlIndex >= meshUrls.size()) {
        meshUrlIndex = 0;
      }
      return meshUrls[meshUrlIndex++];
    }
    return config.demBaseUrl;
  }

  void LogNetworkRequest(const std::string& url, const std::string& type) {
    std::lock_guard<std::mutex> lock(networkLogMutex);
    NetRequestEntry entry;
    entry.url = url;
    entry.type = type;
    entry.status = NetRequestStatus::Pending;
    entry.startTime = glfwGetTime();
    entry.endTime = 0.0;
    entry.bytes = 0;
    networkLog.push_back(entry);
    while (networkLog.size() > kMaxNetworkLogEntries) {
      networkLog.erase(networkLog.begin());
    }
  }

  void UpdateNetworkRequest(const std::string& url, NetRequestStatus status, size_t bytes) {
    std::lock_guard<std::mutex> lock(networkLogMutex);
    for (auto it = networkLog.rbegin(); it != networkLog.rend(); ++it) {
      if (it->url == url && it->status == NetRequestStatus::Pending) {
        it->status = status;
        it->endTime = glfwGetTime();
        it->bytes = bytes;
        break;
      }
    }
  }

  ScreenPosition GetCurrentScreenPosition() const {
    ScreenPosition pos;
    pos.lonDeg = JsRadianToDegree(camera.ea.z);
    pos.latDeg = -JsRadianToDegree(camera.ea.y);
    pos.dist = camera.dist;
    pos.tiltDeg = camera.tiltDeg;
    pos.northAngleDeg = JsRadianToDegree(camera.ea.x);
    return pos;
  }

  // JS parity: Save current screen position to history
  void SaveScreenPosition() {
    positionHistory.SavePosition(GetCurrentScreenPosition());
  }

  // JS parity: SetLastScreenPosition (no history write)
  void ApplyScreenPosition(const ScreenPosition& pos) {
    StopAnimation(false);
    camera.ea.x = lockNorth ? 0.0 : JsDegreeToRadian(pos.northAngleDeg);
    camera.ea.y = -JsDegreeToRadian(pos.latDeg);
    camera.ea.z = JsDegreeToRadian(pos.lonDeg);
    JsEulToHMatrix(camera.ea, camera.arcball.abQuat);

    camera.dist = pos.dist;
    if (camera.dist < navMinDist) camera.dist = navMinDist;
    if (camera.dist > navMaxDist) camera.dist = navMaxDist;

    if (is2D) {
      camera.tiltDeg = 0.0;
    } else {
      double tilt = pos.tiltDeg;
      if (tilt <= 0.0) tilt = GLOBE_MIN_TILTANGLE;
      if (tilt < GLOBE_MIN_TILTANGLE) tilt = GLOBE_MIN_TILTANGLE;
      if (tilt > GLOBE_MAX_TILTANGLE) tilt = GLOBE_MAX_TILTANGLE;
      camera.tiltDeg = tilt;
    }
    UpdateArcballMatrices();
  }

  // JS: Xk7IZX - Stop any running animation
  // Maps to camera.js:467-480 StopAnimation
  void StopAnimation(bool updateGlobe) {
    flyActive = false;
    flyDistStop = false;
    flyReady = false;
    dblClickActive = false;
    wheelZoomActive = false;
    midTurnActive = false;
    rotateInertiaActive = false;
    zoomInertiaActive = false;
    animating = false;
    
    camMode = FCamMode::IDLE;
    
    if (updateGlobe) {
      // JS: XBx8u3() - check globe ready state
      UpdateCameraDerived();
    }
  }
  
  // JS: SetFlyToPoint - Start fly animation to a point
  // Maps to camera.js:211-252 SetFlyToPoint
  void SetFlyToPoint(double lonDeg, double latDeg, double distMeters, 
                     double northAngleDeg, double tiltAngleDeg,
                     std::function<void()> callback = nullptr) {
    SaveScreenPosition();  // JS parity
    StopAnimation(false);
    
    camMode = FCamMode::FLYING;
    flyActive = true;
    flyCallback = callback;
    flyTiltAngleDeg = tiltAngleDeg;
    flyToReTried = false;
    
    // Convert to radians
    flyTargetLonRad = JsDegreeToRadian(lonDeg);
    flyTargetLatRad = JsDegreeToRadian(latDeg);
    
    // Clamp distance using dynamic nav limits
    double scaledDist = distMeters * GLOBE_RADIUS_K;
    flyFinalDist = (scaledDist > navMaxDist) ? navMaxDist : scaledDist;
    if (flyFinalDist < navMinDist) flyFinalDist = navMinDist;
    
    // Normalize north angle to [-180, 180]
    flyNorthAngleDeg = northAngleDeg;
    while (flyNorthAngleDeg < -180.0) flyNorthAngleDeg += 360.0;
    while (flyNorthAngleDeg > 180.0) flyNorthAngleDeg -= 360.0;
    
    flyDistStop = false;
    flyReady = false;
    
    // Pre-calculate target point on sphere
    flyTargetPoint = JsGeoTo3D(flyTargetLonRad, flyTargetLatRad, 1.0);  // Unit sphere
  }
  
  // JS: XAZfZF - Fly animation step (called each frame)
  // Maps to camera.js:612-615 _flyAnimationStep
  void UpdateFlyAnimation(double dt) {
    if (!flyActive) return;
    
    UpdateCameraDerived();
    
    // JS: Calculate step size based on altitude ratio
    // stepDiv = realViewDist / (2 * navMaxDist), clamped
    double stepDiv = altitudeWorld / (2.0 * navMaxDist);
    if (stepDiv < 0.002) stepDiv = 0.002;
    if (stepDiv > 0.2) stepDiv = 0.2;
    
    // JS: Find current orbit point (XBx8u3 logic)
    if (!orbitValid && !flyToReTried) {
      glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                        static_cast<float>(config.windowWidth) / config.windowHeight,
                                        0.01f, 100.0f * GLOBE_RADIUS);
      glm::mat4 view = GetViewMatrix();
      FindOrbitPoint(proj, view);
      flyToReTried = true;
    }
    
    // JS: Current view direction on sphere
    glm::dvec3 currentPoint = orbitValid ? glm::normalize(orbitPoint) 
                                         : glm::dvec3(1.0, 0.0, 0.0);
    
    // JS: Rotate towards target using FlyRotate
    bool incremental = camera.arcball.FlyRotate(flyTargetPoint, currentPoint, 
                                                1.0 / stepDiv, 0.5);
    
    // Update euler angles from arcball
    JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
    
    // JS: Handle north angle interpolation
    if (!lockNorth) {
      double currentNorthDeg = JsRadianToDegree(camera.ea.x);
      double northDiff = flyNorthAngleDeg - currentNorthDeg;
      // Normalize diff
      while (northDiff > 180.0) northDiff -= 360.0;
      while (northDiff < -180.0) northDiff += 360.0;
      
      if (std::abs(northDiff) > 0.1) {
        // Gradual north angle adjustment using GLOBE_FLYTO_NORTH_DIV constant
        camera.ea.x += JsDegreeToRadian(northDiff / GLOBE_FLYTO_NORTH_DIV);
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
    } else {
      camera.ea.x = 0.0;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    }
    
    // JS: Distance interpolation
    if (!flyDistStop) {
      double distDiff = flyFinalDist - camera.dist;
      if (std::abs(distDiff) > navMinDist * 0.1) {
        // Gradual distance adjustment
        double distStep = distDiff * stepDiv * 2.0;
        camera.dist += distStep;
        ClampCameraDistance();
      } else {
        camera.dist = flyFinalDist;
        flyDistStop = true;
      }
    }
    
    // JS: Tilt interpolation using GLOBE_FLYTO_TILT3D2D constant
    if (!is2D && std::abs(camera.tiltDeg - flyTiltAngleDeg) > 0.1) {
      double tiltDiff = flyTiltAngleDeg - camera.tiltDeg;
      camera.tiltDeg += tiltDiff * stepDiv * GLOBE_FLYTO_TILT3D2D;
      if (camera.tiltDeg < GLOBE_MIN_TILTANGLE) camera.tiltDeg = GLOBE_MIN_TILTANGLE;
      if (camera.tiltDeg > GLOBE_MAX_TILTANGLE) camera.tiltDeg = GLOBE_MAX_TILTANGLE;
    }
    
    // JS: Check if animation is complete
    flyReady = !incremental;
    
    if (flyDistStop && flyReady) {
      // Animation complete
      flyActive = false;
      camMode = FCamMode::IDLE;
      
      // Call completion callback if set
      if (flyCallback) {
        flyCallback();
        flyCallback = nullptr;
      }
    }
  }

  // ============================================================================
  // 2D/FLAT MODE NAVIGATION (JS FlatNavigation parity)
  // ============================================================================
  
  // JS: FlatNavigation.FlyToPoint
  void FlyToPoint2D(double lonDeg, double latDeg, double viewSizeMeters, 
                    std::function<void()> callback = nullptr) {
    flat2DAnimActive = false;
    flat2DCallback = callback;
    
    // Convert to Mercator
    double targetX = Mercator::LonToMerc(lonDeg);
    double targetY = Mercator::LatToMerc(latDeg);
    
    // Calculate target zoom from view size
    double targetZoom = std::log2(Mercator::WORLD_SIZE / viewSizeMeters);
    targetZoom = std::clamp(targetZoom, static_cast<double>(GLOBE_MIN_LOD), 
                            static_cast<double>(GLOBE_MAX_LOD));
    
    double targetSize = Mercator::WORLD_SIZE / std::pow(2.0, targetZoom);
    
    // Start animation
    flat2DStartX = flat2DCenterX;
    flat2DStartY = flat2DCenterY;
    flat2DEndX = targetX;
    flat2DEndY = targetY;
    flat2DStartSize = std::max(flat2DViewWidth, flat2DViewHeight);
    flat2DEndSize = targetSize;
    flat2DAnimProgress = 0.0;
    flat2DAnimDuration = 1.0 / navigationSpeed;
    flat2DAnimActive = true;
    camMode = FCamMode::FLYING;
  }
  
  // JS: FlatNavigation.FlyToPointDirect
  void FlyToPointDirect2D(double lonDeg, double latDeg, double viewSizeMeters) {
    flat2DAnimActive = false;
    
    flat2DCenterX = Mercator::LonToMerc(lonDeg);
    flat2DCenterY = Mercator::LatToMerc(latDeg);
    
    double targetZoom = std::log2(Mercator::WORLD_SIZE / viewSizeMeters);
    flat2DZoom = std::clamp(targetZoom, static_cast<double>(GLOBE_MIN_LOD), 
                            static_cast<double>(GLOBE_MAX_LOD));
    
    double size = Mercator::WORLD_SIZE / std::pow(2.0, flat2DZoom);
    flat2DViewWidth = size * config.windowWidth / config.windowHeight;
    flat2DViewHeight = size;
  }
  
  // JS: FlatNavigation.FlyToRegion
  void FlyToRegion2D(double minLon, double minLat, double maxLon, double maxLat,
                     std::function<void()> callback = nullptr) {
    // Swap if needed
    if (minLon > maxLon) std::swap(minLon, maxLon);
    if (minLat > maxLat) std::swap(minLat, maxLat);
    
    flat2DAnimActive = false;
    flat2DCallback = callback;
    
    // Convert to Mercator
    double mercMinX = Mercator::LonToMerc(minLon);
    double mercMinY = Mercator::LatToMerc(minLat);
    double mercMaxX = Mercator::LonToMerc(maxLon);
    double mercMaxY = Mercator::LatToMerc(maxLat);
    
    double targetX = (mercMinX + mercMaxX) / 2.0;
    double targetY = (mercMinY + mercMaxY) / 2.0;
    
    double dx = mercMaxX - mercMinX;
    double dy = mercMaxY - mercMinY;
    double targetSize = std::max(dx, dy) * 1.1;  // 10% padding
    
    double targetZoom = std::log2(Mercator::WORLD_SIZE / targetSize);
    targetZoom = std::clamp(targetZoom, static_cast<double>(GLOBE_MIN_LOD), 
                            static_cast<double>(GLOBE_MAX_LOD));
    targetSize = Mercator::WORLD_SIZE / std::pow(2.0, targetZoom);
    
    // Start animation
    flat2DStartX = flat2DCenterX;
    flat2DStartY = flat2DCenterY;
    flat2DEndX = targetX;
    flat2DEndY = targetY;
    flat2DStartSize = std::max(flat2DViewWidth, flat2DViewHeight);
    flat2DEndSize = targetSize;
    flat2DAnimProgress = 0.0;
    flat2DAnimDuration = 1.0 / navigationSpeed;
    flat2DAnimActive = true;
    camMode = FCamMode::FLYING;
  }
  
  // JS: FlatNavigation.FlyToRegionDirect
  void FlyToRegionDirect2D(double minLon, double minLat, double maxLon, double maxLat) {
    if (minLon > maxLon) std::swap(minLon, maxLon);
    if (minLat > maxLat) std::swap(minLat, maxLat);
    
    flat2DAnimActive = false;
    
    double mercMinX = Mercator::LonToMerc(minLon);
    double mercMinY = Mercator::LatToMerc(minLat);
    double mercMaxX = Mercator::LonToMerc(maxLon);
    double mercMaxY = Mercator::LatToMerc(maxLat);
    
    flat2DCenterX = (mercMinX + mercMaxX) / 2.0;
    flat2DCenterY = (mercMinY + mercMaxY) / 2.0;
    
    double dx = mercMaxX - mercMinX;
    double dy = mercMaxY - mercMinY;
    double targetSize = std::max(dx, dy) * 1.1;
    
    flat2DZoom = std::log2(Mercator::WORLD_SIZE / targetSize);
    flat2DZoom = std::clamp(flat2DZoom, static_cast<double>(GLOBE_MIN_LOD), 
                            static_cast<double>(GLOBE_MAX_LOD));
    
    double size = Mercator::WORLD_SIZE / std::pow(2.0, flat2DZoom);
    flat2DViewWidth = size * config.windowWidth / config.windowHeight;
    flat2DViewHeight = size;
  }
  
  // JS: FlatNavigation._animateZoomFlight (van Wijk algorithm)
  // Reference: "Smooth and efficient zooming and panning" - van Wijk & Nuij
  void Update2DAnimation(double dt) {
    if (!is2D || !flat2DAnimActive) return;
    
    // van Wijk constants (JS parity)
    constexpr double RHO = 1.42;    // JS: rho parameter
    constexpr double RHO2 = RHO * RHO;
    constexpr double RHO4 = RHO2 * RHO2;
    
    // Helper functions
    auto sqr = [](double x) { return x * x; };
    auto cosh = [](double x) { return (std::exp(x) + std::exp(-x)) / 2.0; };
    auto sinh = [](double x) { return (std::exp(x) - std::exp(-x)) / 2.0; };
    // JS parity: main.js FlatNavigation uses log(-x + sqrt(x^2+1)) = asinh(-x)
    auto asinh = [&sqr](double x) { return std::log(-x + std::sqrt(sqr(x) + 1.0)); };
    
    // Calculate u1 (distance between start and end positions)
    double u0 = 0.0;
    double u1 = std::sqrt(sqr(flat2DEndX - flat2DStartX) + sqr(flat2DEndY - flat2DStartY));
    
    double w0 = flat2DStartSize;
    double w1 = flat2DEndSize;
    
    // Special case: zoom-only flight (no position change)
    // When u1 ≈ 0, the van Wijk algorithm breaks (division by zero)
    // Use simple eased zoom interpolation instead
    constexpr double ZOOM_ONLY_THRESHOLD = 1.0;  // 1 meter threshold
    bool isZoomOnly = (u1 < ZOOM_ONLY_THRESHOLD);
    
    double size = w1;
    double easedProgress = 0.0;
    bool completed = false;
    
    if (isZoomOnly) {
      // Simple zoom-only animation with easing
      // Duration based on zoom ratio
      double zoomRatio = std::abs(std::log2(w1 / w0));
      double duration = std::max(0.3, zoomRatio * 0.3);  // At least 0.3s, scale with zoom change
      
      flat2DAnimProgress += dt * navigationSpeed;
      double t = flat2DAnimProgress / duration;
      
      if (t >= 1.0) {
        t = 1.0;
        completed = true;
      }
      
      // Easing: t * (2 - t)
      easedProgress = t * (2.0 - t);
      
      // Interpolate size (logarithmic for smooth zoom)
      double logW0 = std::log(w0);
      double logW1 = std::log(w1);
      size = std::exp(logW0 + (logW1 - logW0) * easedProgress);
      
    } else {
      // Full van Wijk algorithm for position + zoom
      auto calcR = [&](bool isEnd) {
        double w = isEnd ? w1 : w0;
        double sign = isEnd ? -1.0 : 1.0;
        return (sqr(w1) - sqr(w0) + sign * RHO4 * sqr(u1 - u0)) / (2.0 * w * RHO2 * (u1 - u0));
      };
      
      double r0 = asinh(calcR(false));
      double S = (asinh(calcR(true)) - r0) / RHO;
      
      // Handle degenerate S (can happen with certain w0/w1/u1 combinations)
      if (std::isnan(S) || S <= 0.0) {
        S = 1.0;
      }
      
      // Update progress based on speed
      flat2DAnimProgress += dt * navigationSpeed;
      double s = flat2DAnimProgress;
      
      if (s >= S) {
        s = S;
        completed = true;
      }
      
      // van Wijk u(s) - position along path
      double wFactor = w0 / RHO2;
      double t = RHO * s + r0;
      double uVal = wFactor * cosh(r0) * (sinh(t) / cosh(t)) - wFactor * sinh(r0) + u0;
      if (std::isnan(uVal)) uVal = 0.0;
      
      // van Wijk w(s) - zoom level
      double wVal = w0 * cosh(r0) / cosh(RHO * s + r0);
      if (std::isnan(wVal) || wVal <= 0.0) wVal = w1;
      
      // Calculate eased progress for position interpolation
      double progress = (uVal - u0) / (u1 - u0);
      progress = std::clamp(progress, 0.0, 1.0);
      easedProgress = progress * (2.0 - progress);
      size = wVal;
    }
    
    // Interpolate position
    flat2DCenterX = flat2DStartX + (flat2DEndX - flat2DStartX) * easedProgress;
    flat2DCenterY = flat2DStartY + (flat2DEndY - flat2DStartY) * easedProgress;
    
    // Update view size
    flat2DZoom = std::log2(Mercator::WORLD_SIZE / size);
    flat2DZoom = std::clamp(flat2DZoom, static_cast<double>(GLOBE_MIN_LOD), 
                            static_cast<double>(GLOBE_MAX_LOD));
    flat2DViewWidth = size * config.windowWidth / config.windowHeight;
    flat2DViewHeight = size;
    
    // Sync with 3D camera for LOD calculations
    camera.dist = GLOBE_RADIUS * std::pow(2.0, 22.0 - flat2DZoom) / 256.0;
    ClampCameraDistance();
    
    // Check completion
    if (completed) {
      flat2DAnimActive = false;
      camMode = FCamMode::IDLE;
      if (flat2DCallback) {
        flat2DCallback();
        flat2DCallback = nullptr;
      }
    }
  }
  
  // 2D scroll zoom
  void OnScroll2D(double yoffset) {
    double direction = mouseWheelReverse ? -yoffset : yoffset;
    double zoomDelta = direction > 0.0 ? -0.5 : 0.5;
    
    double newZoom = flat2DZoom + zoomDelta;
    newZoom = std::clamp(newZoom, static_cast<double>(GLOBE_MIN_LOD), 
                         static_cast<double>(GLOBE_MAX_LOD));
    
    flat2DZoom = newZoom;
    double size = Mercator::WORLD_SIZE / std::pow(2.0, flat2DZoom);
    flat2DViewWidth = size * config.windowWidth / config.windowHeight;
    flat2DViewHeight = size;
    
    // Sync with 3D camera
    camera.dist = GLOBE_RADIUS * std::pow(2.0, 22.0 - flat2DZoom) / 256.0;
    ClampCameraDistance();
  }

  void BeginWheelZoom(double lonDeg, double latDeg, double startDist, bool zoomIn,
                      bool orbitValidLocal, const glm::dvec3& orbitWorldLocal, bool targetValid) {
    wheelZoomTargetLon = lonDeg;
    wheelZoomTargetLat = latDeg;
    wheelZoomTargetValid = targetValid;

    // HS-style zoom factor: 1/1.5 for zoom in, 1.5 for zoom out
    const double HS_ZOOM_FACTOR = 1.5;
    double zoomScale = zoomIn ? (1.0 / HS_ZOOM_FACTOR) : HS_ZOOM_FACTOR;
    
    wheelZoomStartDist = camera.dist;
    // P2: Use orbit distance for scaling to match JS behavior
    double delta = startDist * (1.0 - zoomScale);
    wheelZoomTargetDist = camera.dist - delta;
    
    if (wheelZoomTargetDist < navMinDist) wheelZoomTargetDist = navMinDist;
    if (wheelZoomTargetDist > navMaxDist) wheelZoomTargetDist = navMaxDist;
    
    wheelZoomIn = zoomIn;
    wheelZoomProgress = 0.0;
    wheelZoomOrbitValid = orbitValidLocal;
    wheelZoomOrbitWorld = orbitWorldLocal;
    wheelZoomActive = true;
  }

  // HS/Mapbox-style zoom: smooth easing towards cursor position
  void OnScroll(double yoffset) {
    if (animating) animating = false;
    if (ImGui::GetIO().WantCaptureMouse) return;
    
    // 2D mode uses separate scroll handling
    if (is2D) {
      OnScroll2D(yoffset);
      return;
    }
    
    // Update modifier state before scroll (GLFW scroll callback doesn't provide mods)
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                 glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    m_flightController.OnModifiers(shift, ctrl);
    
    m_flightController.OnScroll(0.0, yoffset);
    return; // Skip legacy logic
    
    /* Legacy 3D Scroll Logic Disabled
    // HS-style: direction > 0 means zoom in, < 0 means zoom out
    double direction = mouseWheelReverse ? -yoffset : yoffset;
    const bool zoomIn = direction > 0.0;
    
    if (lastMouseOnGlobe) {
      // Find orbit point for zoom-towards-cursor
      if (!orbitValid) {
        glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                          static_cast<float>(config.windowWidth) / config.windowHeight,
                                          0.01f, 100.0f * GLOBE_RADIUS);
        glm::mat4 view = GetViewMatrix();
        FindOrbitPoint(proj, view);
      }
      
      double orbitDist = camera.dist;
      if (orbitValid) {
        orbitDist = glm::length(eyeWorld - orbitPoint);
      }
      BeginWheelZoom(lastMouseLon, lastMouseLat, orbitDist, zoomIn, 
                     orbitValid, orbitPoint, true);
    } else {
      // Zoom centered on screen if not on globe
      BeginWheelZoom(0, 0, camera.dist, zoomIn, false, glm::dvec3(0.0), false);
    }
    */
  }

  // JS Parity: Start 3D double click animation
  void StartDoubleClick3D(double latDeg, double lonDeg) {
    if (!owner) return;
    
    // 1. Save history
    SaveScreenPosition();
    
    // 2. Calculate target distance (75% zoom in from orbit point or current dist)
    double startOD = camera.dist;
    if (orbitValid) {
      startOD = glm::length(eyeWorld - orbitPoint);
    }
    
    // JS: l = 0.75 * startOD; target = startOD - l = 0.25 * startOD
    double targetDist = startOD * 0.25;
    
    // Clamp target distance
    if (targetDist < navMinDist) targetDist = navMinDist;
    
    // 3. Set animation targets
    dblClickActive = true;
    dblClickDistStop = false;
    dblClickTargetLat = latDeg;
    dblClickTargetLon = lonDeg;
    dblClickTargetDist = targetDist;
    
    // Calculate delta for animation speed (JS parity)
    // JS: delta = (l / 20) * navigationSpeed
    double l = startOD - targetDist;
    dblClickDeltaDist = (l / 20.0) * navigationSpeed;
    
    JsEuler targetEa = camera.ea;
    targetEa.y = -JsDegreeToRadian(latDeg);
    targetEa.z = JsDegreeToRadian(lonDeg);
    // targetEa.x (North) remains same
    
    RowMat3 tempMat;
    JsEulToHMatrix(targetEa, tempMat);
    dblClickTargetQuat = glm::quat_cast(ToGlmMat3(tempMat));
    
    // Stop other animations
    animating = false;
    flyActive = false;
    wheelZoomActive = false;
  }

  void OnMouseButton(int button, int action) {
    double currentTime = glfwGetTime();
    if (action == GLFW_PRESS) {
      midTurnActive = false;
      dblClickActive = false;
      StopAnimation(false);  // JS: Xk7IZX(false) on any mouse down
    }
    
    if (ImGui::GetIO().WantCaptureMouse) return;

    // Double-click detection (moved before 3D check for parity)
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
      double rawX = 0.0, rawY = 0.0;
      glfwGetCursorPos(window, &rawX, &rawY);
      float xscale = 1.0f, yscale = 1.0f;
      glfwGetWindowContentScale(window, &xscale, &yscale);
      double scaledX = rawX * xscale;
      double scaledY = rawY * yscale;
      
      double timeDiff = currentTime - lastClickTime;
      double distSq = (scaledX - lastClickX) * (scaledX - lastClickX) + 
                      (scaledY - lastClickY) * (scaledY - lastClickY);
      
      if (timeDiff < DOUBLE_CLICK_TIME && distSq < 100.0) {
        // Double-click detected!
        lastClickTime = 0.0; // Reset
        
        if (is2D) {
           // 2D logic
        } else {
           // 3D logic: Delegate to FlightController
           m_flightController.OnDoubleClick(scaledX, scaledY);
           return;
        }
      } else {
        lastClickTime = currentTime;
        lastClickX = scaledX;
        lastClickY = scaledY;
      }
    }

    if (!is2D) {
      // New 6DOF Flight Controller
      double x = cursorX;
      double y = cursorY;
      
      if (x == 0.0 && y == 0.0) {
          double rawX = 0, rawY = 0;
          glfwGetCursorPos(window, &rawX, &rawY);
          float xscale = 1.0f, yscale = 1.0f;
          glfwGetWindowContentScale(window, &xscale, &yscale);
          x = rawX * xscale;
          y = rawY * yscale;
          cursorX = x;
          cursorY = y;
      }
      
      if (action == GLFW_PRESS) {
        m_flightController.OnMouseDown(button, x, y, currentTime);
      } else if (action == GLFW_RELEASE) {
        m_flightController.OnMouseUp(button, currentTime);
      }
      // Return early to disable legacy 3D input
      return;
    }
    
    // Legacy 2D Input Logic (kept for flat mode)
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        double rawX = 0.0, rawY = 0.0;
        glfwGetCursorPos(window, &rawX, &rawY);
        lastX = rawX;
        lastY = rawY;
        cursorX = rawX;
        cursorY = rawY;
        dragStartX = rawX;
        dragStartY = rawY;
        lastDragMoveTime = currentTime;
        lastSegmentStartX = rawX;
        lastSegmentStartY = rawY;
        lastSegmentEndX = rawX;
        lastSegmentEndY = rawY;
        
        // Double-click handled above, just update click position tracking if needed
        
        // Check if click is on globe (for 3D mode gate)
        double mouseLat = 0.0, mouseLon = 0.0;
        bool onGlobe = owner && owner->ScreenToGeo(static_cast<int>(rawX), static_cast<int>(rawY), mouseLat, mouseLon);
        lastMouseOnGlobe = onGlobe;
        if (onGlobe) {
          lastMouseLat = mouseLat;
          lastMouseLon = mouseLon;
        }
        
        // 3D mode: don't start drag if not on globe
        if (!is2D && !onGlobe) {
          dragging = false;
          mouseMode = MouseMode::IDLE;
          return;
        }
        
        // 2D mode: pan instead of rotate
        if (is2D) {
          flat2DDragging = true;
          flat2DDragStartX = lastX;
          flat2DDragStartY = lastY;
          flat2DDragCenterX = flat2DCenterX;
          flat2DDragCenterY = flat2DCenterY;
          flat2DAnimActive = false;  // Stop any animation
          mouseMode = MouseMode::ROTATE;  // Reuse for 2D pan
          dragging = true;
        } else {
          // Start rotation
          dragging = true;
          mouseMode = MouseMode::ROTATE;
          
          // JS: Initialize arcball - Xgp8MD + FArcBall.XpM3pS
          UpdateArcballMatrices();
          camera.arcball.Begin(lastX, lastY);
        }
        
      } else if (action == GLFW_RELEASE) {
        dragging = false;
        flat2DDragging = false;
        if (mouseMode == MouseMode::ROTATE) {
          mouseMode = MouseMode::IDLE;
          // JS: XuzxcV - start rotation inertia on release (3D only)
          if (!is2D) {
            const double segDist = std::hypot(lastSegmentEndX - lastSegmentStartX,
                                              lastSegmentEndY - lastSegmentStartY);
            const double idleMs = (currentTime - lastDragMoveTime) * 1000.0;
            
            // JS parity: DrainInertiaArray 120ms timeout
            if (segDist > 3.0 && idleMs <= 120.0) {
              // Compute inertia angle from the last drag segment (JS XuzxcV)
              const RowMat3 savedQuat = camera.arcball.abQuat;
              
              // Skip UpdateArcballMatrices - use drag-start matrices
              camera.arcball.Begin(lastSegmentStartX, lastSegmentStartY);
              
              // Project end point to sphere to get abCurr, then cache axis BEFORE Drag()
              glm::dvec3 endSphere;
              camera.arcball.ProjectToSphere(lastSegmentEndX, lastSegmentEndY, endSphere);
              camera.arcball.abCurr = endSphere;
              camera.arcball.CacheInertiaAxis();  // Cache axis before Drag() clears it
              
              const double angleDeg = camera.arcball.Drag(lastSegmentEndX, lastSegmentEndY);
              camera.arcball.abQuat = savedQuat;  // restore current orientation
              if (angleDeg > 0.0) {
                rotateInertiaAngleDeg = angleDeg;
                rotateInertiaActive = true;
                camMode = FCamMode::TURNING;
                orbitValid = false;
              }
            }
          }
        }
        lastDragDx = 0.0;
        lastDragDy = 0.0;
      }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
      // Middle mouse: Tilt (vertical) + North angle (horizontal) - JS MidRotate
      if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        tiltDragging = true;
        mouseMode = MouseMode::TILT;
        midTurnActive = false;  // Cancel any ongoing mid-turn
        glfwGetCursorPos(window, &lastX, &lastY);
        dragStartX = lastX;
        dragStartY = lastY;
        
        // JS: SetMidRotateParams - save current state
        camera.saveTiltDeg = camera.tiltDeg;
        camera.saveDist = camera.dist;
        
      } else if (action == GLFW_RELEASE) {
        tiltDragging = false;
        if (mouseMode == MouseMode::TILT) {
          mouseMode = MouseMode::IDLE;
          // JS: XfqA5S(-(end.x - start2.x)/50) - mid-turn inertia
          if (!lockNorth) {
            double dx = lastX - dragStartX;
            if (std::abs(dx) > 1.0) {
              midTurnAngleDeg = -(dx / 50.0);
              midTurnActive = std::abs(midTurnAngleDeg) > 0.01;
            }
          }
        }
      }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      // Right mouse: Zoom (vertical drag) - JS parity
      if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        zoomDragging = true;
        mouseMode = MouseMode::ZOOM;
        glfwGetCursorPos(window, &lastX, &lastY);
        dragStartY = lastY;
        camera.saveDist = camera.dist;
        zoomInertiaVelocity = 0.0;
        zoomInertiaActive = false;
        
      } else if (action == GLFW_RELEASE) {
        zoomDragging = false;
        if (mouseMode == MouseMode::ZOOM) {
          mouseMode = MouseMode::IDLE;
          // JS: Start inertia on release (XWkyjI)
          if (std::abs(zoomInertiaVelocity) > INERTIA_THRESHOLD) {
            zoomInertiaActive = true;
          }
        }
      }
    }
  }
  
  void OnDoubleClick(double lon, double lat) {
    // JS: SetDblClick - fly to point with zoom
    SaveScreenPosition();  // JS parity
    if (animating) animating = false;
    dblClickActive = false;

    // JS: XiuzG7() - orbit distance from eye to screen-center point
    double startOD = 0.0;
    {
      glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                        static_cast<float>(config.windowWidth) / config.windowHeight,
                                        0.01f, 100.0f * GLOBE_RADIUS);
      glm::mat4 view = GetViewMatrix();
      FindOrbitPoint(proj, view);
      if (orbitValid) {
        startOD = glm::length(eyeWorld - orbitPoint);
      }
    }
    if (startOD <= 0.0) startOD = camera.dist;

    // JS: 0.75 * XiuzG7()
    double l = 0.75 * startOD;
    dblClickDeltaDist = (l / 20.0) * navigationSpeed;
    dblClickTargetDist = camera.dist - l;
    if (dblClickTargetDist < navMinDist) dblClickTargetDist = navMinDist;
    dblClickDistStop = false;

    dblClickTargetLon = lon;
    dblClickTargetLat = lat;

    // Create target orientation quaternion for the lat/lon
    JsEuler targetEa;
    targetEa.x = lockNorth ? 0.0 : camera.ea.x;
    targetEa.y = -JsDegreeToRadian(lat);
    targetEa.z = JsDegreeToRadian(lon);
    RowMat3 targetQuat{};
    JsEulToHMatrix(targetEa, targetQuat);
    dblClickTargetQuat = glm::quat_cast(ToGlmMat3(targetQuat));
    dblClickActive = true;
  }
  
  void UpdateArcballMatrices() {
    // JS: Xgp8MD - update arcball projection matrices matching render matrices
    UpdateCameraDerived();
    float aspect = static_cast<float>(config.windowWidth) / static_cast<float>(config.windowHeight);
    
    // JS: Use same near/far as render: 0.01 to 100*GLOBE_RADIUS
    double nearPlane = 0.01;
    double farPlane = 100.0 * GLOBE_RADIUS;
    
    // Sync m_newCamera projection with Render settings
    m_newCamera.SetFov(GLOBE_FOV);
    m_newCamera.SetAspectRatio(aspect);
    m_newCamera.SetNearFar(nearPlane, farPlane);

    glm::dmat4 proj = glm::perspective(glm::radians(static_cast<double>(GLOBE_FOV)), 
                                        static_cast<double>(aspect), nearPlane, farPlane);
    
    // Use the same model matrix path as rendering for consistent screen-to-globe mapping.
    glm::dvec3 up = upJs;
    if (glm::dot(up, up) < 1e-12) {
      up = glm::dvec3(0.0, 0.0, 1.0);
    }
    glm::dmat4 modelJs = glm::lookAt(glm::dvec3(0.0), glm::dvec3(-camera.dist, 0.0, 0.0), up);
    modelJs = glm::rotate(modelJs, JsDegreeToRadian(-camera.tiltDeg), glm::dvec3(0.0, 1.0, 0.0));
    modelJs = glm::translate(modelJs, -eyeLocal2);
    modelJs = glm::translate(modelJs, glm::dvec3(-GLOBE_RADIUS, 0.0, 0.0));
    glm::dmat4 modelWorld = JsToWorldMat(modelJs);
    glm::ivec4 viewport(0, 0, config.windowWidth, config.windowHeight);
    
    camera.arcball.UpdateMatrices(GLOBE_RADIUS + camera.camZ, eyeJs, proj, modelWorld, viewport);
  }

  void OnCursorPos(double xpos, double ypos) {
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    double scaledX = xpos * xscale;
    double scaledY = ypos * yscale;

    cursorX = scaledX;
    cursorY = scaledY;
    
    if (!is2D) {
      m_flightController.OnMouseMove(scaledX, scaledY, glfwGetTime());
      return;
    }
    
    // Middle mouse button: Tilt (vertical) + North angle (horizontal) - JS MidRotateMove
    // Drag DOWN = increase tilt (towards horizon), Drag UP = decrease tilt (towards top-down)
    if (tiltDragging && !is2D) {
      if (animating) animating = false;
      midTurnActive = false;  // Cancel mid-turn when dragging
      
      double dy = ypos - dragStartY;  // Total delta from start (positive = down)
      
      // Increased sensitivity for more responsive tilt control
      // Drag 200 pixels = full tilt range (0 to 80 degrees)
      const double tiltSensitivity = 0.4 * navigationSpeed;  // degrees per pixel
      double newTilt = camera.saveTiltDeg + dy * tiltSensitivity;
      
      // Clamp tilt to valid range
      if (newTilt < GLOBE_MIN_TILTANGLE) newTilt = GLOBE_MIN_TILTANGLE;
      if (newTilt > GLOBE_MAX_TILTANGLE) newTilt = GLOBE_MAX_TILTANGLE;
      
      // Horizontal drag controls north angle (rotation around vertical axis)
      // Only if not locked to north
      if (!lockNorth) {
        const double northSensitivity = 0.01 * navigationSpeed;  // radians per pixel
        double deltaNorth = (xpos - lastX) * northSensitivity;
        camera.ea.x += deltaNorth;
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }

      // Preserve screen center while tilting (keeps globe rotation anchored)
      {
        glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                          static_cast<float>(config.windowWidth) / config.windowHeight,
                                          0.01f, 100.0f * GLOBE_RADIUS);
        glm::mat4 view = GetViewMatrix();
        SetTiltPreserveCenter(newTilt, proj, view);
      }
      
      lastX = xpos;
      lastY = ypos;
      // UpdateArcballMatrices();
      return;
    }
    
    // Right mouse button: Zoom (vertical drag)
    if (zoomDragging && !is2D) {
      if (animating) animating = false;
      double dy = ypos - dragStartY;  // Total delta from start (positive = down)
      double stepDy = ypos - lastY;
      lastX = xpos;
      lastY = ypos;
      
      const double zoomSpeed = ZOOM_DRAG_SPEED * navigationSpeed * GLOBE_RADIUS_K;
      camera.dist = camera.saveDist + dy * zoomSpeed;
      ClampCameraDistance();
      zoomInertiaVelocity = stepDy * zoomSpeed;
      
      return;
    }
    
    // Left mouse button: Arcball rotation (3D) or Pan (2D)
    if (!dragging) return;
    if (animating) animating = false;
    rotateInertiaActive = false;  // Cancel inertia when dragging
    
    // Track drag delta for inertia
    double dx = xpos - lastX;
    double dy = ypos - lastY;
    lastDragDx = dx;
    lastDragDy = dy;
    
    // JS: Update segment tracking for inertia calculation
    lastSegmentStartX = lastSegmentEndX;
    lastSegmentStartY = lastSegmentEndY;
    lastSegmentEndX = xpos;
    lastSegmentEndY = ypos;
    lastDragMoveTime = glfwGetTime();
    
    lastX = xpos;
    lastY = ypos;
    
    // 2D mode: Pan the map
    if (is2D && flat2DDragging) {
      // Convert screen delta to Mercator delta
      double pixelToMeter = flat2DViewHeight / config.windowHeight;
      double deltaMercX = -(xpos - flat2DDragStartX) * pixelToMeter;
      double deltaMercY = (ypos - flat2DDragStartY) * pixelToMeter;  // Y is inverted
      
      flat2DCenterX = flat2DDragCenterX + deltaMercX;
      flat2DCenterY = flat2DDragCenterY + deltaMercY;
      
      // Sync center lon/lat with 3D camera
      double lon = Mercator::MercToLon(flat2DCenterX);
      double lat = Mercator::MercToLat(flat2DCenterY);
      camera.ea.y = -JsDegreeToRadian(lat);
      camera.ea.z = JsDegreeToRadian(lon);
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      return;
    }
    
    // JS: Update arcball matrices before each drag for smooth rotation
    // This matches JS behavior where matrices are updated continuously
    // UpdateArcballMatrices();
    
    // JS: Use arcball for rotation (3D mode)
    double rotAngle = camera.arcball.Drag(xpos, ypos);
    (void)rotAngle;
    
    // JS: LockToNorth - if enabled, reset north angle after rotation
    if (lockNorth) {
      // Extract euler from arcball result
      JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
      // Reset north angle to 0
      camera.ea.x = 0.0;
      // Rebuild quaternion with locked north
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    } else {
      // Update euler angles from arcball
      JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
    }
    
    // Clamp latitude to avoid flipping
    if (camera.ea.y > M_PI / 2.0 - 0.01) {
      camera.ea.y = M_PI / 2.0 - 0.01;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    }
    if (camera.ea.y < -M_PI / 2.0 + 0.01) {
      camera.ea.y = -M_PI / 2.0 + 0.01;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    }
  }
  
  void UpdateInertia(double dt) {
    // JS: XWkyjI - zoom inertia decay
    if (zoomInertiaActive && std::abs(zoomInertiaVelocity) > INERTIA_THRESHOLD) {
      camera.dist += zoomInertiaVelocity;
      ClampCameraDistance();
      zoomInertiaVelocity *= INERTIA_DECAY;
    } else {
      zoomInertiaActive = false;
      zoomInertiaVelocity = 0.0;
    }
    
    // JS: XuzxcV - rotation inertia decay (arcball-based)
    if (rotateInertiaActive) {
      camMode = FCamMode::TURNING;
      rotateInertiaAngleDeg *= 0.96;  // JS: continuousRotationValue decay
      UpdateCameraDerived();
      
      // JS: threshold = altitude / (2 * navMaxDist)
      const double altitude = std::max(0.0, altitudeWorld);
      const double threshold = altitude / (2.0 * navMaxDist);
      
      if (std::abs(rotateInertiaAngleDeg) < threshold) {
        rotateInertiaActive = false;
        rotateInertiaAngleDeg = 0.0;
        camMode = FCamMode::IDLE;
      } else {
        // JS: arcball.RotateByAngle(deg2rad(angle))
        const bool rotated = camera.arcball.RotateByAngle(JsDegreeToRadian(rotateInertiaAngleDeg));
        if (!rotated) {
          rotateInertiaActive = false;
          rotateInertiaAngleDeg = 0.0;
          camMode = FCamMode::IDLE;
        } else {
          JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
          if (lockNorth) {
            camera.ea.x = 0.0;
            JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
          }
          // Clamp latitude
          if (camera.ea.y > M_PI / 2.0 - 0.01) {
            camera.ea.y = M_PI / 2.0 - 0.01;
            JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
          }
          if (camera.ea.y < -M_PI / 2.0 + 0.01) {
            camera.ea.y = -M_PI / 2.0 + 0.01;
            JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
          }
          UpdateCameraDerived();
        }
      }
    }
    
    // JS: XfqA5S / X8MC9S - mid-turn rotation inertia with decay
    if (midTurnActive) {
      // Check if angle is still significant
      if (std::abs(midTurnAngleDeg) < 0.01) {
        midTurnActive = false;
        midTurnAngleDeg = 0.0;
      } else {
        if (!orbitValid) {
          glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                            static_cast<float>(config.windowWidth) / config.windowHeight,
                                            0.01f, 100.0f * GLOBE_RADIUS);
          glm::mat4 view = GetViewMatrix();
          FindOrbitPoint(proj, view);
        }
        glm::dvec3 axisWorld = orbitValid ? orbitPoint : glm::dvec3(GLOBE_RADIUS, 0.0, 0.0);
        glm::dvec3 axisJs = WorldToJs(axisWorld);
        if (glm::length(axisJs) > 0.0) {
          axisJs = glm::normalize(axisJs);
          const double halfAngle = JsDegreeToRadian(midTurnAngleDeg);
          const double sinH = std::sin(halfAngle);
          const double cosH = std::cos(halfAngle);
          RowMat3 delta = RowMat3FromQuaternion(axisJs.x * sinH, axisJs.y * sinH, axisJs.z * sinH, cosH);
          camera.arcball.abQuat = MultiplyRowMat3(camera.arcball.abQuat, delta);
          JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
          if (lockNorth) {
            camera.ea.x = 0.0;
            JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
          }
        }
        
        // JS: Decay mid-turn angle (INERTIA_DECAY = 0.92)
        midTurnAngleDeg *= INERTIA_DECAY;
      }
    }

    // JS: XLzc8e - double-click fly/zoom towards target
    if (dblClickActive) {
      glm::dquat curQ = glm::quat_cast(ToGlmMat3(camera.arcball.abQuat));
      double step = camera.dist / (2.0 * navMaxDist);
      if (step < 0.002) step = 0.002;
      if (step > 0.2) step = 0.2;
      curQ = glm::slerp(curQ, dblClickTargetQuat, step);
      camera.arcball.abQuat = FromGlmMat3(glm::mat3_cast(glm::normalize(curQ)));
      JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
      if (lockNorth) {
        camera.ea.x = 0.0;
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }

      if (!dblClickDistStop) {
        camera.dist -= dblClickDeltaDist;
        if (camera.dist <= dblClickTargetDist) {
          camera.dist = dblClickTargetDist;
          dblClickDistStop = true;
        }
        ClampCameraDistance();
      }

      double dot = std::abs(curQ.w * dblClickTargetQuat.w +
                            curQ.x * dblClickTargetQuat.x +
                            curQ.y * dblClickTargetQuat.y +
                            curQ.z * dblClickTargetQuat.z);
      dot = std::clamp(dot, 0.0, 1.0);
      double ang = 2.0 * std::acos(dot);
      if (dblClickDistStop && ang < 0.01) {
        dblClickActive = false;
      }
    }

    // JS: SetZoomWheelIn/OutDist + XYFJj7/XJG8pU - continuous smooth zoom
    if (wheelZoomActive) {
      UpdateCameraDerived();
      
      // Smooth easing: ease-out cubic
      wheelZoomProgress += dt / WHEEL_ZOOM_DURATION;
      if (wheelZoomProgress >= 1.0) {
        wheelZoomProgress = 1.0;
      }
      
      // Ease-out cubic: 1 - (1-t)^3
      double t = wheelZoomProgress;
      double easedT = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
      
      // Interpolate distance
      double newDist = wheelZoomStartDist + (wheelZoomTargetDist - wheelZoomStartDist) * easedT;
      
      // Calculate rotation amount based on progress delta
      if (wheelZoomTargetValid && mouseWheelZoomToCursor) {
        const double r = altitudeWorld / (2.0 * navMaxDist);
        const double lonRad = JsDegreeToRadian(wheelZoomTargetLon);
        const double latRad = JsDegreeToRadian(wheelZoomTargetLat);
        glm::dvec3 tempSpacePos = JsGeoTo3DRot(lonRad, latRad, GLOBE_RADIUS, camera.ea);
        if (glm::length(tempSpacePos) > 0.0) {
          tempSpacePos = glm::normalize(tempSpacePos);
        }

        glm::dvec3 orbitJs = wheelZoomOrbitValid ? WorldToJs(wheelZoomOrbitWorld)
                                                 : glm::dvec3(GLOBE_RADIUS, 0.0, 0.0);
        if (glm::length(orbitJs) > 0.0) {
          orbitJs = glm::normalize(orbitJs);
        }

        // Rotate towards cursor position during zoom
        // JS parity: interpolation factor based on zoom direction
        double rotAmount = wheelZoomIn ? 35.0 : -15.0; 
        double progressStep = dt / WHEEL_ZOOM_DURATION;
        camera.arcball.FlyRotate(tempSpacePos, orbitJs, rotAmount * progressStep, r);
        JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
      }
      
      camera.dist = newDist;
      ClampCameraDistance();
      
      if (lockNorth) {
        camera.ea.x = 0.0;
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }

      // Check completion
      if (wheelZoomProgress >= 1.0) {
        camera.dist = wheelZoomTargetDist;
        ClampCameraDistance();
        wheelZoomActive = false;
        wheelZoomProgress = 0.0;
      }
    }
  }
  
  void OnKeyInput(int key, int action) {
    if (!is2D) {
      if (action == GLFW_PRESS) {
        m_flightController.OnKeyDown(key);
      } else if (action == GLFW_RELEASE) {
        m_flightController.OnKeyUp(key);
      }
      return;
    }

    // JS: OnMove - arrow keys rotate via arcball from screen center
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (animating) animating = false;
    
    const double movePixels = arrowKeySpeed;
    const double cx = config.windowWidth * 0.5;
    const double cy = config.windowHeight * 0.5;

    auto applyMove = [&](double dx, double dy) {
      UpdateArcballMatrices();
      camera.arcball.Begin(cx, cy);
      camera.arcball.Drag(cx + dx, cy + dy);
      JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
      if (lockNorth) {
        camera.ea.x = 0.0;
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
      if (camera.ea.y > M_PI / 2.0 - 0.01) camera.ea.y = M_PI / 2.0 - 0.01;
      if (camera.ea.y < -M_PI / 2.0 + 0.01) camera.ea.y = -M_PI / 2.0 + 0.01;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    };

    switch (key) {
      case GLFW_KEY_LEFT:
        applyMove(movePixels, 0.0);
        break;
      case GLFW_KEY_RIGHT:
        applyMove(-movePixels, 0.0);
        break;
      case GLFW_KEY_UP:
        applyMove(0.0, -movePixels);
        break;
      case GLFW_KEY_DOWN:
        applyMove(0.0, movePixels);
        break;
      case GLFW_KEY_PAGE_UP:
        // Zoom in - multiplicative like wheel
        camera.dist *= 0.9;
        ClampCameraDistance();
        break;
      case GLFW_KEY_PAGE_DOWN:
        // Zoom out - multiplicative like wheel
        camera.dist *= 1.1;
        ClampCameraDistance();
        break;
      case GLFW_KEY_HOME:
        // Tilt up
        camera.tiltDeg += 5.0 * navigationSpeed;
        if (camera.tiltDeg > GLOBE_MAX_TILTANGLE) camera.tiltDeg = GLOBE_MAX_TILTANGLE;
        UpdateArcballMatrices();
        break;
      case GLFW_KEY_END:
        // Tilt down
        camera.tiltDeg -= 5.0 * navigationSpeed;
        if (camera.tiltDeg < GLOBE_MIN_TILTANGLE) camera.tiltDeg = GLOBE_MIN_TILTANGLE;
        UpdateArcballMatrices();
        break;
    }
  }

  void UpdateAnimation(double dt) {
    // JS Parity: Double click animation loop
    if (dblClickActive) {
      bool changed = false;
      
      // Distance interpolation
      if (!dblClickDistStop) {
        // JS uses frame-based step: delta = (l/20)*speed
        // We need to scale by dt to match 60fps assumption or use time-based decay
        double frameScale = dt / 0.0166666;
        double step = dblClickDeltaDist * frameScale; 
        
        if (camera.dist > dblClickTargetDist) {
          camera.dist -= step;
          if (camera.dist <= dblClickTargetDist) {
            camera.dist = dblClickTargetDist;
            dblClickDistStop = true;
          }
          changed = true;
        } else {
          dblClickDistStop = true;
        }
      }
      
      // Orientation interpolation (Slerp)
      // JS parity usually centers the point. We slerp towards dblClickTargetQuat
      {
        double t = 5.0 * dt; // Fast orientation alignment
        if (t > 1.0) t = 1.0;
        
        glm::dquat currentQ = glm::quat_cast(ToGlmMat3(camera.arcball.abQuat));
        glm::dquat nextQ = glm::slerp(currentQ, dblClickTargetQuat, t);
        camera.arcball.abQuat = FromGlmMat3(glm::mat3_cast(glm::normalize(nextQ)));
        changed = true;
      }
      
      if (dblClickDistStop) {
        // If distance reached, and orientation is reasonably close, stop
        // For simplicity, stop when distance stops, or add orientation check
        dblClickActive = false;
      }
      
      if (changed) {
        ClampCameraDistance();
        UpdateArcballMatrices();
        
        // Fix: Sync back to m_newCamera for 3D mode because UpdateCameraDerived overwrites legacy state
        if (!is2D) {
            double lat = -JsRadianToDegree(camera.ea.y);
            double lon = JsRadianToDegree(camera.ea.z);
            double heading = JsRadianToDegree(camera.ea.x);
            double altMeters = camera.dist / GLOBE_RADIUS_K;
            
            m_newCamera.SetLatLonAlt(lat, lon, altMeters);
            m_newCamera.SetHeading(heading);
            m_newCamera.SetTilt(camera.tiltDeg);
        }
      }
    }

    if (!animating) return;
    animElapsed += dt;
    float t = static_cast<float>(animElapsed / animDuration);
    if (t >= 1.0f) {
      t = 1.0f;
      animating = false;
    }
    // Smooth ease in-out
    float easeT = t * t * (3.0f - 2.0f * t);
    camera.dist = animStartDist + (animEndDist - animStartDist) * easeT;
    camera.tiltDeg = animStartTilt + (animEndTilt - animStartTilt) * easeT;
    glm::dquat q = glm::slerp(animStartQuat, animEndQuat, static_cast<double>(easeT));
    camera.arcball.abQuat = FromGlmMat3(glm::mat3_cast(glm::normalize(q)));
  }

  void StartFlyAnimation(const RowMat3& endQuat, double endTilt, double endDist, double duration) {
    animStartTilt = camera.tiltDeg;
    animStartDist = camera.dist;
    animEndTilt = endTilt;
    animEndDist = endDist;
    animStartQuat = glm::quat_cast(ToGlmMat3(camera.arcball.abQuat));
    animEndQuat = glm::quat_cast(ToGlmMat3(endQuat));
    animDuration = duration > 0.0 ? duration : 1.0;
    animElapsed = 0.0;
    animating = true;
  }

  void UpdateCenterLatLon() {
    centerLat = JsRadianToDegree(camera.camLongLat.y);
    centerLon = JsRadianToDegree(camera.camLongLat.x);
  }

  void OnFramebufferSize(int width, int height) {
    config.windowWidth = width;
    config.windowHeight = height;
    m_flightController.OnWindowResize(width, height);
    glViewport(0, 0, width, height);
  }

  void UpdateCameraDerived() {
    if (!is2D) {
      // 3D Flight Mode: m_newCamera is the source of truth (updated by FlightController)
      // We must sync legacy variables (altitudeWorld, eyeWorld, etc.) FROM m_newCamera
      // so that LOD selection and other systems continue to work.
      
      double lat, lon, alt;
      m_newCamera.GetLatLonAlt(lat, lon, alt);
      
      camera.camLongLat.y = JsDegreeToRadian(lat);
      camera.camLongLat.x = JsDegreeToRadian(lon);
      camera.tiltDeg = m_newCamera.GetTilt();
      camera.ea.x = JsDegreeToRadian(m_newCamera.GetHeading());
      
      // Update ECEF position (Scaled world coords)
      eyeWorld = m_newCamera.GetPositionECEF();
      
      // Update derived altitude
      // Note: m_newCamera.alt is absolute altitude (meters).
      // altitudeWorld expects scaled units relative to terrain?
      // Original: altitudeWorld = eyeMag - GLOBE_RADIUS - camera.camZ;
      // camera.camZ is terrain height (scaled).
      
      double heightMeters = 0.0;
      int lod = currentZoom >= 0 ? currentZoom : config.minZoom;
      if (owner && owner->SampleTerrainHeightMeters(lon, lat, lod, heightMeters)) {
        camera.camZ = heightMeters * GLOBE_RADIUS_K;
      } else {
        camera.camZ = 0.0;
      }
      
      // alt is absolute meters.
      double absoluteAltScaled = alt * GLOBE_RADIUS_K;
      altitudeWorld = absoluteAltScaled - camera.camZ;
      if (altitudeWorld < 0.0) altitudeWorld = 0.0;
      
      // Derived view distances
      realViewDist = std::sqrt(2.0 * GLOBE_RADIUS * altitudeWorld + altitudeWorld * altitudeWorld);
      const double minRealView = 0.0053333 * GLOBE_RADIUS;
      if (realViewDist < minRealView) realViewDist = minRealView;
      nearViewDist = altitudeWorld / 10.0;
      tanq = static_cast<double>(config.windowHeight) / JsDegreeToRadian(GLOBE_FOV);
      
      // Update Up vector for orbit/picking
      glm::dvec3 f, u, r;
      m_newCamera.GetBasisVectors(f, u, r);
      upWorld = u;
      
      // Update JS counterparts for arcball and legacy systems (Phase 8 Fix)
      upJs = WorldToJs(upWorld);
      eyeJs = WorldToJs(eyeWorld);

      m_newCamera.SetFov(GLOBE_FOV);
      m_newCamera.SetAspectRatio(static_cast<double>(config.windowWidth) / config.windowHeight);
      m_newCamera.SetNearFar(0.01, 100.0 * GLOBE_RADIUS);
      
      return;
    }

    if (is2D) {
      camera.tiltDeg = 0.0;
    } else {
      if (camera.tiltDeg < GLOBE_MIN_TILTANGLE) camera.tiltDeg = GLOBE_MIN_TILTANGLE;
      if (camera.tiltDeg > GLOBE_MAX_TILTANGLE) camera.tiltDeg = GLOBE_MAX_TILTANGLE;
    }
    // P0: Use dynamic nav limits instead of constants (2D uses flat limits)
    ClampCameraDistance();

    JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);

    const double tiltRad = JsDegreeToRadian(-camera.tiltDeg);
    glm::dvec2 tmp{};
    JsCSRotate(tiltRad, 0.0, 0.0, camera.dist, 0.0, tmp);
    eyeLocal = glm::dvec3(GLOBE_RADIUS + tmp.y, 0.0, tmp.x);
    eyeLocal2 = glm::dvec3(tmp.y, 0.0, tmp.x);
    JsCSRotate(tiltRad, 0.0, 0.0, 0.0, 1.0, tmp);
    upLocal = glm::dvec3(tmp.y, 0.0, tmp.x);

    glm::dvec3 eyeNorm = eyeLocal;
    const double eyeMag = glm::length(eyeNorm);
    if (eyeMag > 0.0) {
      eyeNorm /= eyeMag;
    }
    JsRot3DToGeo(eyeNorm, camera.ea, camera.camLongLat);
    double heightMeters = 0.0;
    const double lonDeg = JsRadianToDegree(camera.camLongLat.x);
    const double latDeg = JsRadianToDegree(camera.camLongLat.y);
    int lod = currentZoom >= 0 ? currentZoom : config.minZoom;
    if (owner && owner->SampleTerrainHeightMeters(lonDeg, latDeg, lod, heightMeters)) {
      camera.camZ = heightMeters * GLOBE_RADIUS_K;
    } else {
      camera.camZ = 0.0;
    }

    altitudeWorld = eyeMag - GLOBE_RADIUS - camera.camZ;
    if (altitudeWorld < 0.0) altitudeWorld = 0.0;
    realViewDist = std::sqrt(2.0 * GLOBE_RADIUS * altitudeWorld + altitudeWorld * altitudeWorld);
    const double minRealView = 0.0053333 * GLOBE_RADIUS;
    if (realViewDist < minRealView) realViewDist = minRealView;
    nearViewDist = altitudeWorld / 10.0;
    tanq = static_cast<double>(config.windowHeight) / JsDegreeToRadian(GLOBE_FOV);

    // JS: n = FEyePos (eyeLocal rotated)
    eyeJs = eyeLocal;
    JsRotate3D(eyeJs, camera.ea.x, -camera.ea.y, camera.ea.z);
    
    // JS: h = (GLOBE_RADIUS, 0, 0) rotated
    targetJs = glm::dvec3(GLOBE_RADIUS, 0.0, 0.0);
    JsRotate3D(targetJs, camera.ea.x, -camera.ea.y, camera.ea.z);
    
    // JS: Fp2 = h - n (target - eye in rotated space)
    fp2Js = targetJs - eyeJs;
    
    // JS: Fu = upLocal rotated
    upJs = upLocal;
    JsRotate3D(upJs, camera.ea.x, -camera.ea.y, camera.ea.z);

    // Convert to world coordinates (JS Y/Z swap)
    eyeWorld = JsToWorld(eyeJs);
    targetWorld = JsToWorld(targetJs);
    fp2World = JsToWorld(fp2Js);
    upWorld = glm::normalize(JsToWorld(upJs));

    // --- Google Earth Architecture Parity ---
    // Sync the new 6DOF Camera System with the legacy Input System state
    // Note: latDeg and lonDeg are already computed above
    double altMeters = altitudeWorld / GLOBE_RADIUS_K;
    
    m_newCamera.SetLatLonAlt(latDeg, lonDeg, altMeters);
    m_newCamera.SetHeading(JsRadianToDegree(camera.ea.x));
    m_newCamera.SetTilt(camera.tiltDeg);
    m_newCamera.SetRoll(0.0); // No roll support in legacy input yet
    
    // Sync Projection Params
    m_newCamera.SetFov(GLOBE_FOV);
    m_newCamera.SetAspectRatio(static_cast<double>(config.windowWidth) / config.windowHeight);
    // Near/Far planes matching ArcBall (0.01 to 100*R) - scaled units?
    // New camera expects meters for Near/Far? No, projection matrix handles units.
    // But our world is scaled. Let's use scaled units for near/far to match GL context.
    m_newCamera.SetNearFar(0.01, 100.0 * GLOBE_RADIUS);
  }

  float GetMinCameraDist() const {
    return static_cast<float>(navMinDist);
  }

  float GetMaxCameraDist() const {
    return static_cast<float>(navMaxDist);
  }

  void ClampCameraDistance() {
    if (is2D) {
      double minDist = navMaxFlatDist * GLOBE_RADIUS_K;
      double maxDist = navMinFlatDist * GLOBE_RADIUS_K;
      if (minDist > maxDist) std::swap(minDist, maxDist);
      lastClampMin = minDist;
      lastClampMax = maxDist;
      double clamped = camera.dist;
      if (clamped < minDist) clamped = minDist;
      if (clamped > maxDist) clamped = maxDist;
      if (clamped != camera.dist) {
        camera.dist = clamped;
        clampCount++;
      }
      return;
    }
    lastClampMin = navMinDist;
    lastClampMax = navMaxDist;
    double clamped = camera.dist;
    if (clamped < navMinDist) clamped = navMinDist;
    if (clamped > navMaxDist) clamped = navMaxDist;
    if (clamped != camera.dist) {
      camera.dist = clamped;
      clampCount++;
    }
  }
  
  // P0: JS parity - api_SetMinNavigationLOD
  void SetMinNavigationLOD(int lod) {
    lod = std::clamp(lod, GLOBE_DEFAULT_MIN_LOD, GLOBE_DEFAULT_MAX_LOD);
    navMinLOD = (lod > navMaxLOD) ? navMaxLOD : lod;
    double alt = GetAltitudeFromLOD(navMinLOD) * GLOBE_RADIUS_K;
    navMaxDist = (alt < navMinDist) ? navMinDist : alt;
    // Update flat dist for 2D mode
    navMinFlatDist = 2.0 * MercatorPixelToMeter(256, navMinLOD - GLOBE_DEFAULT_MIN_LOD);
    // Sync with tile selection bounds using nav LODs (not config)
    if (owner) {
      owner->SetZoomLimits(navMinLOD, navMaxLOD);
    }
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
    // If current dist exceeds new max, fly to max
    if (camera.dist > navMaxDist) {
      FlyToCurrentWithDist(navMaxDist / GLOBE_RADIUS_K);
    }
  }
  
  // P0: JS parity - api_SetMaxNavigationLOD
  void SetMaxNavigationLOD(int lod) {
    lod = std::clamp(lod, GLOBE_DEFAULT_MIN_LOD, GLOBE_DEFAULT_MAX_LOD);
    navMaxLOD = (lod < navMinLOD) ? navMinLOD : lod;
    double alt = GetAltitudeFromLOD(navMaxLOD) * GLOBE_RADIUS_K;
    navMinDist = (alt > navMaxDist) ? navMaxDist : alt;
    // Update flat dist for 2D mode
    navMaxFlatDist = 2.0 * MercatorPixelToMeter(256, navMaxLOD - GLOBE_DEFAULT_MIN_LOD);
    // Sync with tile selection bounds using nav LODs (not config)
    if (owner) {
      owner->SetZoomLimits(navMinLOD, navMaxLOD);
    }
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
    // If current dist is below new min, fly to min
    if (camera.dist < navMinDist) {
      FlyToCurrentWithDist(navMinDist / GLOBE_RADIUS_K);
    }
  }

  void SetMinNavigationDist(double distMeters) {
    navMinDist = distMeters * static_cast<double>(GLOBE_RADIUS_K);
    ClampCameraDistance();
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
  }

  void SetMaxNavigationDist(double distMeters) {
    navMaxDist = distMeters * static_cast<double>(GLOBE_RADIUS_K);
    ClampCameraDistance();
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
  }
  
  // P0: JS parity - api_SetNavigationLOD (locks to single LOD)
  void SetNavigationLOD(int lod) {
    lod = std::clamp(lod, GLOBE_DEFAULT_MIN_LOD, GLOBE_DEFAULT_MAX_LOD);
    navMinLOD = navMaxLOD = lod;
    navMinDist = navMaxDist = GetAltitudeFromLOD(lod) * GLOBE_RADIUS_K;
    navMinFlatDist = navMaxFlatDist = 2.0 * MercatorPixelToMeter(256, lod - GLOBE_DEFAULT_MIN_LOD);
    // Sync with tile selection bounds
    if (owner) {
      owner->SetZoomLimits(lod, lod);
    }
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
    // Fly to this LOD if not already there
    if (camera.dist < navMinDist || camera.dist > navMaxDist) {
      FlyToCurrentWithDist(GetAltitudeFromLOD(lod));
    }
  }
  
  // P0: JS parity - api_SetNavigationDist (preserves input distance)
  // JS: GetDistByMinMaxLOD clamps to [10, 25512548], then sets nav limits directly
  void SetNavigationDist(double distMeters) {
    // JS clamping: [GLOBE_MIN_DIST_METER=10, Sa[2]=25512548]
    constexpr double JS_MIN_DIST_METER = 10.0;
    constexpr double JS_MAX_DIST_METER = 25512548.0;  // Sa[2]
    distMeters = std::clamp(distMeters, JS_MIN_DIST_METER, JS_MAX_DIST_METER);

    // JS parity: Find LOD from Sa table (reverse lookup + interpolation)
    auto findLodFromSa = [&](double dist) -> double {
      const double minLod = static_cast<double>(GLOBE_DEFAULT_MIN_LOD);
      const double maxLod = static_cast<double>(GLOBE_DEFAULT_MAX_LOD);
      const double top = GetAltitudeFromLOD(0);
      const double bottom = GetAltitudeFromLOD(25);
      if (dist > top) return minLod;     // JS: clamp(0, min, max) -> min
      if (dist < bottom) return maxLod;  // JS: clamp(25, min, max) -> max
      for (int a = 0; a < 25; ++a) {
        double o = GetAltitudeFromLOD(a);
        double s = GetAltitudeFromLOD(a + 1);
        if (dist <= o && dist >= s) {
          if (std::abs(o - s) < 1e-9) {
            return std::clamp(static_cast<double>(a), minLod, maxLod);
          }
          double lod = static_cast<double>(a) + (o - dist) / (o - s);
          return std::clamp(lod, minLod, maxLod);
        }
      }
      return minLod;
    };

    double lodExact = findLodFromSa(distMeters);
    int lod = static_cast<int>(std::round(lodExact));
    lod = std::clamp(lod, GLOBE_DEFAULT_MIN_LOD, GLOBE_DEFAULT_MAX_LOD);

    // JS parity: preserve input distance, don't snap to Sa table
    navMinLOD = navMaxLOD = lod;
    navMinDist = navMaxDist = distMeters * GLOBE_RADIUS_K;  // preserve input
    navMinFlatDist = navMaxFlatDist =
        2.0 * MercatorPixelToMeter(256, lodExact - GLOBE_DEFAULT_MIN_LOD);
    
    // Sync with tile selection bounds
    if (owner) {
      owner->SetZoomLimits(lod, lod);
    }
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
    
    // Fly to this distance if not already there
    if (std::abs(camera.dist - navMinDist) > navMinDist * 0.01) {
      FlyToCurrentWithDist(distMeters);
    }
  }
  
  // P0: JS parity - api_CancelScreenWidthAndMinMaxLOD (reset to defaults)
  void ResetNavigationLimits() {
    navMinLOD = GLOBE_DEFAULT_MIN_LOD;
    navMaxLOD = GLOBE_DEFAULT_MAX_LOD;
    navMinDist = GLOBE_DEFAULT_MIN_DIST;
    navMaxDist = GLOBE_DEFAULT_MAX_DIST;
    navMinFlatDist = 40075016.68;           // JS: GLOBE_MIN_FLAT_DIST (zoomed out)
    navMaxFlatDist = 38.21851414258813;     // JS: GLOBE_MAX_FLAT_DIST (zoomed in)
    // Also reset tile selection bounds (JS has no separate render bounds)
    if (owner) {
      owner->SetZoomLimits(GLOBE_DEFAULT_MIN_LOD, GLOBE_DEFAULT_MAX_LOD);
    }
    m_flightController.SetNavigationLimits(navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
  }
  
  // P0: JS parity - GetDistByMinMaxLOD (clamp dist to nav limits)
  double GetDistByMinMaxLOD(double dist) const {
    if (is2D) {
      return std::clamp(dist, navMaxFlatDist, navMinFlatDist);
    }
    return std::clamp(dist, navMinDist / GLOBE_RADIUS_K, navMaxDist / GLOBE_RADIUS_K);
  }

  // JS parity: FindDistForSrcWdMeter (GeomClass)
  double FindDistForSrcWdMeter(double widthMeters) const {
    if (widthMeters <= 0.0) return widthMeters;
    double screenW = static_cast<double>(config.windowWidth);
    double screenH = static_cast<double>(config.windowHeight);
    if (screenW <= 0.0 || screenH <= 0.0) return widthMeters;
    if (is2D) {
      return FindDistForSrcWdMeter2D(widthMeters, screenW, screenH);
    }
    return FindDistForSrcWdMeter3D(widthMeters, screenW, screenH);
  }

  double FindDistForSrcWdMeter3D(double widthMeters, double screenW, double screenH) const {
    double aspect = screenW / screenH;
    if (aspect <= 0.0) return widthMeters;
    FovDistInfo info = CalculateFOVAndDistFactor(widthMeters, aspect);
    double fovRad = glm::radians(info.fov);
    double denom = aspect * 2.0 * std::tan(fovRad / 2.0) * info.distFactor;
    if (denom <= 0.0) return widthMeters;
    return widthMeters / denom;
  }

  double FindDistForSrcWdMeter2D(double widthMeters, double screenW, double screenH) const {
    double minWidth = std::min(widthMeters, widthMeters * (screenH / screenW));
    double centerLat = Mercator::MercToLat(flat2DCenterY);
    double metersAtLat = Mercator::GetMeterForLatitude(minWidth, centerLat);
    double halfWidthMeters = Mercator::GetMeterForLatitude(widthMeters, centerLat) / 2.0;
    double centerX = flat2DCenterX;
    double centerY = flat2DCenterY;
    double lonLeft = Mercator::MercToLon(centerX - halfWidthMeters);
    double latLeft = Mercator::MercToLat(centerY);
    double lonRight = Mercator::MercToLon(centerX + halfWidthMeters);
    double latRight = Mercator::MercToLat(centerY);
    double dist = GreatCircleDistanceMeters(lonLeft, latLeft, lonRight, latRight);
    if (dist <= 0.0) return widthMeters;
    return metersAtLat * (widthMeters / dist);
  }
  
  // Helper: Mercator pixel to meter conversion - JS parity
  // JS: (pixelSize - imageSize/2) * (2 * PI * R / imageSize) / pow(2, zoom)
  double MercatorPixelToMeter(int pixelSize, double zoom) const {
    constexpr double GLOBE_IMAGE_SIZE = 256.0;
    constexpr double GLOBE_RADIUS_METER = 6378137.0;
    constexpr double PI = 3.14159265358979323846;
    double metersPerTile = 2.0 * PI * GLOBE_RADIUS_METER / GLOBE_IMAGE_SIZE;
    return (pixelSize - GLOBE_IMAGE_SIZE / 2.0) * metersPerTile / std::pow(2.0, zoom);
  }
  
  // Helper: Fly to current position with new distance
  void FlyToCurrentWithDist(double altitudeMeters) {
    double lonDeg = JsRadianToDegree(camera.ea.z);
    double latDeg = -JsRadianToDegree(camera.ea.y);
    double dist = altitudeMeters * GLOBE_RADIUS_K;
    camera.dist = dist;
    ClampCameraDistance();
  }

  // JS parity: Find orbit point at screen center (XBx8u3)
  bool FindOrbitPoint(const glm::mat4& proj, const glm::mat4& view) {
    float centerX = static_cast<float>(config.windowWidth) / 2.0f;
    float centerY = static_cast<float>(config.windowHeight) / 2.0f;
    
    glm::mat4 invVP = glm::inverse(proj * view);
    float ndcX = (2.0f * centerX) / config.windowWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * centerY) / config.windowHeight;
    
    glm::vec4 rayClipNear = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayClipFar = glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    
    glm::vec4 rayWorldNear = invVP * rayClipNear;
    glm::vec4 rayWorldFar = invVP * rayClipFar;
    rayWorldNear /= rayWorldNear.w;
    rayWorldFar /= rayWorldFar.w;
    
    glm::vec3 rayOrigin = glm::vec3(rayWorldNear);
    glm::vec3 rayDir = glm::normalize(glm::vec3(rayWorldFar - rayWorldNear));
    
    // Ray-sphere intersection
    float a = glm::dot(rayDir, rayDir);
    float b = 2.0f * glm::dot(rayOrigin, rayDir);
    float c = glm::dot(rayOrigin, rayOrigin) - GLOBE_RADIUS * GLOBE_RADIUS;
    float discriminant = b * b - 4.0f * a * c;
    
    if (discriminant < 0.0f) {
      orbitValid = false;
      return false;
    }
    
    float sqrtD = std::sqrt(discriminant);
    float t1 = (-b - sqrtD) / (2.0f * a);
    float t2 = (-b + sqrtD) / (2.0f * a);
    float t = (t1 > 0.0f) ? t1 : t2;
    
    if (t < 0.0f) {
      orbitValid = false;
      return false;
    }
    
    orbitPoint = glm::dvec3(rayOrigin + rayDir * t);
    orbitValid = true;
    return true;
  }

  // JS parity: Update orbit data for tilt operations (X92zTr)
  void UpdateOrbitData() {
    if (!orbitValid) return;
    orbitDistFromCenter = glm::length(orbitPoint);
    double tiltRad = JsDegreeToRadian(camera.tiltDeg);
    orbitDeltaX = orbitDistFromCenter * std::cos(tiltRad);
    orbitDeltaY = orbitDistFromCenter * std::sin(tiltRad);
  }

  // JS parity: Set tilt while preserving screen center (OnToTilt3D2D logic)
  void SetTiltPreserveCenter(double newTilt, const glm::mat4& proj, const glm::mat4& view) {
    // Clamp new tilt
    if (newTilt < GLOBE_MIN_TILTANGLE) newTilt = GLOBE_MIN_TILTANGLE;
    if (newTilt > GLOBE_MAX_TILTANGLE) newTilt = GLOBE_MAX_TILTANGLE;
    
    // Find orbit point before tilt change
    if (!FindOrbitPoint(proj, view)) {
      // No valid orbit point, just set tilt directly
      camera.tiltDeg = newTilt;
      return;
    }
    
    // Save current orbit data
    double oldTilt = camera.tiltDeg;
    double tiltDelta = newTilt - oldTilt;
    
    if (std::abs(tiltDelta) < 0.01) return;
    
    // Calculate eye-to-orbit vector in local space (JS: FNewEye)
    glm::dvec3 eyeToOrbit = eyeWorld - orbitPoint;
    double eyeOrbitDist = glm::length(eyeToOrbit);
    
    // When tilt changes, we need to:
    // 1. Rotate the camera around the orbit point
    // 2. Keep the orbit point at the same screen position
    
    // Calculate new camera distance to maintain orbit point position
    double oldTiltRad = JsDegreeToRadian(oldTilt);
    double newTiltRad = JsDegreeToRadian(newTilt);
    
    // The camera moves on an arc around the orbit point
    // New distance from orbit to eye based on tilt change
    double orbitHeight = orbitDistFromCenter;  // Distance from globe center to orbit
    
    // Simple approach: adjust distance proportionally to tilt change
    // Higher tilt = camera moves further from orbit point
    double distRatio = std::cos(oldTiltRad) / std::cos(newTiltRad);
    if (distRatio > 0.1 && distRatio < 10.0) {
      camera.dist *= distRatio;
      if (camera.dist < navMinDist) camera.dist = navMinDist;
      if (camera.dist > navMaxDist) camera.dist = navMaxDist;
    }
    
    // Set new tilt
    camera.tiltDeg = newTilt;
  }

  glm::mat4 GetViewMatrix() {
    UpdateCameraDerived();
    
    // Use the new Google Earth-style Camera System
    // It calculates the view matrix based on Lat/Lon/Alt/Heading/Tilt
    // conforming to WGS84 ECEF (Z-up) which matches the tile mesh generation.
    glm::dmat4 viewDouble = m_newCamera.GetViewMatrix();
    
    // Cast to float for OpenGL
    return glm::mat4(viewDouble);
  }

  void InitUi() {
    if (uiInitialized) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
      std::cerr << "ImGui GLFW init failed." << std::endl;
      ImGui::DestroyContext();
      return;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
      std::cerr << "ImGui OpenGL3 init failed." << std::endl;
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
      return;
    }
    uiInitialized = true;
  }

  void ShutdownUi() {
    if (!uiInitialized) return;
    uiInitialized = false;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  void BeginUiFrame() {
    if (!uiInitialized) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

  void EndUiFrame() {
    if (!uiInitialized) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  void DrawUi() {
    if (!uiInitialized || !showUi) return;
    ImGui::Begin("Globe Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    // Info Section
    ImGui::Text("FPS: %.1f", fpsValue);
    ImGui::Text("LOD: %d (exact: %.2f)", currentZoom, currentZoomExact);
    ImGui::Text("Tiles: %zu", visibleTiles.size());
    
    ImGui::Separator();
    
    // Camera Controls
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      float distF = static_cast<float>(camera.dist);
      if (ImGui::SliderFloat("Distance", &distF, GetMinCameraDist(), GetMaxCameraDist())) {
        camera.dist = static_cast<double>(distF);
      }
      float tiltF = static_cast<float>(camera.tiltDeg);
      if (ImGui::SliderFloat("Tilt", &tiltF, 0.0f, GLOBE_MAX_TILTANGLE)) {
        camera.tiltDeg = static_cast<double>(tiltF);
        UpdateArcballMatrices();
      }
      float northDeg = static_cast<float>(JsRadianToDegree(camera.ea.x));
      if (ImGui::SliderFloat("North", &northDeg, -180.0f, 180.0f)) {
        camera.ea.x = JsDegreeToRadian(static_cast<double>(northDeg));
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
    }
    
    
    // Display Options
    if (ImGui::CollapsingHeader("Display")) {
      ImGui::Checkbox("Wireframe Mode (W)", &wireframeMode);
      cacheEnabledUi = config.useDiskCache;
      if (ImGui::Checkbox("Enable Disk Cache", &cacheEnabledUi)) {
        config.useDiskCache = cacheEnabledUi;
      }
    }
    
    // Zoom Settings (collapsed by default)
    if (ImGui::CollapsingHeader("Zoom Settings")) {
      ImGui::Checkbox("Fixed Zoom", &config.useFixedZoom);
      if (config.useFixedZoom) {
        ImGui::SliderInt("Zoom", &config.fixedZoom, 2, 22);
      } else {
        ImGui::SliderInt("Min Zoom", &config.minZoom, 2, 22);
        ImGui::SliderInt("Max Zoom", &config.maxZoom, 2, 22);
        if (config.minZoom > config.maxZoom) {
          config.minZoom = config.maxZoom;
        }
      }
    }
    
    // Performance Profiling
    if (ImGui::CollapsingHeader("Performance")) {
      ImGui::Text("Frame Time: %.2f ms", lastFrameTime * 1000.0);
      ImGui::Text("Tiles Rendered: %zu", visibleTiles.size());
      ImGui::Text("LOD Selection: %d refined", cellDivisionCount);
      
      // Texture cache stats (from tile map)
      size_t texCount = 0;
      for (const auto& kv : tiles) {
          if (kv.second.ownsTexture && kv.second.texture != 0) texCount++;
      }
      ImGui::Text("Tex Count: %zu", texCount);
      
      // DEM/Terrain stats
      ImGui::Separator();
      ImGui::Text("DEM Enabled: %s", config.demEnabled ? "YES" : "NO");
      {
        std::lock_guard<std::mutex> lock(demMutex);
        ImGui::Text("DEM Cache: %zu tiles", dem.tiles.size());
      }
      
      // Tile loading stats
      ImGui::Separator();
      int loading = 0, ready = 0, failed = 0, withTexture = 0, withData = 0;
      for (const auto& kv : tiles) {
          switch (kv.second.loadState) {
              case TileLoadState::READY: 
                  ready++; 
                  if (kv.second.ownsTexture) withTexture++;
                  if (!kv.second.decodedData.empty()) withData++;
                  break;
              case TileLoadState::FETCHING:
              case TileLoadState::DECODING:
              case TileLoadState::UPLOADING:
              case TileLoadState::SCHEDULED: loading++; break;
              case TileLoadState::FAILED: failed++; break;
              default: break;
          }
      }
      ImGui::Text("Tile States: Ready=%d (tex=%d, data=%d)", ready, withTexture, withData);
      ImGui::Text("Loading=%d Failed=%d", loading, failed);
      if (failed > 0) {
          ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "WARNING: %d tiles failed!", failed);
      }
      
      // SSE threshold slider
      float sseThresh = config.sseThresholdPx;
      if (ImGui::SliderFloat("SSE Threshold", &sseThresh, 0.5f, 10.0f)) {
        config.sseThresholdPx = sseThresh;
      }
      ImGui::Text("(Lower = more detail, higher = less tiles)");
    }
    
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    
    // ========================================================================
    // GOOGLE EARTH STYLE NAVIGATION CONTROLS (Bottom-Right)
    // ========================================================================
    const float navWidth = 90.0f;
    const float navPadding = 20.0f;
    const float compassSize = 80.0f;
    const float zoomBarHeight = 160.0f;
    const float panControlHeight = 110.0f;  // Pan joystick (3 rows)
    const float tiltControlHeight = 50.0f;  // Tilt buttons row
    const float totalNavHeight = compassSize + 24.0f + zoomBarHeight + panControlHeight + tiltControlHeight;
    
    ImGuiWindowFlags navFlags = ImGuiWindowFlags_NoTitleBar |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoMove;
    
    // Position at bottom-right
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - navWidth - navPadding, 
                                   io.DisplaySize.y - totalNavHeight - navPadding - 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(navWidth, totalNavHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    ImGui::Begin("GENavigation", nullptr, navFlags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    
    // ---- COMPASS (Top) ----
    ImVec2 compassCenter(windowPos.x + navWidth / 2.0f, windowPos.y + compassSize / 2.0f + 4.0f);
    float compassRadius = compassSize / 2.0f - 2.0f;
    
    // Compass background circle
    drawList->AddCircleFilled(compassCenter, compassRadius, IM_COL32(40, 40, 40, 220), 32);
    drawList->AddCircle(compassCenter, compassRadius, IM_COL32(100, 100, 100, 255), 32, 2.0f);
    
    // North indicator (rotates with camera)
    float northAngleRad = static_cast<float>(camera.ea.x); // Current north angle
    float northX = compassCenter.x + std::sin(northAngleRad) * (compassRadius - 8.0f);
    float northY = compassCenter.y - std::cos(northAngleRad) * (compassRadius - 8.0f);
    
    // Draw N marker (red triangle pointing north)
    ImVec2 n1(northX, northY - 8.0f);
    ImVec2 n2(northX - 5.0f, northY + 4.0f);
    ImVec2 n3(northX + 5.0f, northY + 4.0f);
    // Rotate triangle around compass center
    auto rotatePoint = [&](ImVec2 p, float angle) -> ImVec2 {
      float dx = p.x - compassCenter.x;
      float dy = p.y - compassCenter.y;
      float cosA = std::cos(angle);
      float sinA = std::sin(angle);
      return ImVec2(compassCenter.x + dx * cosA - dy * sinA,
                    compassCenter.y + dx * sinA + dy * cosA);
    };
    n1 = rotatePoint(ImVec2(compassCenter.x, compassCenter.y - compassRadius + 10.0f), northAngleRad);
    n2 = rotatePoint(ImVec2(compassCenter.x - 6.0f, compassCenter.y - compassRadius + 22.0f), northAngleRad);
    n3 = rotatePoint(ImVec2(compassCenter.x + 6.0f, compassCenter.y - compassRadius + 22.0f), northAngleRad);
    drawList->AddTriangleFilled(n1, n2, n3, IM_COL32(220, 60, 60, 255));
    
    // "N" text
    ImVec2 nTextPos = rotatePoint(ImVec2(compassCenter.x - 4.0f, compassCenter.y - compassRadius + 24.0f), northAngleRad);
    drawList->AddText(nTextPos, IM_COL32(255, 255, 255, 255), "N");
    
    // Compass center dot
    drawList->AddCircleFilled(compassCenter, 4.0f, IM_COL32(200, 200, 200, 255), 16);
    
    // Compass click detection - Reset to North
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - compassRadius, 4.0f));
    ImGui::InvisibleButton("compass_btn", ImVec2(compassSize - 4.0f, compassSize - 4.0f));
    if (ImGui::IsItemClicked()) {
      // Reset north angle to 0 (north up) - sync to m_newCamera for 3D mode
      if (!is2D) {
        m_newCamera.SetHeading(0.0);
      }
      camera.ea.x = 0.0;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Click to reset North");
    }
    
    // Frame-rate independent delta time
    const double dt = static_cast<double>(io.DeltaTime);
    const double rotateSpeed = 90.0;  // degrees per second
    const double zoomSpeed = 2.0;     // zoom factor per second (e^2 ≈ 7.4x per second)
    const double panSpeed = 200.0;    // pixels per second equivalent
    const double tiltSpeed = 60.0;    // degrees per second
    
    // Rotation buttons around compass
    const float rotBtnSize = 28.0f;
    ImGui::SetCursorPos(ImVec2(0.0f, compassSize / 2.0f - rotBtnSize / 2.0f + 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::Button("<##rot", ImVec2(rotBtnSize, rotBtnSize));
    if (ImGui::IsItemActive()) {
      double deltaHeading = -rotateSpeed * dt * navigationSpeed;
      if (!is2D) {
        // 3D mode: only update m_newCamera, UpdateCameraDerived will sync legacy
        m_newCamera.SetHeading(m_newCamera.GetHeading() + deltaHeading);
      } else {
        // 2D mode: update legacy camera
        camera.ea.x = JsDegreeToRadian(JsRadianToDegree(camera.ea.x) + deltaHeading);
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Left (hold)");
    ImGui::SetCursorPos(ImVec2(navWidth - rotBtnSize, compassSize / 2.0f - rotBtnSize / 2.0f + 4.0f));
    ImGui::Button(">##rot", ImVec2(rotBtnSize, rotBtnSize));
    if (ImGui::IsItemActive()) {
      double deltaHeading = rotateSpeed * dt * navigationSpeed;
      if (!is2D) {
        // 3D mode: only update m_newCamera, UpdateCameraDerived will sync legacy
        m_newCamera.SetHeading(m_newCamera.GetHeading() + deltaHeading);
      } else {
        // 2D mode: update legacy camera
        camera.ea.x = JsDegreeToRadian(JsRadianToDegree(camera.ea.x) + deltaHeading);
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Right (hold)");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    
    // ---- ZOOM CONTROLS ----
    float zoomStartY = compassSize + 20.0f;
    const float zoomBtnSize = 36.0f;
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - zoomBtnSize / 2.0f, zoomStartY));
    
    // Zoom In (+) - Google Earth style: multiplicative zoom, hold to continue
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::Button("+##zoom", ImVec2(zoomBtnSize, zoomBtnSize));
    if (ImGui::IsItemActive()) {
      // Hold to zoom continuously - frame-rate independent exponential zoom
      double zoomFactor = std::exp(-zoomSpeed * dt * navigationSpeed);
      if (!is2D) {
        double lat, lon, alt;
        m_newCamera.GetLatLonAlt(lat, lon, alt);
        m_newCamera.SetLatLonAlt(lat, lon, alt * zoomFactor);
      }
      camera.dist *= zoomFactor;
      ClampCameraDistance();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom In (hold for continuous)");
    
    // Zoom slider track
    float sliderStartY = zoomStartY + zoomBtnSize + 8.0f;
    float sliderHeight = zoomBarHeight - zoomBtnSize * 2 - 20.0f;
    ImVec2 sliderTL(windowPos.x + navWidth / 2.0f - 4.0f, windowPos.y + sliderStartY);
    ImVec2 sliderBR(windowPos.x + navWidth / 2.0f + 4.0f, windowPos.y + sliderStartY + sliderHeight);
    drawList->AddRectFilled(sliderTL, sliderBR, IM_COL32(60, 60, 60, 220), 3.0f);
    
    // Zoom slider thumb
    float minDist = GetMinCameraDist();
    float maxDist = GetMaxCameraDist();
    float zoomT = 1.0f - (static_cast<float>(camera.dist) - minDist) / (maxDist - minDist);
    zoomT = std::clamp(zoomT, 0.0f, 1.0f);
    float thumbY = sliderTL.y + (1.0f - zoomT) * sliderHeight;
    drawList->AddCircleFilled(ImVec2(windowPos.x + navWidth / 2.0f, thumbY), 10.0f, IM_COL32(200, 200, 200, 255), 16);
    drawList->AddCircle(ImVec2(windowPos.x + navWidth / 2.0f, thumbY), 10.0f, IM_COL32(120, 120, 120, 255), 16, 2.0f);
    
    // Zoom slider interaction
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - 15.0f, sliderStartY - windowPos.y + ImGui::GetWindowPos().y));
    ImGui::InvisibleButton("zoom_slider", ImVec2(30.0f, sliderHeight));
    if (ImGui::IsItemActive()) {
      float mouseY = io.MousePos.y;
      float newT = 1.0f - (mouseY - sliderTL.y) / sliderHeight;
      newT = std::clamp(newT, 0.0f, 1.0f);
      double newDist = maxDist - newT * (maxDist - minDist);
      if (!is2D) {
        double lat, lon, alt;
        m_newCamera.GetLatLonAlt(lat, lon, alt);
        // Convert world units to meters and clamp to valid altitude range
        double newAltMeters = (newDist - GLOBE_RADIUS) / GLOBE_RADIUS_K;
        double minAltMeters = (navMinDist - GLOBE_RADIUS) / GLOBE_RADIUS_K;
        double maxAltMeters = (navMaxDist - GLOBE_RADIUS) / GLOBE_RADIUS_K;
        newAltMeters = std::clamp(newAltMeters, std::max(0.0, minAltMeters), maxAltMeters);
        m_newCamera.SetLatLonAlt(lat, lon, newAltMeters);
      }
      camera.dist = newDist;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to zoom");
    
    // Zoom Out (-) - Google Earth style: multiplicative zoom, hold to continue
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - zoomBtnSize / 2.0f, zoomStartY + zoomBarHeight - zoomBtnSize - 4.0f));
    ImGui::Button("-##zoom", ImVec2(zoomBtnSize, zoomBtnSize));
    if (ImGui::IsItemActive()) {
      // Hold to zoom continuously - frame-rate independent exponential zoom
      double zoomFactor = std::exp(zoomSpeed * dt * navigationSpeed);
      if (!is2D) {
        double lat, lon, alt;
        m_newCamera.GetLatLonAlt(lat, lon, alt);
        m_newCamera.SetLatLonAlt(lat, lon, alt * zoomFactor);
      }
      camera.dist *= zoomFactor;
      ClampCameraDistance();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom Out (hold for continuous)");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    
    // ---- PAN/TILT CONTROLS (Bottom) - Google Earth Style ----
    // Google Earth uses a "joystick" style control:
    // - Up/Down/Left/Right arrows: Pan the globe (move view center)
    // - Center button: Reset view to default
    // - Separate tilt control via the "look" ring
    float tiltStartY = zoomStartY + zoomBarHeight + 12.0f;
    float tiltBtnSize = 32.0f;
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    
    // Helper lambda for pan - frame-rate independent, syncs to m_newCamera in 3D mode
    auto doPan = [&](double dx, double dy) {
      // Frame-rate independent: use dt and panSpeed
      double movePixels = panSpeed * dt * navigationSpeed;
      double cx = io.DisplaySize.x / 2.0;
      double cy = io.DisplaySize.y / 2.0;
      UpdateArcballMatrices();
      camera.arcball.Begin(cx, cy);
      camera.arcball.Drag(cx + dx * movePixels, cy + dy * movePixels);
      JsEulFromHMatrix(camera.arcball.abQuat, camera.ea);
      if (lockNorth) {
        camera.ea.x = 0.0;
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      }
      if (camera.ea.y > M_PI / 2.0 - 0.01) camera.ea.y = M_PI / 2.0 - 0.01;
      if (camera.ea.y < -M_PI / 2.0 + 0.01) camera.ea.y = -M_PI / 2.0 + 0.01;
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      
      // Sync to m_newCamera for 3D mode
      if (!is2D) {
        double lat = -JsRadianToDegree(camera.ea.y);
        double lon = JsRadianToDegree(camera.ea.z);
        double heading = lockNorth ? 0.0 : JsRadianToDegree(camera.ea.x);
        double curLat, curLon, curAlt;
        m_newCamera.GetLatLonAlt(curLat, curLon, curAlt);
        m_newCamera.SetLatLonAlt(lat, lon, curAlt);
        m_newCamera.SetHeading(heading);
      }
    };
    
    // Pan Up (move globe down, view moves north) - Google Earth: up arrow
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - tiltBtnSize / 2.0f, tiltStartY));
    ImGui::Button("^##pan_up", ImVec2(tiltBtnSize, tiltBtnSize));
    if (ImGui::IsItemActive()) {
      doPan(0.0, -1.0);  // Pan north
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan North (hold)");
    
    // Center row: Left - Home - Right
    float centerY = tiltStartY + tiltBtnSize + 4.0f;
    
    // Pan Left (move globe right, view moves west) - Google Earth: left arrow
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - tiltBtnSize * 1.5f - 4.0f, centerY));
    ImGui::Button("<##pan_left", ImVec2(tiltBtnSize, tiltBtnSize));
    if (ImGui::IsItemActive()) {
      doPan(1.0, 0.0);  // Pan west
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan West (hold)");
    
    // Home/Reset button (center) - Google Earth: double-click on center or 'r' key
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - tiltBtnSize / 2.0f, centerY));
    if (ImGui::Button("o##home", ImVec2(tiltBtnSize, tiltBtnSize))) {
      // Reset to default view - sync to m_newCamera for 3D mode
      camera.tiltDeg = GLOBE_MIN_TILTANGLE;
      camera.dist = GLOBE_START_DIST_YATAY;
      camera.ea.x = 0.0; // North up
      JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
      UpdateArcballMatrices();
      
      if (!is2D) {
        double lat = -JsRadianToDegree(camera.ea.y);
        double lon = JsRadianToDegree(camera.ea.z);
        double altMeters = (GLOBE_START_DIST_YATAY - GLOBE_RADIUS) / GLOBE_RADIUS_K;
        m_newCamera.SetLatLonAlt(lat, lon, altMeters);
        m_newCamera.SetHeading(0.0);
        m_newCamera.SetTilt(GLOBE_MIN_TILTANGLE);
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset View");
    
    // Pan Right (move globe left, view moves east) - Google Earth: right arrow
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f + tiltBtnSize / 2.0f + 4.0f, centerY));
    ImGui::Button(">##pan_right", ImVec2(tiltBtnSize, tiltBtnSize));
    if (ImGui::IsItemActive()) {
      doPan(-1.0, 0.0);  // Pan east
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan East (hold)");
    
    // Pan Down (move globe up, view moves south) - Google Earth: down arrow
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - tiltBtnSize / 2.0f, centerY + tiltBtnSize + 4.0f));
    ImGui::Button("v##pan_down", ImVec2(tiltBtnSize, tiltBtnSize));
    if (ImGui::IsItemActive()) {
      doPan(0.0, 1.0);  // Pan south
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pan South (hold)");
    
    // ---- TILT CONTROLS (separate row) - Google Earth "Look" joystick ----
    float tiltRowY = centerY + tiltBtnSize * 2 + 12.0f;
    float tiltSmallBtn = 28.0f;
    
    // Tilt label
    ImVec2 tiltLabelPos(windowPos.x + navWidth / 2.0f - 15.0f, windowPos.y + tiltRowY - 2.0f);
    drawList->AddText(tiltLabelPos, IM_COL32(180, 180, 180, 255), "Tilt");
    
    // Tilt Up (look toward horizon) - Google Earth: Shift+Up or middle-drag up
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f - tiltSmallBtn - 8.0f, tiltRowY + 12.0f));
    ImGui::Button("+##tilt_up", ImVec2(tiltSmallBtn, tiltSmallBtn));
    if (ImGui::IsItemActive()) {
      // Frame-rate independent tilt
      double deltaTilt = tiltSpeed * dt * navigationSpeed;
      camera.tiltDeg += deltaTilt;
      if (camera.tiltDeg > GLOBE_MAX_TILTANGLE) camera.tiltDeg = GLOBE_MAX_TILTANGLE;
      if (!is2D) {
        m_newCamera.SetTilt(camera.tiltDeg);
      }
      UpdateArcballMatrices();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tilt Up - Look at Horizon (hold)");
    
    // Tilt Down (look straight down at globe) - Google Earth: Shift+Down or middle-drag down
    ImGui::SetCursorPos(ImVec2(navWidth / 2.0f + 8.0f, tiltRowY + 12.0f));
    ImGui::Button("-##tilt_down", ImVec2(tiltSmallBtn, tiltSmallBtn));
    if (ImGui::IsItemActive()) {
      // Frame-rate independent tilt
      double deltaTilt = tiltSpeed * dt * navigationSpeed;
      camera.tiltDeg -= deltaTilt;
      if (camera.tiltDeg < GLOBE_MIN_TILTANGLE) camera.tiltDeg = GLOBE_MIN_TILTANGLE;
      if (!is2D) {
        m_newCamera.SetTilt(camera.tiltDeg);
      }
      UpdateArcballMatrices();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tilt Down - Look Straight Down (hold)");
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    
    // Clamp camera values
    if (camera.dist < minDist) camera.dist = minDist;
    if (camera.dist > maxDist) camera.dist = maxDist;
    if (camera.tiltDeg < 0.0f) camera.tiltDeg = 0.0f;
    if (camera.tiltDeg > GLOBE_MAX_TILTANGLE) camera.tiltDeg = GLOBE_MAX_TILTANGLE;
    
    ImGui::End();

    // 2D/3D Mode Toggle Button (top-right, first)
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 60.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("ModeToggle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    if (ImGui::Button(is2D ? "3D" : "2D", ImVec2(40.0f, 32.0f))) {
      is2D = !is2D;
      if (is2D) {
        // Switch to 2D: force tilt to 0, lock north
        camera.tiltDeg = 0.0;
        camera.ea.x = 0.0;
        lockNorth = true;  // Enforce north lock in 2D mode to prevent rotations
        JsEulToHMatrix(camera.ea, camera.arcball.abQuat);
        
        // Initialize flat2D state from current camera position
        // This prevents jumps when user starts panning/zooming in 2D mode
        double lonDeg = JsRadianToDegree(camera.ea.z);
        double latDeg = JsRadianToDegree(camera.ea.y);
        flat2DCenterX = Mercator::LonToMerc(lonDeg);
        flat2DCenterY = Mercator::LatToMerc(latDeg);
        
        // Calculate zoom from camera distance
        // JS: zoom = 22 - log2(dist * 256 / GLOBE_RADIUS)
        flat2DZoom = 22.0 - std::log2(camera.dist * 256.0 / GLOBE_RADIUS);
        flat2DZoom = std::clamp(flat2DZoom, static_cast<double>(GLOBE_MIN_LOD), 
                                static_cast<double>(GLOBE_MAX_LOD));
        
        // Calculate view size from zoom
        double size = Mercator::WORLD_SIZE / std::pow(2.0, flat2DZoom);
        flat2DViewWidth = size * config.windowWidth / config.windowHeight;
        flat2DViewHeight = size;
        
        // Reset animation state
        flat2DAnimActive = false;
        flat2DDragging = false;
      }
      UpdateArcballMatrices();
    }
    ImGui::End();

    // Network Debug Toggle Button (top-right, second)
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 120.0f, 50.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("NetToggle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    if (ImGui::Button(showNetworkDebug ? "Hide Network" : "Show Network")) {
      showNetworkDebug = !showNetworkDebug;
    }
    ImGui::End();

    // Network Debug Panel (bottom)
    if (showNetworkDebug) {
      ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - 200.0f), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 200.0f), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.9f);
      ImGui::Begin("Network Debug", &showNetworkDebug, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
      
      // Stats
      size_t pendingCount = 0;
      size_t successCount = 0;
      size_t failedCount = 0;
      {
        std::lock_guard<std::mutex> lock(networkLogMutex);
        for (const auto& entry : networkLog) {
          if (entry.status == NetRequestStatus::Pending) pendingCount++;
          else if (entry.status == NetRequestStatus::Success) successCount++;
          else failedCount++;
        }
      }
      ImGui::Text("Pending: %zu | Success: %zu | Failed: %zu | Queue: %zu", 
                  pendingCount, successCount, failedCount, downloadQueue.size());
      ImGui::Separator();

      // Request list
      ImGui::BeginChild("RequestList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
      {
        std::lock_guard<std::mutex> lock(networkLogMutex);
        double now = glfwGetTime();
        for (auto it = networkLog.rbegin(); it != networkLog.rend(); ++it) {
          const auto& entry = *it;
          const char* statusStr = "?";
          ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
          if (entry.status == NetRequestStatus::Pending) {
            statusStr = "...";
            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
          } else if (entry.status == NetRequestStatus::Success) {
            statusStr = "OK";
            color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green
          } else {
            statusStr = "FAIL";
            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
          }
          double elapsed = (entry.status == NetRequestStatus::Pending) 
                           ? (now - entry.startTime) 
                           : (entry.endTime - entry.startTime);
          ImGui::TextColored(color, "[%s]", statusStr);
          ImGui::SameLine();
          ImGui::Text("[%s] %.2fs", entry.type.c_str(), elapsed);
          if (entry.bytes > 0) {
            ImGui::SameLine();
            if (entry.bytes > 1024) {
              ImGui::Text("%.1fKB", entry.bytes / 1024.0);
            } else {
              ImGui::Text("%zuB", entry.bytes);
            }
          }
          ImGui::SameLine();
          // Truncate URL for display
          std::string displayUrl = entry.url;
          if (displayUrl.size() > 80) {
            displayUrl = displayUrl.substr(0, 77) + "...";
          }
          ImGui::TextWrapped("%s", displayUrl.c_str());
        }
      }
      ImGui::EndChild();
      ImGui::End();
    }

    // Footer with mouse coordinates and elevation
    float footerHeight = 28.0f;
    float footerY = showNetworkDebug ? (io.DisplaySize.y - 200.0f - footerHeight) : (io.DisplaySize.y - footerHeight);
    ImGui::SetNextWindowPos(ImVec2(0.0f, footerY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, footerHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Footer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    
    // Get mouse geo coordinates
    double mouseLat = 0.0, mouseLon = 0.0;
    bool mouseOnGlobe = false;
    if (cursorX >= 0 && cursorY >= 0 && cursorX < config.windowWidth && cursorY < config.windowHeight) {
      // Need to access GlobeEngine's ScreenToGeo - use stored values
      mouseOnGlobe = lastMouseOnGlobe;
      mouseLat = lastMouseLat;
      mouseLon = lastMouseLon;
    }
    
    if (mouseOnGlobe) {
      // Z requires mesh data (not yet implemented)
      ImGui::Text("Lon: %.6f  Lat: %.6f  |  Screen: (%.0f, %.0f)", 
                  mouseLon, mouseLat, cursorX, cursorY);
    } else {
      ImGui::Text("Mouse: outside globe  |  Screen: (%.0f, %.0f)", cursorX, cursorY);
    }
    ImGui::End();
  }

  void StartWorker() {
    workerRunning = true;
    workers.reserve(kNumDownloadWorkers);
    for (int i = 0; i < kNumDownloadWorkers; ++i) {
      workers.emplace_back([this]() { WorkerLoop(); });
    }
    StartDemWorker();
  }

  void StopWorker() {
    if (!workerRunning) return;
    workerRunning = false;
    downloadCv.notify_all();
    for (auto& w : workers) {
      if (w.joinable()) {
        w.join();
      }
    }
    workers.clear();
    StopDemWorker();
  }

  void StartDemWorker() {
    demWorkerRunning = true;
    demWorker = std::thread([this]() { DemWorkerLoop(); });
  }

  void StopDemWorker() {
    if (!demWorkerRunning) return;
    demWorkerRunning = false;
    demCv.notify_all();
    if (demWorker.joinable()) {
      demWorker.join();
    }
  }

  void WorkerLoop() {
    while (workerRunning) {
      DownloadJob job;
      {
        std::unique_lock<std::mutex> lock(downloadMutex);
        downloadCv.wait(lock, [this]() { return !workerRunning || !downloadQueue.empty(); });
        if (!workerRunning) {
          break;
        }
        job = downloadQueue.top();
        downloadQueue.pop();
      } // downloadMutex released here

      // Skip stale or cancelled jobs (bandwidth optimization)
      double currentTime = glfwGetTime();
      bool isCancelled = false;
      {
        std::lock_guard<std::mutex> lock(cancelMutex);
        SchedulerKey sk{TileKey{job.z, job.x, job.y}, job.layerId, job.isVector};
        if (cancelledKeys.count(sk)) {
          cancelledKeys.erase(sk);
          isCancelled = true;
        }
      }
      if (job.cancelled || isCancelled || (currentTime - job.queueTime > MAX_PENDING_AGE_SECONDS)) {
        // Cleanup scheduler state if needed
        if (job.callback) {
            job.callback({}, false);
        }

        // Remove from pending set without processing (Legacy)
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            std::string key = MakeTileKey(job.z, job.x, job.y);
            if (job.isVector) {
              pendingVector.erase(key);
            } else if (job.isSupportRequest && !job.layerId.empty()) {
              auto it = pendingLayerSupportDownloads.find(job.layerId);
              if (it != pendingLayerSupportDownloads.end()) {
                it->second.erase(key);
              }
            } else if (!job.layerId.empty()) {
              auto it = pendingLayerDownloads.find(job.layerId);
              if (it != pendingLayerDownloads.end()) {
                it->second.erase(key);
              }
              // Fix: Clear layer-specific pending set
              auto lit = layerTiles.find(job.layerId);
              if (lit != layerTiles.end()) {
                  lit->second.pendingDownloads.erase(key);
              }
            } else {
              pendingRaster.erase(key);
            }
        }
        continue;
      }

      DownloadResult result;
      result.z = job.z;
      result.x = job.x;
      result.y = job.y;
      result.layerId = job.layerId;  // Pass layer ID for multi-layer support
      result.isVector = job.isVector;
      result.isSupport = job.isSupportRequest;
      result.supportMode = job.supportMode;
      
      // Build URL for logging
      std::string url = BuildTileUrl(job.urlTemplate, job.z, job.x, job.y);
      std::string reqType = job.isVector ? "vector" : (job.layerId.empty() ? "tile" : "layer");
      if (job.isSupportRequest) {
        reqType += "-support";
      }
      LogNetworkRequest(url, reqType);
      
      // Try primary URL first
      result.ok = LoadTileData(config, job.urlTemplate, job.z, job.x, job.y, result.data);
      
      // Update network log
      UpdateNetworkRequest(url, result.ok ? NetRequestStatus::Success : NetRequestStatus::Failed, result.data.size());
      
      // If failed and supportUrl is available, try support URL
      if (!job.isSupportRequest && !result.ok && !job.supportUrl.empty() && job.retryCount == 0) {
        std::string supportUrl = BuildTileUrl(job.supportUrl, job.z, job.x, job.y);
        LogNetworkRequest(supportUrl, reqType + "-retry");
        result.ok = LoadTileData(config, job.supportUrl, job.z, job.x, job.y, result.data);
        UpdateNetworkRequest(supportUrl, result.ok ? NetRequestStatus::Success : NetRequestStatus::Failed, result.data.size());
        result.usedSupportUrl = result.ok;
      }

      // OPTIMIZATION: Decode image in worker thread (offload from main thread)
      // P0 Fix: Skip worker decode if callback path is used (scheduler expects raw data)
      if (result.ok && !result.isVector && job.layerId.empty() && !job.isSupportRequest && !job.callback) {
        result.decodeSuccess = DecodeImageRGBA(result.data, result.decodedPixels, 
                                                result.decodedWidth, result.decodedHeight);
        if (result.decodeSuccess) {
          // Clear raw data to save memory - we have decoded pixels now
          result.data.clear();
          result.data.shrink_to_fit();
        }
      }

      if (job.callback) {
        // Scheduler path: pass raw data (not decoded)
        job.callback(std::move(result.data), result.ok);
      } else {
        std::lock_guard<std::mutex> lock(downloadMutex);
        readyQueue.push(std::move(result));
      }
    }
  }

  void DemWorkerLoop() {
    while (demWorkerRunning) {
      DemJob job;
      {
        std::unique_lock<std::mutex> lock(demMutex);
        demCv.wait(lock, [this]() { return !demWorkerRunning || !demQueue.empty(); });
        if (!demWorkerRunning) {
          break;
        }
        job = std::move(demQueue.front());
        demQueue.pop();
      }

      // LogNetworkRequest(job.url, "dem");
      std::vector<unsigned char> payload;
      
      // Phase 0: Add Referer/Origin headers for DEM requests (scoped to DemWorkerLoop)
      std::vector<std::string> headers;
      std::string origin = ExtractOrigin(job.url);
      if (!origin.empty()) {
          headers.push_back("Origin: " + origin);
          headers.push_back("Referer: " + origin + "/");
      }
      
      bool downloadOk = DownloadUrl(job.url, payload, MESH_DOWNLOAD_TIMEOUT_SECONDS, headers);
      if (!downloadOk) {
        // Log failures always for visibility
        fprintf(stderr, "DEM Download FAILED: %s\n", job.url.c_str());
        
        UpdateNetworkRequest(job.url, NetRequestStatus::Failed, 0);
        std::lock_guard<std::mutex> lock(demMutex);
        if (config.meshRetryAtTimeout && job.retryCount < 1) {
          DemJob retry = job;
          retry.retryCount++;
          retry.url = NextMeshUrl();
          retry.url = BuildDemBatchUrl(retry.url, config.meshType, retry.meshN, retry.cells, config.demDebug);
          demQueue.push(std::move(retry));
          demCv.notify_one();
          continue;
        }
        pendingDemBatches.erase(job.batchKey);
        if (config.meshContinueDivision && job.batchGrid > 1 && job.cells.size() > 1) {
          for (const auto& cell : job.cells) {
            std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                               std::to_string(cell.level) + ":1";
            if (pendingDemBatches.find(ckey) != pendingDemBatches.end()) {
              continue;
            }
            DemJob sub;
            sub.meshN = job.meshN;
            sub.batchGrid = 1;
            sub.retryCount = 0;
            sub.batchKey = ckey;
            sub.cells.push_back(cell);
            sub.url = BuildDemBatchUrl(NextMeshUrl(), config.meshType, sub.meshN, sub.cells, config.demDebug);
            pendingDemBatches.insert(ckey);
            demQueue.push(std::move(sub));
          }
          demCv.notify_all();
        }
        continue;
      } else {
         if (config.demDebug) {
             fprintf(stderr, "DEM Download OK: %zu bytes from %s\n", payload.size(), job.url.c_str());
         }
      }
      std::string body(payload.begin(), payload.end());
      std::vector<double> values;
      if (!ParseDemGrid(body, job.meshN, values, config.demDebug)) {
        UpdateNetworkRequest(job.url, NetRequestStatus::Failed, payload.size());
        std::lock_guard<std::mutex> lock(demMutex);
        if (config.meshRetryAtTimeout && job.retryCount < 1) {
          DemJob retry = job;
          retry.retryCount++;
          retry.url = NextMeshUrl();
          retry.url = BuildDemBatchUrl(retry.url, config.meshType, retry.meshN, retry.cells, config.demDebug);
          demQueue.push(std::move(retry));
          demCv.notify_one();
          continue;
        }
        pendingDemBatches.erase(job.batchKey);
        if (config.meshContinueDivision && job.batchGrid > 1 && job.cells.size() > 1) {
          for (const auto& cell : job.cells) {
            std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                               std::to_string(cell.level) + ":1";
            if (pendingDemBatches.find(ckey) != pendingDemBatches.end()) {
              continue;
            }
            DemJob sub;
            sub.meshN = job.meshN;
            sub.batchGrid = 1;
            sub.retryCount = 0;
            sub.batchKey = ckey;
            sub.cells.push_back(cell);
            sub.url = BuildDemBatchUrl(NextMeshUrl(), config.meshType, sub.meshN, sub.cells, config.demDebug);
            pendingDemBatches.insert(ckey);
            demQueue.push(std::move(sub));
          }
          demCv.notify_all();
        }
        continue;
      }
      const size_t perTile = static_cast<size_t>(job.meshN * job.meshN);
      if (values.size() < perTile * job.cells.size()) {
        UpdateNetworkRequest(job.url, NetRequestStatus::Failed, payload.size());
        std::lock_guard<std::mutex> lock(demMutex);
        if (config.meshRetryAtTimeout && job.retryCount < 1) {
          DemJob retry = job;
          retry.retryCount++;
          retry.url = NextMeshUrl();
          retry.url = BuildDemBatchUrl(retry.url, config.meshType, retry.meshN, retry.cells, config.demDebug);
          demQueue.push(std::move(retry));
          demCv.notify_one();
          continue;
        }
        pendingDemBatches.erase(job.batchKey);
        if (config.meshContinueDivision && job.batchGrid > 1 && job.cells.size() > 1) {
          for (const auto& cell : job.cells) {
            std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                               std::to_string(cell.level) + ":1";
            if (pendingDemBatches.find(ckey) != pendingDemBatches.end()) {
              continue;
            }
            DemJob sub;
            sub.meshN = job.meshN;
            sub.batchGrid = 1;
            sub.retryCount = 0;
            sub.batchKey = ckey;
            sub.cells.push_back(cell);
            sub.url = BuildDemBatchUrl(NextMeshUrl(), config.meshType, sub.meshN, sub.cells, config.demDebug);
            pendingDemBatches.insert(ckey);
            demQueue.push(std::move(sub));
          }
          demCv.notify_all();
        }
        continue;
      }
      UpdateNetworkRequest(job.url, NetRequestStatus::Success, payload.size());

      std::vector<std::pair<std::string, DemTile>> newTiles;
      newTiles.reserve(job.cells.size());
      for (size_t i = 0; i < job.cells.size(); ++i) {
        const auto& cell = job.cells[i];
          std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                             std::to_string(cell.level);
        DemTile tile;
        tile.llx = cell.llx;
        tile.lly = cell.lly;
        tile.urx = cell.urx;
        tile.ury = cell.ury;
        const size_t start = i * perTile;
        tile.grid.assign(values.begin() + start, values.begin() + start + perTile);
        newTiles.emplace_back(std::move(ckey), std::move(tile));
      }

      {
        std::lock_guard<std::mutex> lock(demMutex);
        int meshN = job.meshN;
        for (auto& entry : newTiles) {
          const std::string& ckey = entry.first;
          DemTile& tile = entry.second;
          
          // JS parity: Extract edge values for neighbor stitching
          // Pass row ordering config to handle south-to-north vs north-to-south DEM sources
          tile.ExtractEdges(meshN, config.demRowsNorthToSouth);
          
          auto it = dem.tiles.find(ckey);
          if (it != dem.tiles.end()) {
            dem.lru.erase(it->second.lruIt);
            it->second = std::move(tile);
            dem.lru.push_front(ckey);
            it->second.lruIt = dem.lru.begin();
          } else {
            dem.lru.push_front(ckey);
            tile.lruIt = dem.lru.begin();
            dem.tiles.emplace(ckey, std::move(tile));
          }
        }
        
        // JS parity: Share edges with neighbors for seam stitching
        // Also re-stitch existing neighbors that were waiting for this tile
        for (auto& entry : newTiles) {
          const std::string& ckey = entry.first;
          StitchDemNeighbors(ckey, meshN, config.meshType);
          
          // P0.2 Fix: Re-stitch existing neighbors now that this tile is available
          int tileX = 0, tileY = 0, level = 0;
          std::sscanf(ckey.c_str(), "%d:%d:%d", &tileX, &tileY, &level);
          for (int dir = 0; dir < 4; ++dir) {
            std::string neighborKey = GetNeighborDemKey(tileX, tileY, level, dir, config.meshType);
            if (!neighborKey.empty() && dem.tiles.find(neighborKey) != dem.tiles.end()) {
              StitchDemNeighbors(neighborKey, meshN, config.meshType);
              if (config.demDebug) {
                fprintf(stderr, "[Stitch] Re-stitching neighbor %s after %s arrived\n", 
                        neighborKey.c_str(), ckey.c_str());
              }
            }
          }
        }
        
        // Signal main thread to rebuild meshes (thread-safe)
        // demDataUpdated.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(updatedDemMutex);
            for (const auto& entry : newTiles) {
                updatedDemKeys.push_back(entry.first);
            }
        }

        pendingDemBatches.erase(job.batchKey);

        size_t cacheSize = config.meshCacheSize > 0 ? config.meshCacheSize : config.demCacheSize;
        while (dem.tiles.size() > std::max<size_t>(1, cacheSize)) {
          const std::string& evictKey = dem.lru.back();
          dem.tiles.erase(evictKey);
          dem.lru.pop_back();
        }
      }
    }
  }

  void ProcessReadyDownloads() {
    std::queue<DownloadResult> local;
    {
      std::lock_guard<std::mutex> lock(downloadMutex);
      std::swap(local, readyQueue);
    }

    // Cache layer configs once per frame to avoid repeated mutex locks in hot path
    std::unordered_map<std::string, RasterLayerConfig> layerConfigCache;
    {
      std::lock_guard<std::mutex> lock(configMutex);
      for (const auto& layer : config.rasterLayers) {
        layerConfigCache[layer.id] = layer;
      }
    }

    std::queue<DownloadResult> deferred;
    size_t processed = 0;

    while (!local.empty()) {
      DownloadResult res = std::move(local.front());
      local.pop();

      if (res.ok && processed >= kMaxReadyDownloadsPerFrame) {
        deferred.push(std::move(res));
        continue;
      }
      std::string key = MakeTileKey(res.z, res.x, res.y);

      if (res.isVector) {
        {
             std::lock_guard<std::mutex> lock(pendingMutex);
             pendingVector.erase(key);
        }
        auto it = vectorTiles.find(key);
        if (it == vectorTiles.end()) {
          continue;
        }
        if (!res.ok) {
          continue;
        }
        DestroyVectorTile(it->second, &deferredQueue);
        VectorTile vt;
        vt.x = res.x;
        vt.y = res.y;
        vt.z = res.z;
        if (BuildVectorTileFromData(config, res.z, res.x, res.y, res.data, vt)) {
          it->second = std::move(vt);
        }
      } else if (!res.layerId.empty()) {
        // Multi-layer tile download result
        auto layerIt = layerTiles.find(res.layerId);
        if (layerIt != layerTiles.end()) {
          // Use cached config instead of FindRasterLayerConfig to avoid mutex in hot path
          auto cacheIt = layerConfigCache.find(res.layerId);
          std::optional<RasterLayerConfig> layerCfgOpt = (cacheIt != layerConfigCache.end()) 
              ? std::optional<RasterLayerConfig>(cacheIt->second) : std::nullopt;
          auto tileIt = layerIt->second.tiles.find(key);

          auto queueSupport = [&](Tile& tile, SupportMode mode,
                                  std::vector<unsigned char> mainPixels,
                                  int mainWidth,
                                  int mainHeight) -> bool {
            if (!layerCfgOpt || layerCfgOpt->supportUrl.empty()) {
              return false;
            }
            if (tile.supportPending) {
              return true;
            }
            size_t queueSize = 0;
            {
              std::lock_guard<std::mutex> lock(downloadMutex);
              queueSize = downloadQueue.size();
            }
            if (queueSize >= kMaxDownloadQueueSize) {
              return false;
            }
            tile.supportPending = true;
            tile.supportMode = mode;
            tile.supportMainPixels = std::move(mainPixels);
            tile.supportMainWidth = mainWidth;
            tile.supportMainHeight = mainHeight;
            DownloadJob job;
            job.urlTemplate = layerCfgOpt->supportUrl;
            job.layerId = res.layerId;
            job.z = res.z;
            job.x = res.x;
            job.y = res.y;
            job.isVector = false;
            job.priority = PRIORITY_VISIBLE_LEAF;
            job.isSupportRequest = true;
            job.supportMode = mode;
            // Pass pendingMutex to EnqueueDownload - it handles lock ordering internally
            EnqueueDownload(pendingLayerSupportDownloads[res.layerId], downloadQueue, downloadMutex, downloadCv, job, &pendingMutex);
            return true;
          };

          if (res.isSupport) {
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                auto pendIt = pendingLayerSupportDownloads.find(res.layerId);
                if (pendIt != pendingLayerSupportDownloads.end()) {
                  pendIt->second.erase(key);
                }
            }
            if (tileIt == layerIt->second.tiles.end()) {
              continue;
            }
            Tile& tile = tileIt->second;
            tile.supportPending = false;
            SupportMode mode = res.supportMode;
            if (res.ok) {
              GLuint tex = 0;
              if (mode == SupportMode::TRANSPARENT_PIXEL && !tile.supportMainPixels.empty()) {
                std::vector<unsigned char> supportPixels;
                int sw = 0;
                int sh = 0;
                if (DecodeImageRGBA(res.data, supportPixels, sw, sh) &&
                    sw == tile.supportMainWidth && sh == tile.supportMainHeight) {
                  const size_t count = static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4;
                  std::vector<unsigned char> merged;
                  merged.resize(count);
                  constexpr unsigned char kAlphaVisibleThreshold = 10;
                  for (size_t i = 0; i + 3 < count; i += 4) {
                    if (tile.supportMainPixels[i + 3] <= kAlphaVisibleThreshold) {
                      merged[i] = supportPixels[i];
                      merged[i + 1] = supportPixels[i + 1];
                      merged[i + 2] = supportPixels[i + 2];
                      merged[i + 3] = supportPixels[i + 3];
                    } else {
                      merged[i] = tile.supportMainPixels[i];
                      merged[i + 1] = tile.supportMainPixels[i + 1];
                      merged[i + 2] = tile.supportMainPixels[i + 2];
                      merged[i + 3] = tile.supportMainPixels[i + 3];
                    }
                  }
                  tex = CreateTextureFromRGBA(merged.data(), sw, sh);
                } else {
                  tex = CreateTextureFromRGBA(tile.supportMainPixels.data(),
                                              tile.supportMainWidth, tile.supportMainHeight);
                }
              } else {
                tex = CreateTextureFromMemory(res.data);
              }
              tile.supportMainPixels.clear();
              tile.supportMainPixels.shrink_to_fit();
              tile.supportMainWidth = 0;
              tile.supportMainHeight = 0;
              tile.supportMode = SupportMode::NONE;
              if (tex) {
                if (tile.ownsTexture) {
                  glDeleteTextures(1, &tile.texture);
                }
                tile.texture = tex;
                tile.ownsTexture = true;
                tile.fade = 0.0f;
              }
            } else {
              if (mode == SupportMode::TRANSPARENT_PIXEL && !tile.supportMainPixels.empty()) {
                GLuint tex = CreateTextureFromRGBA(tile.supportMainPixels.data(),
                                                  tile.supportMainWidth, tile.supportMainHeight);
                tile.supportMainPixels.clear();
                tile.supportMainPixels.shrink_to_fit();
                tile.supportMainWidth = 0;
                tile.supportMainHeight = 0;
                tile.supportMode = SupportMode::NONE;
                if (tex) {
                  if (tile.ownsTexture) {
                    glDeleteTextures(1, &tile.texture);
                  }
                  tile.texture = tex;
                  tile.ownsTexture = true;
                  tile.fade = 0.0f;
                }
              } else {
                tile.supportMainPixels.clear();
                tile.supportMainPixels.shrink_to_fit();
                tile.supportMainWidth = 0;
                tile.supportMainHeight = 0;
                tile.supportMode = SupportMode::NONE;
              }
            }
          } else {
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                layerIt->second.pendingDownloads.erase(key);
                auto pendIt = pendingLayerDownloads.find(res.layerId);
                if (pendIt != pendingLayerDownloads.end()) {
                  pendIt->second.erase(key);
                }
            }
            if (tileIt == layerIt->second.tiles.end()) {
              continue;
            }
            Tile& tile = tileIt->second;
            if (!res.ok) {
              if (layerCfgOpt && layerCfgOpt->supportEmptyContent &&
                  queueSupport(tile, SupportMode::EMPTY_CONTENT, {}, 0, 0)) {
                continue;
              }
              continue;
            }

            const bool supportTransparent = layerCfgOpt && layerCfgOpt->supportTransparentPixel;
            const bool supportOutOfBbox = layerCfgOpt && layerCfgOpt->supportOutOfBBOX;
            if (supportTransparent || supportOutOfBbox) {
              std::vector<unsigned char> pixels;
              int w = 0;
              int h = 0;
              if (DecodeImageRGBA(res.data, pixels, w, h)) {
                bool anyTransparent = false;
                bool allTransparent = false;
                AnalyzeAlpha(pixels, anyTransparent, allTransparent);
                if (supportTransparent && anyTransparent) {
                  if (queueSupport(tile, SupportMode::TRANSPARENT_PIXEL, std::move(pixels), w, h)) {
                    continue;
                  }
                } else if (supportOutOfBbox && allTransparent) {
                  if (queueSupport(tile, SupportMode::OUT_OF_BBOX, std::move(pixels), w, h)) {
                    continue;
                  }
                }
                GLuint tex = CreateTextureFromRGBA(pixels.data(), w, h);
                if (tex) {
                  if (tile.ownsTexture) {
                    glDeleteTextures(1, &tile.texture);
                  }
                  tile.texture = tex;
                  tile.ownsTexture = true;
                  tile.fade = 0.0f;
                }
                continue;
              }
            }

            GLuint tex = CreateTextureFromMemory(res.data);
            if (tex) {
              if (tile.ownsTexture) {
                glDeleteTextures(1, &tile.texture);
              }
              tile.texture = tex;
              tile.ownsTexture = true;
              tile.fade = 0.0f;
            }
          }
        }
      } else {
        // Base layer tile download result
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingRaster.erase(key);
        }
        auto it = tiles.find(key);
        if (it == tiles.end()) {
          continue;
        }
        int baseMinZoom = config.baseRasterMinZoom >= 0 ? config.baseRasterMinZoom : config.minZoom;
        int baseMaxZoom = config.baseRasterMaxZoom >= 0 ? config.baseRasterMaxZoom : config.maxZoom;
        if (baseMinZoom > baseMaxZoom) {
          std::swap(baseMinZoom, baseMaxZoom);
        }
        if (res.z < baseMinZoom || res.z > baseMaxZoom) {
          if (it->second.ownsTexture && it->second.texture != 0) {
            glDeleteTextures(1, &it->second.texture);
          }
          it->second.texture = loadingTexture;
          it->second.ownsTexture = false;
          it->second.textureState = TextureState::LOAD_OK_NO_DATA;
          continue;
        }
        if (!res.ok) {
          // JS parity: Set state to NO_INTERNET on failure
          it->second.textureState = TextureState::LOAD_NO_INTERNET;
          it->second.retryCount++;
          it->second.lastRetryTime = glfwGetTime();  // P3: Track for backoff
          continue;
        }
        
        // OPTIMIZATION: Use pre-decoded data from worker thread if available
        if (res.decodeSuccess && !res.decodedPixels.empty()) {
          // Worker thread already decoded - just upload to GPU
          bool anyTransparent = false, allTransparent = false;
          AnalyzeAlpha(res.decodedPixels, anyTransparent, allTransparent);
          
          if (allTransparent) {
            it->second.textureState = TextureState::LOAD_OK_NO_DATA;
            it->second.ownsTexture = false;
          } else {
            GLuint tex = CreateTextureFromRGBA(res.decodedPixels.data(), 
                                               res.decodedWidth, res.decodedHeight);
            if (tex) {
              if (it->second.ownsTexture) {
                glDeleteTextures(1, &it->second.texture);
              }
              it->second.texture = tex;
              it->second.ownsTexture = true;
              it->second.textureState = TextureState::LOAD_OK;
              it->second.fade = 0.0f;
              it->second.unpopStartTime = glfwGetTime();
              it->second.unpopFactor = 0.0f;
              it->second.unpopComplete = false;
            } else {
              it->second.textureState = TextureState::LOAD_NO_INTERNET;
              it->second.retryCount++;
              it->second.lastRetryTime = glfwGetTime();
            }
          }
        } else {
          // Fallback: decode on main thread (for non-base tiles or failed worker decode)
          int imgWidth = 0, imgHeight = 0;
          int checkResult = CheckTileImage(res.data, imgWidth, imgHeight);
          
          if (checkResult == -1) {
            it->second.textureState = TextureState::LOAD_NO_INTERNET;
            it->second.retryCount++;
            it->second.lastRetryTime = glfwGetTime();
          } else if (checkResult == 1) {
            it->second.textureState = TextureState::LOAD_OK_NO_DATA;
            it->second.ownsTexture = false;
          } else {
            GLuint tex = CreateTextureFromMemory(res.data);
            if (tex) {
              if (it->second.ownsTexture) {
                glDeleteTextures(1, &it->second.texture);
              }
              it->second.texture = tex;
              it->second.ownsTexture = true;
              it->second.textureState = TextureState::LOAD_OK;
              it->second.fade = 0.0f;
              it->second.unpopStartTime = glfwGetTime();
              it->second.unpopFactor = 0.0f;
              it->second.unpopComplete = false;
            } else {
              it->second.textureState = TextureState::LOAD_NO_INTERNET;
              it->second.retryCount++;
              it->second.lastRetryTime = glfwGetTime();
            }
          }
        }
      }

      if (res.ok) {
        ++processed;
      }
    }

    if (!deferred.empty()) {
      std::lock_guard<std::mutex> lock(downloadMutex);
      while (!deferred.empty()) {
        readyQueue.push(std::move(deferred.front()));
        deferred.pop();
      }
    }
  }

  static void ScrollCallback(GLFWwindow* window, double, double yoffset) {
    auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (impl) impl->OnScroll(yoffset);
  }

  static void MouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (impl) impl->OnMouseButton(button, action);
  }

  static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (impl) {
        impl->OnCursorPos(xpos, ypos);
    }
  }

  static void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (impl) impl->OnFramebufferSize(width, height);
  }

  static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (impl) {
      bool shift = (mods & GLFW_MOD_SHIFT) != 0;
      bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
      impl->m_flightController.OnModifiers(shift, ctrl);

      if (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureKeyboard) {
        impl->OnKeyInput(key, action);
      }
    }
  }
};

GlobeEngine::GlobeEngine() : impl_(new Impl()) {
  stbi_set_flip_vertically_on_load(true);
  impl_->owner = this;
}

GlobeEngine::~GlobeEngine() {
  Shutdown();
  delete impl_;
  impl_ = nullptr;
}

std::string GetTerrainVertexShader(const std::string& defines = "") {
  return std::string("#version 330 core\n") + defines + R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform vec2 uUvScale;
uniform vec2 uUvOffset;
uniform vec2 uUvScale2;
uniform vec2 uUvOffset2;
out vec2 vUV;
out vec2 vUV2;
out vec2 vEdgeDist;
void main() {
  vUV = aUV * uUvScale + uUvOffset;
  vUV2 = aUV * uUvScale2 + uUvOffset2;
  vEdgeDist = vec2(min(aUV.x, 1.0 - aUV.x), min(aUV.y, 1.0 - aUV.y));
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";
}

std::string GetTerrainFragmentShader(const std::string& defines = "") {
  return std::string("#version 330 core\n") + defines + R"(
in vec2 vUV;
in vec2 vUV2;
in vec2 vEdgeDist;
out vec4 FragColor;
uniform sampler2D uTex;
uniform sampler2D uTex2;
uniform float uAlpha;
uniform vec4 uColor;
uniform float uBlendFactor;
uniform float uEdgeBlend;
void main() {
  vec4 childColor = texture(uTex, vUV);
  vec4 parentColor = texture(uTex2, vUV2);
  
  float edgeFactor = 0.0;
  if (uEdgeBlend > 0.0) {
    float minEdgeDist = min(vEdgeDist.x, vEdgeDist.y);
    edgeFactor = 1.0 - smoothstep(0.0, uEdgeBlend, minEdgeDist);
  }
  
  float blend = max(uBlendFactor, edgeFactor);
  vec4 texColor = mix(childColor, parentColor, blend);
  
  FragColor = vec4(texColor.rgb * uColor.rgb, texColor.a * uAlpha * uColor.a);
}
)";
}

bool GlobeEngine::Init(const GlobeConfig& config) {
  impl_->config = config;
  impl_->layerManager.SetIconMaps(&impl_->iconMaps);
  
  // stbi_set_flip handled in constructor
  
  impl_->labelManager = std::make_unique<LabelManager>();
  impl_->labelManager->Init();
  impl_->layerManager.SetLabelManager(impl_->labelManager.get());
  // Note: InitPivotGizmo() moved after OpenGL context is ready
  
  // Initialize Scheduler
  impl_->tileFetcher = std::make_unique<GlobeTileFetcher>(
      impl_->downloadQueue, impl_->downloadMutex, impl_->downloadCv,
      impl_->config.tileUrl, nullptr,
      impl_->cancelMutex, impl_->cancelledKeys);
  
  impl_->imageDecoder = std::make_shared<GlobeImageDecoder>(nullptr);
  
  impl_->scheduler = std::make_unique<TileScheduler>(impl_->tileFetcher.get(), impl_->imageDecoder.get());
  
  impl_->tileFetcher->SetScheduler(impl_->scheduler.get());
  impl_->imageDecoder->SetScheduler(impl_->scheduler.get());

  // Always default to fetching from server unless user explicitly enables cache
  impl_->config.useDiskCache = false;
  // JS parity + backward compatibility: normalize meshType numeric values
  // JS: WGS84=1, XYZ_MERCATOR=2. Legacy config may pass 0 for WGS84.
  int meshTypeRaw = static_cast<int>(impl_->config.meshType);
  if (meshTypeRaw == 0) {
    impl_->config.meshType = MeshType::WGS84;
  } else if (meshTypeRaw == 2) {
    impl_->config.meshType = MeshType::XYZ_MERCATOR;
  }
  impl_->meshUrls = impl_->config.meshUrls;
  if (impl_->meshUrls.empty() && !impl_->config.meshUrl.empty()) {
    const std::string& urls = impl_->config.meshUrl;
    size_t start = 0;
    while (start <= urls.size()) {
      size_t comma = urls.find(',', start);
      std::string part = (comma == std::string::npos)
                             ? urls.substr(start)
                             : urls.substr(start, comma - start);
      if (!part.empty()) {
        impl_->meshUrls.push_back(part);
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    if (impl_->meshUrls.empty()) {
      impl_->meshUrls.push_back(impl_->config.meshUrl);
    }
  }
  if (!impl_->meshUrls.empty()) {
    impl_->config.demBaseUrl = impl_->meshUrls.front();
  }
  if (impl_->config.meshCacheSize > 0) {
    impl_->config.demCacheSize = impl_->config.meshCacheSize;
  }
  impl_->vectorEnabled = !config.vectorTileUrl.empty();

  // Connect FlightController to Picking
  impl_->m_flightController.SetPickCallback([this](double x, double y, glm::dvec3& outPoint) -> bool {
      double lat = 0, lon = 0;
      if (ScreenToGeo(static_cast<int>(x), static_cast<int>(y), lat, lon)) {
          double height = 0.0;
          int lod = GetCurrentZoom();
          if (lod < 2) lod = 2; // Default min lod for picking
          
          // Try to get terrain height
          SampleTerrainHeightMeters(lon, lat, lod, height);
          
          double r = GLOBE_RADIUS + height * GLOBE_RADIUS_K;
          glm::vec3 p = LatLonToSphere(glm::radians(lat), glm::radians(lon), r);
          outPoint = glm::dvec3(p);
          return true;
      }
      return false;
  });

  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    std::cerr << "Failed to init GLFW" << std::endl;
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  impl_->window = glfwCreateWindow(impl_->config.windowWidth, impl_->config.windowHeight,
                                   "Native Globe", nullptr, nullptr);
  if (!impl_->window) {
    std::cerr << "Failed to create window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(impl_->window);
  glfwShowWindow(impl_->window);
  glfwFocusWindow(impl_->window);
  glfwSetWindowUserPointer(impl_->window, impl_);
  glfwSetScrollCallback(impl_->window, Impl::ScrollCallback);
  glfwSetMouseButtonCallback(impl_->window, Impl::MouseButtonCallback);
  glfwSetCursorPosCallback(impl_->window, Impl::CursorPosCallback);
  glfwSetFramebufferSizeCallback(impl_->window, Impl::FramebufferSizeCallback);
  glfwSetKeyCallback(impl_->window, Impl::KeyCallback);
  glfwSwapInterval(1);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to init GLAD" << std::endl;
    return false;
  }

  int fbWidth = 0;
  int fbHeight = 0;
  glfwGetFramebufferSize(impl_->window, &fbWidth, &fbHeight);
  if (fbWidth > 0 && fbHeight > 0) {
    impl_->config.windowWidth = fbWidth;
    impl_->config.windowHeight = fbHeight;
    impl_->m_flightController.OnWindowResize(fbWidth, fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
  }

  LogOpenGLInfo();
  if (!CheckOpenGLVersion(3, 3)) {
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Shader with dual texture support and basic lighting
  std::string vs = GetTerrainVertexShader();
  std::string fs = GetTerrainFragmentShader();

  impl_->program = CreateProgram(vs.c_str(), fs.c_str());
  if (!impl_->program) {
    std::cerr << "Failed to create terrain shader program." << std::endl;
    return false;
  }
  impl_->mvpLoc = glGetUniformLocation(impl_->program, "uMVP");
  impl_->texLoc = glGetUniformLocation(impl_->program, "uTex");
  impl_->alphaLoc = glGetUniformLocation(impl_->program, "uAlpha");
  impl_->colorLoc = glGetUniformLocation(impl_->program, "uColor");
  impl_->uvScaleLoc = glGetUniformLocation(impl_->program, "uUvScale");
  impl_->uvOffsetLoc = glGetUniformLocation(impl_->program, "uUvOffset");
  impl_->uvScale2Loc = glGetUniformLocation(impl_->program, "uUvScale2");
  impl_->uvOffset2Loc = glGetUniformLocation(impl_->program, "uUvOffset2");
  impl_->tex2Loc = glGetUniformLocation(impl_->program, "uTex2");
  impl_->blendFactorLoc = glGetUniformLocation(impl_->program, "uBlendFactor");
  impl_->edgeBlendLoc = glGetUniformLocation(impl_->program, "uEdgeBlend");
  // Lighting uniforms removed


  const char* vsVector = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
uniform float uScale;
uniform float uPointSize;
void main() {
  gl_Position = uMVP * vec4(aPos * uScale, 1.0);
  gl_PointSize = uPointSize;
}
)";

  const char* fsVector = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
  FragColor = uColor;
}
)";

  impl_->vectorProgram = CreateProgram(vsVector, fsVector);
  if (!impl_->vectorProgram) {
    std::cerr << "Failed to create vector shader program." << std::endl;
    return false;
  }
  impl_->vectorMvpLoc = glGetUniformLocation(impl_->vectorProgram, "uMVP");
  impl_->vectorColorLoc = glGetUniformLocation(impl_->vectorProgram, "uColor");
  impl_->vectorPointSizeLoc = glGetUniformLocation(impl_->vectorProgram, "uPointSize");
  impl_->vectorScaleLoc = glGetUniformLocation(impl_->vectorProgram, "uScale");

  // Initialize pivot gizmo (requires OpenGL context)
  impl_->InitPivotGizmo();
  
  impl_->loadingTexture = CreateLoadingTexture();

  // HS-style pole mesh initialization
  impl_->northPoleMesh = BuildPoleMesh(true, 64);
  impl_->southPoleMesh = BuildPoleMesh(false, 64);
  
  // Simple pole shader (uses solid color for poles)
  const char* vsPole = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec2 aUV;
    uniform mat4 uMVP;
    void main() {
      gl_Position = uMVP * vec4(aPos, 1.0);
    }
  )";
  const char* fsPole = R"(
    #version 330 core
    uniform vec4 uColor;
    out vec4 FragColor;
    void main() {
      FragColor = uColor;
    }
  )";
  impl_->poleProgram = CreateProgram(vsPole, fsPole);
  if (!impl_->poleProgram) {
    std::cerr << "Failed to create pole shader program." << std::endl;
    return false;
  }
  impl_->poleMvpLoc = glGetUniformLocation(impl_->poleProgram, "uMVP");
  impl_->poleColorLoc = glGetUniformLocation(impl_->poleProgram, "uColor");
  
  // Atmosphere initialization removed


  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    std::cerr << "Failed to init curl" << std::endl;
    return false;
  }

  impl_->InitUi();
  impl_->StartWorker();
  
  // Set default starting view to LOD 2
  ZoomToLOD(2);
  
  impl_->valid = true;
  return true;
}

  void GlobeEngine::Impl::ProcessPendingIconMaps() {
    std::vector<PendingIconMap> localPending;
    {
      std::lock_guard<std::mutex> lock(iconMapMutex);
      localPending = std::move(pendingIconMaps);
      pendingIconMaps.clear();
    }

    for (auto& item : localPending) {
      if (valid) {
        // GL context valid - create texture
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item.w, item.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, item.pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        item.map.textureId = tex;
        item.map.width = item.w;
        item.map.height = item.h;
        item.map.loaded = true;
        
        iconMaps[item.name] = std::move(item.map);
        stbi_image_free(item.pixels);
        
        if (item.callback) {
          item.callback(true);
        }
      } else {
        // GL context invalid - cleanup only, no GL calls
        stbi_image_free(item.pixels);
        if (item.callback) {
          item.callback(false);  // Notify failure
        }
      }
    }
    
    if (!localPending.empty() && valid) {
        layerManager.MarkAllDirty();
    }
  }

  void GlobeEngine::Impl::Render(double dt) {
    // Process pending icon maps even if invalid (cleanup mode)
    // This prevents pixel buffer leaks if valid flips false outside Shutdown
    ProcessPendingIconMaps();
    
    // Early exit if context is invalid - no further GL calls allowed
    if (!valid) return;
    
    // Process Main Thread Tasks (Phase 19)
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        if (!mainThreadTasks.empty()) {
            for (auto& task : mainThreadTasks) {
                task();
            }
            mainThreadTasks.clear();
        }
    }
    
    // P2: Process deferred GL deletions (limit 10 resources per frame to avoid stutter)
    ProcessDeferredDeletions(10);
    
    // Phase 16.5: Sync cursor state before simulation
    {
      std::lock_guard<std::mutex> lock(networkLogMutex);
      cursorX = pendingCursorX;
      cursorY = pendingCursorY;
    }

    // Reset state tracker at start of frame to sync with external changes (e.g. ImGui)
    glState.Reset();
    
    // Enforce core GL state each frame to avoid pass leaks.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Debug: Trace Render heartbeat
    /*
    static double lastDebugTime = 0.0;
    bool debugPrint = (glfwGetTime() - lastDebugTime > 1.0);
    if (debugPrint) {
        lastDebugTime = glfwGetTime();
        std::cout << "[Render] FPS: " << fpsValue << " dt: " << dt << " Valid: " << valid << std::endl;
    }
    */
    
    // Matrix setup
    glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                      static_cast<float>(config.windowWidth) / config.windowHeight,
                                      0.01f, 100.0f * GLOBE_RADIUS);
    glm::mat4 view = GetViewMatrix(); 
    
    // JS parity updates
    glm::dvec3 up = upJs;
    if (glm::dot(up, up) < 1e-12) {
      up = glm::dvec3(0.0, 0.0, 1.0);
    }
    glm::dmat4 modelJs = glm::lookAt(glm::dvec3(0.0), glm::dvec3(-camera.dist, 0.0, 0.0), up);
    modelJs = glm::rotate(modelJs, JsDegreeToRadian(-camera.tiltDeg), glm::dvec3(0.0, 1.0, 0.0));
    modelJs = glm::translate(modelJs, -eyeLocal2);
    modelJs = glm::translate(modelJs, glm::dvec3(-GLOBE_RADIUS, 0.0, 0.0));
    glm::dmat4 modelWorld = JsToWorldMat(modelJs);

    camera.arcball.UpdateMatrices(GLOBE_RADIUS + camera.camZ, eyeJs,
                                         glm::dmat4(proj), modelWorld,
                                         glm::ivec4(0, 0, config.windowWidth, config.windowHeight));

    UpdateOrbitData();
    FindOrbitPoint(proj, view);
    
    glm::mat4 mvp = proj * view;
    
    // Frustum extraction
    glm::mat4 invMvp = glm::inverse(mvp);
    glm::vec4 insideNDC(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 insideWorld = invMvp * insideNDC;
    glm::vec3 insidePoint = glm::vec3(insideWorld) / insideWorld.w;
    auto frustum = ExtractFrustumPlanes(mvp, insidePoint);
    
    // Ensure render state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); // Disable culling for debug
    
    glClearColor(0.01f, 0.02f, 0.05f, 1.0f); // Space color
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (wireframeMode) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    // Render Pipeline - Correct order:
    // 1. Base tiles (terrain only)
    // 2. Poles (on terrain)
    // 3. Vector tiles (if enabled)
    // 4. Raster overlays (zIndex sorted, above base/poles/vectors)
    // 5. Layer manager (features, polygons, etc.)
    // 6. Image overlays (top-most raster)
    // 7. Pivot gizmo
    // 8. Labels (always on top)
    
    RenderTiles(mvp, frustum, dt);  // Base tiles only
    RenderPoles(mvp);
    
    if (vectorEnabled && !config.vectorTileUrl.empty()) {
        RenderVectors(mvp, frustum);
    }
    
    RenderRasterOverlays(mvp, frustum, dt);  // Raster overlays (after poles/vectors)
    
    // Render Layers (Phase 16)
    layerManager.Render(mvp, currentZoom, static_cast<float>(glfwGetTime()), static_cast<float>(flashPeriod));
    
    RenderImageOverlays(mvp); // Phase 19: Image Overlays (top-most)
    
    RenderPivot(mvp);
    
    if (uiInitialized && labelManager) {
        labelManager->Render(mvp, glm::vec3(eyeWorld));
    }
}

void GlobeEngine::Impl::RenderTiles(const glm::mat4& mvp, const std::array<Plane, 6>& frustum, double dt) {
    double centerLat = JsRadianToDegree(camera.camLongLat.y);
    double centerLon = JsRadianToDegree(camera.camLongLat.x);
    int lodMaxZoom = config.useFixedZoom ? config.fixedZoom : config.maxZoom;
    ClampCameraDistance();
    double altitudeMeters = altitudeWorld / static_cast<double>(GLOBE_RADIUS_K);
    if (altitudeMeters < 0.0) altitudeMeters = 0.0;
    double zoomExact = config.useFixedZoom
                           ? static_cast<double>(config.fixedZoom)
                           : FindLodFromAltitudeMeters(altitudeMeters, config.minZoom, lodMaxZoom);
    int zoom = config.useFixedZoom
                   ? config.fixedZoom
                   : static_cast<int>(std::round(zoomExact));
    if (zoom < config.minZoom) zoom = config.minZoom;
    if (zoom > lodMaxZoom) zoom = lodMaxZoom;
    zoomExact = ClampLod(zoomExact, config.minZoom, lodMaxZoom);

    auto [cx, cy] = GeoToTileXY(centerLat, centerLon, zoom);
    const int n = 1 << zoom;
    cx = WrapTileX(cx, n);
    cy = ClampTileY(cy, n);

    currentZoom = zoom;
    currentZoomExact = zoomExact;
    currentCenterX = cx;
    currentCenterY = cy;

    double mouseLat = 0.0, mouseLon = 0.0;
    lastMouseOnGlobe = owner->ScreenToGeo(static_cast<int>(cursorX), static_cast<int>(cursorY), mouseLat, mouseLon);
    if (lastMouseOnGlobe) {
      lastMouseLat = mouseLat;
      lastMouseLon = mouseLon;
    }

    std::unordered_set<std::string> requiredTiles;
    std::vector<std::string> leafTiles;
    cellDivisionCount = 0;
    maxCellCanBeCreated = GetDynamicMaxCellCanBeCreated();

    int baseLayerMaxZoom = lodMaxZoom;
    float tiltFactor = 1.0f;
    if (!is2D) {
      tiltFactor = std::clamp(1.0f - static_cast<float>(camera.tiltDeg / 150.0), 0.0f, 1.0f);
    }
    
    glm::vec3 cameraPos = glm::vec3(eyeWorld);
    float fovRad = glm::radians(GLOBE_FOV);

    // Use modern TileLodSelector (fixed 2026-02-04)
    // Configure LOD selector with current settings
    earth::TileLodSelector::Config lodConfig;
    lodConfig.minZoom = config.minZoom;
    lodConfig.maxZoom = baseLayerMaxZoom;
    lodConfig.sseThreshold = static_cast<double>(config.sseThresholdPx);
    lodConfig.tiltFactor = tiltFactor;
    lodSelector.SetConfig(lodConfig);
    
    // Tile ready callback
    auto isTileReady = [this](const std::string& key) -> bool {
        auto it = tiles.find(key);
        if (it == tiles.end()) return false;
        return it->second.ownsTexture || 
               it->second.textureState == TextureState::LOAD_OK ||
               it->second.textureState == TextureState::LOAD_OK_NO_DATA;
    };
    
    // Run tile selection
    auto selectionResult = lodSelector.Select(cameraPos, mvp, 
                                              config.windowWidth, config.windowHeight,
                                              isTileReady);
    
    // Copy results to legacy format
    requiredTiles = std::move(selectionResult.required);
    leafTiles = std::move(selectionResult.leaves);
    cellDivisionCount = selectionResult.refinedCount;
                         
    static double lastDebugTimeTiles = 0.0;
    if (glfwGetTime() - lastDebugTimeTiles > 2.0) {
        lastDebugTimeTiles = glfwGetTime();
        // Count tiles by state
        int loading = 0, ready = 0, failed = 0, unloaded = 0;
        for (const auto& kv : tiles) {
            switch (kv.second.loadState) {
                case TileLoadState::READY: ready++; break;
                case TileLoadState::FETCHING:
                case TileLoadState::DECODING:
                case TileLoadState::UPLOADING:
                case TileLoadState::SCHEDULED: loading++; break;
                case TileLoadState::FAILED: failed++; break;
                default: unloaded++; break;
            }
        }
        std::cout << "[Tiles] Required: " << requiredTiles.size() 
                  << " | Ready: " << ready << " | Loading: " << loading 
                  << " | Failed: " << failed << " | Unloaded: " << unloaded << std::endl;
        std::cout << "[Zoom] Current: " << currentZoom << " Max: " << baseLayerMaxZoom << std::endl;
    }
                         
    int maxLevel = -1;
    float maxLevelFloat = 0.0f;
    for (const auto& key : leafTiles) {
        int z = 0, x = 0, y = 0;
        if (std::sscanf(key.c_str(), "%d/%d/%d", &z, &x, &y) == 3) {
            float ratio = ComputeTileSseRatio(cameraPos, config.windowHeight, fovRad, z, x, y,
                                              config.sseThresholdPx, tiltFactor);
            float levelFloat = ComputeTileLevelFloat(ratio);
            if (z > maxLevel) {
                maxLevel = z;
                maxLevelFloat = levelFloat;
            } else if (z == maxLevel && levelFloat > maxLevelFloat) {
                maxLevelFloat = levelFloat;
            }
        }
    }

    if (cellDivisionCount > maxCellCreatedToday) {
      maxCellCreatedToday = cellDivisionCount;
    }

    if (maxLevel < 0) {
      maxLevel = (currentZoom >= 0) ? currentZoom : config.minZoom;
      maxLevelFloat = 0.0f;
    }
    maxDrawnZoom = maxLevel;
    drawnMaxLevel = maxLevel;
    drawnMaxLevelFloat = maxLevelFloat;
    
    size_t meshRebuilds = 0;
    size_t queueSize = 0;
    size_t textureUploads = 0;
    
    HeightSampler heightSampler;
    const HeightSampler* heightSamplerPtr = nullptr;
    if (config.demEnabled && !config.demBaseUrl.empty()) {
      heightSampler = [this](double lonDeg, double latDeg, int level, double& heightMeters) {
        return owner->SampleTerrainHeightMeters(lonDeg, latDeg, level, heightMeters);
      };
      heightSamplerPtr = &heightSampler;
    }

    // P2: Aggressive Texture Upload - faster tile rendering
    // Increased limits for faster tile loading on modern GPUs
    size_t maxUploads = 8;   // Base: more aggressive
    size_t maxRebuilds = 16; // Base: more aggressive
    
    if (dt < 0.010) { // > 100 FPS - very fast machine
        maxUploads = 32;
        maxRebuilds = 64;
    } else if (dt < 0.016) { // > 60 FPS
        maxUploads = 24;
        maxRebuilds = 48;
    } else if (dt < 0.033) { // > 30 FPS
        maxUploads = 16;
        maxRebuilds = 32;
    }

    {
      std::lock_guard<std::mutex> lock(downloadMutex);
      queueSize = downloadQueue.size();
    }

    // Scheduler controlled by config.useScheduler flag (see globe_config.h)
    // Legacy download system is faster for initial loading, scheduler provides
    // more sophisticated priority management for interactive use
    TileScheduler* activeScheduler = config.useScheduler ? scheduler.get() : nullptr;
    SyncRasterTiles(&deferredQueue, tiles, visibleTiles, config, loadingTexture,
                    requiredTiles, leafTiles, pendingRaster, downloadQueue,
                    downloadMutex, downloadCv, config.segments,
                    heightSamplerPtr,
                    meshRebuilds, maxRebuilds, queueSize,
                    textureUploads, maxUploads,
                    activeScheduler,
                    static_cast<uint32_t>(frameCount),
                    &pendingMutex,
                    &cameraPos, currentZoom, &mvp);
    
    // Copy layer configs under lock to avoid race with add/remove/set APIs
    std::vector<RasterLayerConfig> layerConfigsCopy;
    {
      std::lock_guard<std::mutex> lock(configMutex);
      layerConfigsCopy = config.rasterLayers;
    }
    
    for (const auto& layerConfig : layerConfigsCopy) {
      auto& layerData = layerTiles[layerConfig.id];
      layerData.layerId = layerConfig.id;
      SyncLayerTiles(&deferredQueue, layerData, layerConfig, loadingTexture, requiredTiles, leafTiles,
                     scheduler.get(),
                     config.segments, heightSamplerPtr, zoom, pendingLayerDownloads,
                     meshRebuilds, queueSize,
                     textureUploads,
                     maxUploads,
                     maxRebuilds,
                     &pendingMutex,
                     config.demDebug,
                     &cameraPos, &mvp);
    }
    
    if (vectorEnabled && !config.vectorTileUrl.empty()) {
      SyncVectorTiles(&deferredQueue, vectorTiles, visibleVectorTiles, config, leafTiles,
                      scheduler.get(), &cameraPos, currentZoom, &mvp);
    }

    // Pass 3: Base non-leaf fallback meshes (after overlay leaves get priority)
    SyncRasterTilesPass3(tiles, config, requiredTiles, leafTiles,
                         config.segments, heightSamplerPtr, meshRebuilds, maxRebuilds);

    // Scheduler update AFTER all sync operations so tasks scheduled this frame are processed
    if (scheduler) {
        scheduler->Update([this](const SchedulerKey& k) -> Tile* {
            std::string keyStr = MakeTileKey(k.tileKey.level, k.tileKey.x, k.tileKey.y);
            if (k.isVector && vectorEnabled) {
                auto it = vectorTiles.find(keyStr);
                if (it != vectorTiles.end()) return &it->second.schedulerTile;
            } else if (!k.layerId.empty()) {
                auto lit = layerTiles.find(k.layerId);
                if (lit != layerTiles.end()) {
                    auto it = lit->second.tiles.find(keyStr);
                    if (it != lit->second.tiles.end()) return &it->second;
                }
            } else {
                auto it = tiles.find(keyStr);
                if (it != tiles.end()) return &it->second;
            }
            return nullptr;
        }, glfwGetTime());
    }

    ProcessReadyDownloads();

    glState.UseProgram(program);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1i(texLoc, 0);
    glUniform1i(tex2Loc, 1); // Set once
    
    // Lighting uniforms removed


    for (auto& kv : tiles) {
      Tile& tile = kv.second;
      if (tile.ownsTexture && tile.fade < 1.0f) {
        float duration = config.fadeDuration;
        float step = duration > 0.0f ? static_cast<float>(dt / duration) : 1.0f;
        tile.fade += step;
        if (tile.fade > 1.0f) tile.fade = 1.0f;
      }
    }

    // RENDER OPTIMIZATION: Sort tiles front-to-back for early-Z rejection
    // Closer tiles rendered first allow GPU to skip fragments behind them
    std::sort(visibleTiles.begin(), visibleTiles.end(),
              [&cameraPos](const Tile* a, const Tile* b) {
                float distA = glm::length(a->center - cameraPos);
                float distB = glm::length(b->center - cameraPos);
                return distA < distB;  // Front-to-back
              });

    for (auto* tile : visibleTiles) {
      if (!SphereInFrustum(frustum, tile->center, tile->radius)) continue;
      if (tile->mesh.vao == 0 || tile->mesh.indexCount == 0) continue;
      
      glState.BindVAO(tile->mesh.vao);

      ResolvedTexture base;
      ResolveAncestorTexture(tiles, tile->z, tile->x, tile->y, true, base);
      if (!base.valid) {
        base.texture = loadingTexture;
        base.uvOffset = glm::vec2(0.0f);
        base.uvScale = glm::vec2(1.0f);
        base.valid = true;
      }

      ResolvedTexture parent;
      if (!ResolveAncestorTexture(tiles, tile->z, tile->x, tile->y, false, parent)) {
        parent.texture = loadingTexture;
        parent.uvOffset = glm::vec2(0.0f);
        parent.uvScale = glm::vec2(1.0f);
        parent.valid = true;
      }

      float edgeBlendWidth = (tile->edgeFlags != EDGE_NONE) ? 0.1f : 0.0f;

      glState.BindTexture(0, tile->ownsTexture ? tile->texture : base.texture);
      glState.BindTexture(1, parent.texture);
      
      if (tile->ownsTexture) {
        glUniform2f(uvScaleLoc, 1.0f, 1.0f);
        glUniform2f(uvOffsetLoc, 0.0f, 0.0f);
      } else {
        glUniform2f(uvScaleLoc, base.uvScale.x, base.uvScale.y);
        glUniform2f(uvOffsetLoc, base.uvOffset.x, base.uvOffset.y);
      }
      glUniform2f(uvScale2Loc, parent.uvScale.x, parent.uvScale.y);
      glUniform2f(uvOffset2Loc, parent.uvOffset.x, parent.uvOffset.y);
      
      float blendFactor = 0.0f;
      if (tile->ownsTexture) {
        float unpopFactor = tile->unpopFactor;
        if (!tile->unpopComplete && tile->unpopStartTime > 0.0) {
          double elapsed = glfwGetTime() - tile->unpopStartTime;
          float unpopDuration = config.fadeDuration;
          unpopFactor = std::min(1.0f, static_cast<float>(elapsed / unpopDuration));
          tile->unpopFactor = unpopFactor;
          if (unpopFactor >= 1.0f) tile->unpopComplete = true;
        }
        blendFactor = 1.0f - unpopFactor;
      }
      glUniform1f(blendFactorLoc, blendFactor);
      glUniform1f(edgeBlendLoc, edgeBlendWidth);
      // Base tiles use uAlpha=1.0 - unpop blending handles parent→child transition
      // tile.fade is only for overlay tiles to avoid dimming/holes during LOD transitions
      glUniform1f(alphaLoc, 1.0f);
      glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
      
      glDrawElements(GL_TRIANGLES, tile->mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
}

void GlobeEngine::Impl::RenderRasterOverlays(const glm::mat4& mvp, const std::array<Plane, 6>& frustum, double dt) {
    // Copy layer configs under lock to avoid race if config mutates on another thread
    std::vector<RasterLayerConfig> layersCopy;
    {
      std::lock_guard<std::mutex> lock(configMutex);
      if (config.rasterLayers.empty()) return;
      layersCopy.reserve(config.rasterLayers.size());
      for (const auto& layerConfig : config.rasterLayers) {
        if (layerConfig.visible) {
          layersCopy.push_back(layerConfig);
        }
      }
    }
    
    if (layersCopy.empty()) return;
    
    glState.UseProgram(program);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1i(texLoc, 0);
    glUniform1i(tex2Loc, 1);
    
    // Sort by zIndex for correct draw order
    std::sort(layersCopy.begin(), layersCopy.end(),
              [](const RasterLayerConfig& a, const RasterLayerConfig& b) {
                return a.zIndex < b.zIndex;  // Lower zIndex = drawn first (behind)
              });
    
    for (const auto& layerConfig : layersCopy) {
      auto layerIt = layerTiles.find(layerConfig.id);
      if (layerIt == layerTiles.end()) continue;
      auto& layerData = layerIt->second;
      
      // Update fade for all tiles in this layer
      for (auto& kv : layerData.tiles) {
        Tile& tile = kv.second;
        if (tile.ownsTexture && tile.fade < 1.0f) {
          float duration = config.fadeDuration;
          float step = duration > 0.0f ? static_cast<float>(dt / duration) : 1.0f;
          tile.fade += step;
          if (tile.fade > 1.0f) tile.fade = 1.0f;
        }
      }
      
      for (auto* tile : layerData.visibleTiles) {
        if (!SphereInFrustum(frustum, tile->center, tile->radius)) continue;
        if (tile->mesh.vao == 0 || tile->mesh.indexCount == 0) continue;
        
        glState.BindVAO(tile->mesh.vao);
        
        ResolvedTexture base;
        ResolveAncestorTexture(layerData.tiles, tile->z, tile->x, tile->y, true, base);
        if (!base.valid) continue;
        
        ResolvedTexture parent;
        if (!ResolveAncestorTexture(layerData.tiles, tile->z, tile->x, tile->y, false, parent)) {
          parent = base;
        }
        
        float edgeBlendWidth = (tile->edgeFlags != EDGE_NONE) ? 0.1f : 0.0f;
        
        glState.BindTexture(0, tile->ownsTexture ? tile->texture : base.texture);
        glState.BindTexture(1, parent.texture);
        
        if (tile->ownsTexture) {
          glUniform2f(uvScaleLoc, 1.0f, 1.0f);
          glUniform2f(uvOffsetLoc, 0.0f, 0.0f);
        } else {
          glUniform2f(uvScaleLoc, base.uvScale.x, base.uvScale.y);
          glUniform2f(uvOffsetLoc, base.uvOffset.x, base.uvOffset.y);
        }
        glUniform2f(uvScale2Loc, parent.uvScale.x, parent.uvScale.y);
        glUniform2f(uvOffset2Loc, parent.uvOffset.x, parent.uvOffset.y);
        
        float blendFactor = 0.0f;
        if (tile->ownsTexture) {
          float unpopFactor = tile->unpopFactor;
          if (!tile->unpopComplete && tile->unpopStartTime > 0.0) {
            double elapsed = glfwGetTime() - tile->unpopStartTime;
            float unpopDuration = config.fadeDuration;
            unpopFactor = std::min(1.0f, static_cast<float>(elapsed / unpopDuration));
            tile->unpopFactor = unpopFactor;
            if (unpopFactor >= 1.0f) tile->unpopComplete = true;
          }
          blendFactor = 1.0f - unpopFactor;
        }
        glUniform1f(blendFactorLoc, blendFactor);
        glUniform1f(edgeBlendLoc, edgeBlendWidth);
        // Apply tile.fade to alpha for smooth fade-in transition
        float tileAlpha = tile->ownsTexture ? (tile->fade * layerConfig.opacity) : layerConfig.opacity;
        glUniform1f(alphaLoc, tileAlpha);
        glUniform4fv(colorLoc, 1, glm::value_ptr(layerConfig.color));
        
        glDrawElements(GL_TRIANGLES, tile->mesh.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }
}

void GlobeEngine::Impl::InitPivotGizmo() {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    
    // 1. Circle (Line Loop)
    const int segments = 32;
    for (int i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) * 2.0f * glm::pi<float>() / segments;
        vertices.push_back(std::cos(angle)); // X
        vertices.push_back(std::sin(angle)); // Y
        vertices.push_back(0.0f);            // Z
        indices.push_back(i);
    }
    // Close the loop
    indices.push_back(0); 
    // Restart primitive for crosshair
    unsigned int crossStart = segments + 1;
    
    // 2. Crosshair (Lines)
    // Horizontal
    vertices.push_back(-1.2f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(1.2f);  vertices.push_back(0.0f); vertices.push_back(0.0f);
    // Vertical
    vertices.push_back(0.0f); vertices.push_back(-1.2f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(1.2f);  vertices.push_back(0.0f);
    
    indices.push_back(crossStart); indices.push_back(crossStart + 1);
    indices.push_back(crossStart + 2); indices.push_back(crossStart + 3);
    
    pivotIndexCount = static_cast<GLsizei>(indices.size());
    
    glGenVertexArrays(1, &pivotVao);
    glGenBuffers(1, &pivotVbo);
    
    glBindVertexArray(pivotVao);
    glBindBuffer(GL_ARRAY_BUFFER, pivotVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    // Element buffer not used directly with drawElements here because we might mix Loop/Lines
    // Actually, let's use GL_LINES for everything to simplify.
    // Rebuild for GL_LINES
    vertices.clear();
    // Circle
    for (int i = 0; i < segments; ++i) {
        float angle1 = static_cast<float>(i) * 2.0f * glm::pi<float>() / segments;
        float angle2 = static_cast<float>(i + 1) * 2.0f * glm::pi<float>() / segments;
        vertices.push_back(std::cos(angle1)); vertices.push_back(std::sin(angle1)); vertices.push_back(0.0f);
        vertices.push_back(std::cos(angle2)); vertices.push_back(std::sin(angle2)); vertices.push_back(0.0f);
    }
    // Cross
    vertices.push_back(-1.2f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(1.2f);  vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(-1.2f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(1.2f);  vertices.push_back(0.0f);
    
    pivotIndexCount = static_cast<GLsizei>(vertices.size() / 3);
    
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    glBindVertexArray(0);
}

void GlobeEngine::Impl::RenderPivot(const glm::mat4& viewProj) {
    glm::dvec3 pivot;
    if (!m_flightController.GetPivot(pivot)) return;
    
    if (vectorProgram && pivotVao) {
        glUseProgram(vectorProgram);
        
        // Calculate orientation to lie on the surface (or face camera)
        // Lying on surface looks more like a "ground target"
        glm::dvec3 up = glm::normalize(pivot);
        glm::dvec3 camPos = m_newCamera.GetPositionECEF();
        glm::dvec3 toCam = glm::normalize(camPos - pivot);
        
        // Right vector
        glm::dvec3 right = glm::cross(up, toCam);
        if (glm::length(right) < 0.001) right = glm::dvec3(1,0,0);
        right = glm::normalize(right);
        
        // Forward vector (surface tangent)
        glm::dvec3 forward = glm::cross(right, up);
        
        // Scale based on distance and FOV to keep screen size constant
        // Target: ~40 pixels wide on screen
        double dist = glm::length(camPos - pivot);
        float fovRad = glm::radians(m_newCamera.GetFov());
        float targetPixelSize = 40.0f;
        float scale = static_cast<float>(targetPixelSize * (dist * std::tan(fovRad * 0.5f) / config.windowHeight));
        
        // Construct Model Matrix
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(right, 0.0f);
        model[1] = glm::vec4(forward, 0.0f);
        model[2] = glm::vec4(up, 0.0f);
        model[3] = glm::vec4(pivot, 1.0f);
        
        // Scale
        model = glm::scale(model, glm::vec3(scale));
        
        // Raise slightly to avoid Z-fighting
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.05f));
        
        glm::mat4 mvp = glm::mat4(viewProj) * model;
        
        glUniformMatrix4fv(vectorMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform1f(vectorScaleLoc, 1.0f);
        
        // Google Earth style color (Yellow/Orange ring)
        glUniform4f(vectorColorLoc, 1.0f, 0.7f, 0.0f, 0.8f);
        
        glBindVertexArray(pivotVao);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, pivotIndexCount);
        glLineWidth(1.0f);
        glBindVertexArray(0);
    }
}

void GlobeEngine::Impl::RenderPoles(const glm::mat4& mvp) {
    if (poleProgram && northPoleMesh.vao && southPoleMesh.vao) {
      glUseProgram(poleProgram);
      glUniformMatrix4fv(poleMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
      
      glm::vec4 poleColor(0.95f, 0.97f, 1.0f, 1.0f);
      glUniform4fv(poleColorLoc, 1, glm::value_ptr(poleColor));
      
      glBindVertexArray(northPoleMesh.vao);
      glDrawElements(GL_TRIANGLES, northPoleMesh.indexCount, GL_UNSIGNED_INT, 0);
      
      glBindVertexArray(southPoleMesh.vao);
      glDrawElements(GL_TRIANGLES, southPoleMesh.indexCount, GL_UNSIGNED_INT, 0);
      
      glBindVertexArray(0);
    }
}

void GlobeEngine::Impl::RenderVectors(const glm::mat4& mvp, const std::array<Plane, 6>& frustum) {
    if (vectorEnabled && !config.vectorTileUrl.empty() && vectorProgram) {
      glUseProgram(vectorProgram);
      glUniformMatrix4fv(vectorMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
      
      // Sort vector tiles front-to-back for better Z performance
      std::vector<const VectorTile*> sortedVectorTiles(visibleVectorTiles.begin(), visibleVectorTiles.end());
      glm::vec3 camPos = glm::vec3(eyeWorld);
      std::sort(sortedVectorTiles.begin(), sortedVectorTiles.end(),
                [&camPos](const VectorTile* a, const VectorTile* b) {
                  return glm::length(a->center - camPos) < glm::length(b->center - camPos);
                });
      
      // Pass 1: Fills (water, land, buildings)
      glUniform1f(vectorScaleLoc, 1.002f);
      glUniform1f(vectorPointSizeLoc, 4.0f);
      
      // Water polygons - blue tint
      glUniform4f(vectorColorLoc, 0.15f, 0.45f, 0.75f, 0.35f);
      
      for (const auto* vt : sortedVectorTiles) {
        if (!SphereInFrustum(frustum, vt->center, vt->radius)) continue;
        float pxRadius = 0.0f;
        if (ComputeScreenRadius(mvp, vt->center, vt->radius, config.windowWidth, config.windowHeight, pxRadius)) {
          if (pxRadius * 2.0f < config.sseThresholdPx) continue;
        }
        if (vt->fillVertexCount > 0) {
          glBindVertexArray(vt->fillVao);
          glDrawArrays(GL_TRIANGLES, 0, vt->fillVertexCount);
        }
      }

      // Pass 2: Roads and boundaries - yellow/orange
      glUniform4f(vectorColorLoc, 1.0f, 0.75f, 0.2f, 0.85f);
      glLineWidth(1.5f);
      
      for (const auto* vt : sortedVectorTiles) {
        if (!SphereInFrustum(frustum, vt->center, vt->radius)) continue;
        float pxRadius = 0.0f;
        if (ComputeScreenRadius(mvp, vt->center, vt->radius, config.windowWidth, config.windowHeight, pxRadius)) {
          if (pxRadius * 2.0f < config.sseThresholdPx) continue;
        }
        if (vt->lineVertexCount > 0) {
          glBindVertexArray(vt->lineVao);
          glDrawArrays(GL_LINES, 0, vt->lineVertexCount);
        }
      }
      
      // Pass 3: Points of interest - white
      glUniform4f(vectorColorLoc, 1.0f, 1.0f, 1.0f, 0.9f);
      glUniform1f(vectorPointSizeLoc, 6.0f);
      
      for (const auto* vt : sortedVectorTiles) {
        if (!SphereInFrustum(frustum, vt->center, vt->radius)) continue;
        if (vt->pointVertexCount > 0) {
          glBindVertexArray(vt->pointVao);
          glDrawArrays(GL_POINTS, 0, vt->pointVertexCount);
        }
      }
      
      glBindVertexArray(0);
      glLineWidth(1.0f);
    }
}

// RenderAtmosphere removed


void GlobeEngine::Run() {
  if (!impl_->valid) return;

  impl_->lastFpsTime = glfwGetTime();
  impl_->lastFrameTime = impl_->lastFpsTime;
  impl_->frameCount = 0;
  impl_->fpsValue = 0.0;

  while (!glfwWindowShouldClose(impl_->window)) {
    glfwPollEvents();
    
    // Handle 'W' key for wireframe toggle (edge detection)
    bool wPressed = glfwGetKey(impl_->window, GLFW_KEY_W) == GLFW_PRESS;
    if (wPressed && !impl_->wireframeKeyPressed) {
      impl_->wireframeMode = !impl_->wireframeMode;
    }
    impl_->wireframeKeyPressed = wPressed;

    double frameNow = glfwGetTime();
    double dt = frameNow - impl_->lastFrameTime;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.1) dt = 0.1;
    impl_->lastFrameTime = frameNow;

    impl_->BeginUiFrame();

    // Update animation and inertia
    impl_->UpdateAnimation(dt);
    // impl_->UpdateFlyAnimation(dt);  // JS: XAZfZF fly-to-point animation - disabled for new controller test
    // impl_->Update2DAnimation(dt);   // JS: FlatNavigation animation - disabled for new controller test
    // impl_->UpdateInertia(dt);       // Legacy inertia - disabled
    
    // New Flight Controller Update
    impl_->m_flightController.Update(dt, frameNow);
    
    impl_->UpdateCenterLatLon();

    impl_->Render(dt);
    impl_->DrawUi();
    impl_->EndUiFrame();

    glBindVertexArray(0);
    glfwSwapBuffers(impl_->window);

    impl_->frameCount++;
    double now = glfwGetTime();
    double delta = now - impl_->lastFpsTime;
    if (delta >= 1.0) {
      impl_->fpsValue = static_cast<double>(impl_->frameCount) / delta;
      impl_->frameCount = 0;
      impl_->lastFpsTime = now;
      std::string title = "Native Globe | FPS: " +
                          std::to_string(static_cast<int>(impl_->fpsValue)) +
                          " | LOD: " + std::to_string(impl_->currentZoom) +
                          " | Tiles: " + std::to_string(impl_->visibleTiles.size());
      glfwSetWindowTitle(impl_->window, title.c_str());
    }
  }
}

void GlobeEngine::Shutdown() {
  if (!impl_) return;
  
  // Mark invalid early to prevent new work being queued
  impl_->valid = false;
  
  if (impl_->labelManager) {
      impl_->labelManager->Shutdown();
  }
  
  // Phase 5: Ensure decoder threads don't access dead scheduler
  if (impl_->imageDecoder) {
      impl_->imageDecoder->Invalidate();
  }
  
  impl_->ShutdownUi();
  impl_->StopWorker();
  
  // Drain pending icon maps (free pixel data)
  {
    std::lock_guard<std::mutex> lock(impl_->iconMapMutex);
    for (auto& item : impl_->pendingIconMaps) {
      if (item.callback) item.callback(false);  // Notify failure
    }
    impl_->pendingIconMaps.clear();
  }
  
  // Flush any pending deferred deletions
  impl_->ProcessDeferredDeletions(100000);
  
  for (auto& kv : impl_->tiles) {
    DestroyTile(kv.second, nullptr);
  }
  impl_->tiles.clear();
  impl_->visibleTiles.clear();
  for (auto& kv : impl_->vectorTiles) {
    DestroyVectorTile(kv.second, nullptr);
  }
  impl_->vectorTiles.clear();
  impl_->visibleVectorTiles.clear();

  if (impl_->program) {
    glDeleteProgram(impl_->program);
    impl_->program = 0;
  }
  if (impl_->vectorProgram) {
    glDeleteProgram(impl_->vectorProgram);
    impl_->vectorProgram = 0;
  }
  if (impl_->loadingTexture) {
    glDeleteTextures(1, &impl_->loadingTexture);
    impl_->loadingTexture = 0;
  }

  curl_global_cleanup();

  if (impl_->window) {
    glfwDestroyWindow(impl_->window);
    impl_->window = nullptr;
  }
  glfwTerminate();
  impl_->valid = false;
}

bool GlobeEngine::RunLodTest() {
  if (!impl_ || !impl_->valid) return false;
  
  std::cout << "\n=== LOD Test Suite ===\n" << std::endl;
  
  struct TestCase {
    const char* name;
    float distance;
    int minExpectedLod;
    int maxExpectedLod;
  };
  
  // Test cases: different camera distances
  // LOD is chosen where tile screen size is closest to 256px
  TestCase tests[] = {
    {"Far view (4x radius)",      GLOBE_RADIUS * 4.0f,   2, 2},
    {"Medium view (2x radius)",   GLOBE_RADIUS * 2.0f,   3, 4},
    {"Close view (1.2x radius)",  GLOBE_RADIUS * 1.2f,   3, 4},
    {"Very close (1.05x radius)", GLOBE_RADIUS * 1.05f,  3, 4},
  };
  
  int passed = 0;
  int failed = 0;
  
  // Disable fixed zoom for testing
  impl_->config.useFixedZoom = false;
  
  for (const auto& test : tests) {
    // Set camera distance
    SetDistance(test.distance);
    
    // Render one frame to update LOD
    glfwPollEvents();
    
    // Build MVP and calculate zoom
    glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                      static_cast<float>(impl_->config.windowWidth) / impl_->config.windowHeight,
                                      0.01f, 100.0f * GLOBE_RADIUS);
    glm::mat4 view = impl_->GetViewMatrix();
    glm::mat4 mvp = proj * view;
    
    double altitudeMeters = impl_->altitudeWorld / static_cast<double>(GLOBE_RADIUS_K);
    if (altitudeMeters < 0.0) altitudeMeters = 0.0;
    double zoomExact = FindLodFromAltitudeMeters(altitudeMeters, impl_->config.minZoom, impl_->config.maxZoom);
    int zoom = static_cast<int>(std::round(zoomExact));
    
    size_t tileCount = impl_->visibleTiles.size();
    
    bool lodOk = (zoom >= test.minExpectedLod && zoom <= test.maxExpectedLod);
    
    if (lodOk) {
      std::cout << "[PASS] " << test.name << ": LOD=" << zoom 
                << " (expected " << test.minExpectedLod << "-" << test.maxExpectedLod << ")"
                << ", tiles=" << tileCount << std::endl;
      passed++;
    } else {
      std::cout << "[FAIL] " << test.name << ": LOD=" << zoom 
                << " (expected " << test.minExpectedLod << "-" << test.maxExpectedLod << ")"
                << ", tiles=" << tileCount << std::endl;
      failed++;
    }
  }
  
  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
  
  // Skip normal shutdown in test mode to avoid OpenGL cleanup issues
  // Just exit cleanly
  std::exit(failed == 0 ? 0 : 1);
}

bool GlobeEngine::RunDemTest() {
  if (!impl_ || !impl_->valid) return false;
  
  std::cout << "\n=== DEM/Mesh Height Test Suite ===\n" << std::endl;
  
  struct DemTestCase {
    const char* name;
    double lon;
    double lat;
    double minHeight;  // Expected minimum height (meters)
    double maxHeight;  // Expected maximum height (meters)
  };
  
  // Test cases with known locations
  // Note: Heights depend on your DEM service data
  DemTestCase tests[] = {
    {"Sea level (Atlantic)",    -30.0, 30.0,    -100.0, 100.0},    // Ocean - should be ~0
    {"Turkey (Ankara area)",    32.8, 39.9,     800.0, 1200.0},    // Ankara ~900m
    {"Alps (Switzerland)",      8.0, 46.5,      1000.0, 4500.0},   // Mountain range
    {"Death Valley",            -116.8, 36.2,   -100.0, 200.0},    // Below sea level area
  };
  
  int passed = 0;
  int failed = 0;
  int skipped = 0;
  
  for (const auto& test : tests) {
    double height = 0.0;
    bool ok = SampleTerrainHeightMeters(test.lon, test.lat, 14, height);
    
    if (!ok) {
      std::cout << "[SKIP] " << test.name << " (" << test.lon << ", " << test.lat << "): DEM request failed" << std::endl;
      skipped++;
      continue;
    }
    
    bool heightOk = (height >= test.minHeight && height <= test.maxHeight);
    
    if (heightOk) {
      std::cout << "[PASS] " << test.name << ": height=" << height << "m"
                << " (expected " << test.minHeight << "-" << test.maxHeight << "m)" << std::endl;
      passed++;
    } else {
      std::cout << "[FAIL] " << test.name << ": height=" << height << "m"
                << " (expected " << test.minHeight << "-" << test.maxHeight << "m)" << std::endl;
      failed++;
    }
  }
  
  std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed, " << skipped << " skipped ===" << std::endl;
  
  // Exit cleanly to avoid OpenGL cleanup issues
  std::exit(failed == 0 ? 0 : 1);
}

bool GlobeEngine::Run2DClampTest() {
  if (!impl_ || !impl_->valid) return false;

  std::cout << "\n=== 2D Clamp Test ===\n" << std::endl;

  // Force 2D mode and a known navigation limit
  Set2DMode(true);
  SetNavigationLOD(10);

  // Reset clamp telemetry
  impl_->clampCount = 0;
  impl_->lastClampMin = 0.0;
  impl_->lastClampMax = 0.0;

  // Force clamps on both ends
  const double minDist = impl_->navMaxFlatDist * GLOBE_RADIUS_K;
  const double maxDist = impl_->navMinFlatDist * GLOBE_RADIUS_K;
  SetDistance(static_cast<float>(minDist * 0.1));
  SetDistance(static_cast<float>(maxDist * 10.0));

  std::cout << "[LOD] " << DumpParitySnapshot() << std::endl;

  // Also validate SetNavigationDist path
  SetNavigationDist(200000.0);  // meters
  impl_->clampCount = 0;
  impl_->lastClampMin = 0.0;
  impl_->lastClampMax = 0.0;

  const double distMin2 = impl_->navMaxFlatDist * GLOBE_RADIUS_K;
  const double distMax2 = impl_->navMinFlatDist * GLOBE_RADIUS_K;
  SetDistance(static_cast<float>(distMin2 * 0.1));
  SetDistance(static_cast<float>(distMax2 * 10.0));

  std::cout << "[DIST] " << DumpParitySnapshot() << std::endl;

  std::exit(0);
}

bool GlobeEngine::RunParityTest() {
  if (!impl_ || !impl_->valid) return false;

  std::cout << "=== Parity Snapshot Test (Phase 0) ===\n" << std::endl;

  // Scenario: Fly to Istanbul
  SetDirectPos(29.0, 41.0, 10000.0, 0.0, 0.0);

  // Simulate 60 frames to stabilize loading
  const int FRAMES = 60;
  const double dt = 0.016; // 60 FPS

  for (int i = 0; i < FRAMES; ++i) {
    glfwPollEvents();

    // Update logic
    impl_->UpdateAnimation(dt);
    impl_->m_flightController.Update(dt, glfwGetTime());
    impl_->UpdateCenterLatLon();

    // Render setup (matrices)
    glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                      static_cast<float>(impl_->config.windowWidth) / impl_->config.windowHeight,
                                      0.01f, 100.0f * GLOBE_RADIUS);
    glm::mat4 view = impl_->GetViewMatrix();
    glm::mat4 mvp = proj * view;

    // Frustum update
    glm::mat4 invMvp = glm::inverse(mvp);
    glm::vec4 insideNDC(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 insideWorld = invMvp * insideNDC;
    glm::vec3 insidePoint = glm::vec3(insideWorld) / insideWorld.w;
    const auto frustum = ExtractFrustumPlanes(mvp, insidePoint);

    // Zoom calc
    int lodMaxZoom = impl_->config.useFixedZoom ? impl_->config.fixedZoom : impl_->config.maxZoom;
    impl_->ClampCameraDistance();
    double altitudeMeters = impl_->altitudeWorld / static_cast<double>(GLOBE_RADIUS_K);
    if (altitudeMeters < 0.0) altitudeMeters = 0.0;
    double zoomExact = impl_->config.useFixedZoom
                           ? static_cast<double>(impl_->config.fixedZoom)
                           : FindLodFromAltitudeMeters(altitudeMeters, impl_->config.minZoom, lodMaxZoom);
    int zoom = impl_->config.useFixedZoom
                   ? impl_->config.fixedZoom
                   : static_cast<int>(std::round(zoomExact));
    if (zoom < impl_->config.minZoom) zoom = impl_->config.minZoom;
    if (zoom > lodMaxZoom) zoom = lodMaxZoom;
    zoomExact = ClampLod(zoomExact, impl_->config.minZoom, lodMaxZoom);

    auto [centerX, centerY] = GeoToTileXY(JsRadianToDegree(impl_->camera.camLongLat.y),
                                          JsRadianToDegree(impl_->camera.camLongLat.x), zoom);
    const int n = 1 << zoom;
    centerX = WrapTileX(centerX, n);
    centerY = ClampTileY(centerY, n);

    impl_->currentZoom = zoom;
    impl_->currentZoomExact = zoomExact;
    impl_->currentCenterX = centerX;
    impl_->currentCenterY = centerY;

    // Tile selection
    std::unordered_set<std::string> requiredTiles;
    std::vector<std::string> leafTiles;
    impl_->cellDivisionCount = 0;
    impl_->maxCellCanBeCreated = impl_->GetDynamicMaxCellCanBeCreated();

    float tiltFactor = 1.0f;
    if (!impl_->is2D) {
      tiltFactor = std::clamp(1.0f - static_cast<float>(impl_->camera.tiltDeg / 150.0), 0.0f, 1.0f);
    }
    
    glm::vec3 cameraPos = glm::vec3(JsToWorld(impl_->eyeJs));
    float fovRad = glm::radians(GLOBE_FOV);

    // Use TileLodSelector (same as main render loop)
    earth::TileLodSelector::Config lodConfig;
    lodConfig.minZoom = impl_->config.minZoom;
    lodConfig.maxZoom = lodMaxZoom;
    lodConfig.sseThreshold = static_cast<double>(impl_->config.sseThresholdPx);
    lodConfig.tiltFactor = tiltFactor;
    impl_->lodSelector.SetConfig(lodConfig);
    
    auto isTileReady = [this](const std::string& key) -> bool {
        auto it = impl_->tiles.find(key);
        if (it == impl_->tiles.end()) return false;
        return it->second.ownsTexture || 
               it->second.textureState == TextureState::LOAD_OK ||
               it->second.textureState == TextureState::LOAD_OK_NO_DATA;
    };
    
    auto selectionResult = impl_->lodSelector.Select(cameraPos, mvp,
                                                      impl_->config.windowWidth, impl_->config.windowHeight,
                                                      isTileReady);
    requiredTiles = std::move(selectionResult.required);
    leafTiles = std::move(selectionResult.leaves);
    impl_->cellDivisionCount = selectionResult.refinedCount;

    // Sync tiles
    size_t meshRebuilds = 0;
    size_t queueSize = 0;
    size_t textureUploads = 0;
    {
      std::lock_guard<std::mutex> lock(impl_->downloadMutex);
      queueSize = impl_->downloadQueue.size();
    }

    SyncRasterTiles(&impl_->deferredQueue, impl_->tiles, impl_->visibleTiles, impl_->config, impl_->loadingTexture,
                    requiredTiles, leafTiles, impl_->pendingRaster, impl_->downloadQueue,
                    impl_->downloadMutex, impl_->downloadCv, impl_->config.segments,
                    nullptr, meshRebuilds, 32, // maxRebuilds
                    queueSize,
                    textureUploads,
                    16, // maxUploads
                    impl_->scheduler.get(),
                    static_cast<uint32_t>(i + 1),
                    &impl_->pendingMutex,
                    &cameraPos, impl_->currentZoom, &mvp);
    
    if (impl_->scheduler) {
        impl_->scheduler->Update([this](const SchedulerKey& k) -> Tile* {
            std::string keyStr = MakeTileKey(k.tileKey.level, k.tileKey.x, k.tileKey.y);
            if (k.isVector && impl_->vectorEnabled) {
                auto it = impl_->vectorTiles.find(keyStr);
                if (it != impl_->vectorTiles.end()) return &it->second.schedulerTile;
            } else if (!k.layerId.empty()) {
                auto lit = impl_->layerTiles.find(k.layerId);
                if (lit != impl_->layerTiles.end()) {
                    auto it = lit->second.tiles.find(keyStr);
                    if (it != lit->second.tiles.end()) return &it->second;
                }
            } else {
                auto it = impl_->tiles.find(keyStr);
                if (it != impl_->tiles.end()) return &it->second;
            }
            return nullptr;
        }, glfwGetTime());
    }

    impl_->ProcessReadyDownloads();

    // Simulate frame time
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    impl_->fpsValue = 60.0; // Fake stable FPS
  }

  std::cout << DumpParitySnapshot() << std::endl;
  std::exit(0);
}

void GlobeEngine::SetDistance(float distance) {
  impl_->camera.dist = distance;
  impl_->ClampCameraDistance();
  
  // Sync to new camera
  double lat, lon, alt;
  impl_->m_newCamera.GetLatLonAlt(lat, lon, alt);
  double newAlt = (distance - GLOBE_RADIUS) / GLOBE_RADIUS_K;
  impl_->m_newCamera.SetLatLonAlt(lat, lon, newAlt);
}

void GlobeEngine::SetPitch(float degrees) {
  impl_->camera.tiltDeg = degrees;
  if (impl_->is2D) {
    impl_->camera.tiltDeg = 0.0;
  } else {
    if (impl_->camera.tiltDeg < GLOBE_MIN_TILTANGLE) impl_->camera.tiltDeg = GLOBE_MIN_TILTANGLE;
    if (impl_->camera.tiltDeg > GLOBE_MAX_TILTANGLE) impl_->camera.tiltDeg = GLOBE_MAX_TILTANGLE;
  }
  impl_->m_newCamera.SetTilt(impl_->camera.tiltDeg);
  impl_->UpdateArcballMatrices();
}

void GlobeEngine::SetYaw(float degrees) {
  impl_->camera.ea.x = JsDegreeToRadian(degrees);
  JsEulToHMatrix(impl_->camera.ea, impl_->camera.arcball.abQuat);
  impl_->m_newCamera.SetHeading(degrees);
}

void GlobeEngine::SetDirectPos(double lonDeg, double latDeg, double distMeters, double northDeg, double tiltDeg) {
  // FAZ 3: JS parity - SaveLastScreenPosition tüm kamera API'lerinde
  impl_->SaveScreenPosition();
  
  // Stop any active flight controller animation or inertia
  impl_->m_flightController.StopAnimation();
  
  impl_->camera.ea.x = JsDegreeToRadian(northDeg);
  impl_->camera.ea.y = -JsDegreeToRadian(latDeg);
  impl_->camera.ea.z = JsDegreeToRadian(lonDeg);
  JsEulToHMatrix(impl_->camera.ea, impl_->camera.arcball.abQuat);

  double dist = distMeters * GLOBE_RADIUS_K;
  if (dist < impl_->navMinDist) dist = impl_->navMinDist;
  if (dist > impl_->navMaxDist) dist = impl_->navMaxDist;
  impl_->camera.dist = dist;

  if (impl_->is2D) {
    impl_->camera.tiltDeg = 0.0;
  } else {
    double tilt = tiltDeg;
    if (tilt <= 0.0) tilt = GLOBE_MIN_TILTANGLE;
    if (tilt < GLOBE_MIN_TILTANGLE) tilt = GLOBE_MIN_TILTANGLE;
    if (tilt > GLOBE_MAX_TILTANGLE) tilt = GLOBE_MAX_TILTANGLE;
    impl_->camera.tiltDeg = tilt;
  }
  
  // Sync with the new 6DOF camera system
  impl_->m_newCamera.SetLatLonAlt(latDeg, lonDeg, distMeters);
  impl_->m_newCamera.SetHeading(northDeg);
  impl_->m_newCamera.SetTilt(impl_->camera.tiltDeg);
  
  impl_->UpdateArcballMatrices();
}

void GlobeEngine::SetCenterLatLon(double latDeg, double lonDeg) {
  impl_->camera.ea.y = -JsDegreeToRadian(latDeg);
  impl_->camera.ea.z = JsDegreeToRadian(lonDeg);
  JsEulToHMatrix(impl_->camera.ea, impl_->camera.arcball.abQuat);
  
  double lat, lon, alt;
  impl_->m_newCamera.GetLatLonAlt(lat, lon, alt);
  impl_->m_newCamera.SetLatLonAlt(latDeg, lonDeg, alt);
}

void GlobeEngine::SetFixedZoom(int zoom) {
  if (zoom < impl_->config.minZoom) zoom = impl_->config.minZoom;
  if (zoom > impl_->config.maxZoom) zoom = impl_->config.maxZoom;
  impl_->config.fixedZoom = zoom;
  impl_->config.useFixedZoom = true;
}

void GlobeEngine::ZoomToLOD(int lod) {
  // FAZ 3: JS parity - ZoomToLOD flies camera to Sa[lod] + groundZ
  ZoomToAltitude(GetAltitudeFromLOD(lod));
}

void GlobeEngine::ZoomToAltitude(double altitudeMeters) {
  double lonDeg = JsRadianToDegree(impl_->camera.ea.z);
  double latDeg = -JsRadianToDegree(impl_->camera.ea.y);
  double northDeg = JsRadianToDegree(impl_->camera.ea.x);
  double groundHeight = 0.0;
  SampleTerrainHeightMeters(lonDeg, latDeg, -1, groundHeight);
  FlyToPoint(latDeg, lonDeg, altitudeMeters + groundHeight, northDeg, GetTiltAngle(), 0.5);
}

void GlobeEngine::SetZoomLimits(int minZoom, int maxZoom) {
  if (minZoom > maxZoom) std::swap(minZoom, maxZoom);
  impl_->config.minZoom = minZoom;
  impl_->config.maxZoom = maxZoom;
  impl_->config.useFixedZoom = false;
  if (impl_->config.fixedZoom < minZoom) impl_->config.fixedZoom = minZoom;
  if (impl_->config.fixedZoom > maxZoom) impl_->config.fixedZoom = maxZoom;
  impl_->ClampCameraDistance();
}

void GlobeEngine::ResetZoomLimits() {
  impl_->config.minZoom = 2;
  impl_->config.maxZoom = 22;
  impl_->config.useFixedZoom = false;
  impl_->ClampCameraDistance();
}

// P0: JS parity - public nav API wrappers
void GlobeEngine::SetMinNavigationLOD(int lod) {
  impl_->SetMinNavigationLOD(lod);
}

void GlobeEngine::SetMaxNavigationLOD(int lod) {
  impl_->SetMaxNavigationLOD(lod);
}

void GlobeEngine::SetMinNavigationDist(double distMeters) {
  impl_->SetMinNavigationDist(distMeters);
}

void GlobeEngine::SetMaxNavigationDist(double distMeters) {
  impl_->SetMaxNavigationDist(distMeters);
}

void GlobeEngine::SetNavigationLOD(int lod) {
  impl_->SetNavigationLOD(lod);
}

void GlobeEngine::SetNavigationDist(double distMeters) {
  impl_->SetNavigationDist(distMeters);
}

void GlobeEngine::ResetNavigationLimits() {
  impl_->ResetNavigationLimits();
}

double GlobeEngine::GetNavMinDist() const {
  return impl_->navMinDist;
}

double GlobeEngine::GetNavMaxDist() const {
  return impl_->navMaxDist;
}

bool GlobeEngine::GoToPreviousPosition() {
  if (!impl_) return false;
  ScreenPosition current = impl_->GetCurrentScreenPosition();
  ScreenPosition target;
  if (!impl_->positionHistory.GoToPrev(current, target)) return false;
  impl_->ApplyScreenPosition(target);
  return true;
}

bool GlobeEngine::GoToNextPosition() {
  if (!impl_) return false;
  ScreenPosition target;
  if (!impl_->positionHistory.GoToNext(target)) return false;
  impl_->ApplyScreenPosition(target);
  return true;
}

bool GlobeEngine::IsPreviousPositionAvailable() const {
  return impl_ && impl_->positionHistory.IsPrevAvailable();
}

bool GlobeEngine::IsNextPositionAvailable() const {
  return impl_ && impl_->positionHistory.IsNextAvailable();
}

void GlobeEngine::ResetPositionHistory() {
  if (impl_) {
    impl_->positionHistory.Clear();
  }
}

void GlobeEngine::SetTileRadius(int radius) {
  impl_->config.tileRadius = radius;
}

void GlobeEngine::Set2DMode(bool enabled) {
  impl_->is2D = enabled;
}

void GlobeEngine::SetMeshCacheSize(size_t size) {
  size_t clamped = std::max<size_t>(1, size);
  impl_->config.meshCacheSize = clamped;
  impl_->config.demCacheSize = clamped;
}

void GlobeEngine::SetMeshRetryOptions(bool retry, bool continueDivision) {
  impl_->config.meshRetryAtTimeout = retry;
  impl_->config.meshContinueDivision = continueDivision;
}

void GlobeEngine::SetLang(const std::string& lang) {
  impl_->language = lang;
}

void GlobeEngine::SetFlashPeriod(int ms) {
  impl_->flashPeriod = ms;
}

int GlobeEngine::GetFlashPeriod() const {
  return impl_->flashPeriod;
}

void GlobeEngine::SetScreenWidthMeters(double widthMeters, bool lock) {
  if (widthMeters <= 0.0) return;
  double distMeters = impl_->FindDistForSrcWdMeter(widthMeters);
  if (lock) {
    impl_->SetNavigationDist(distMeters);
    if (!impl_->is2D) {
      Set2DMode(true);
    }
    return;
  }
  double clamped = impl_->GetDistByMinMaxLOD(distMeters);
  impl_->FlyToCurrentWithDist(clamped);
}

int GlobeEngine::GetCurrentZoom() const {
  return impl_->currentZoom;
}

double GlobeEngine::GetCurrentZoomExact() const {
  if (impl_) return impl_->currentZoomExact;
  return 0.0;
}

int GlobeEngine::GetMaxDrawnZoom() const {
  if (impl_) return impl_->maxDrawnZoom;
  return 0;
}

double GlobeEngine::GetDrawnMaxLevelExact() const {
  if (!impl_) return 0.0;
  return static_cast<double>(impl_->drawnMaxLevel) + static_cast<double>(impl_->drawnMaxLevelFloat);
}

int GlobeEngine::GetMinZoom() const {
  if (impl_) return impl_->config.minZoom;
  return 0;
}

int GlobeEngine::GetMaxZoom() const {
  return impl_->config.maxZoom;
}

float GlobeEngine::GetDistance() const {
  return static_cast<float>(impl_->altitudeWorld / GLOBE_RADIUS_K);
}

double GlobeEngine::GetCameraDistMeters() const {
  return impl_->camera.dist / static_cast<double>(GLOBE_RADIUS_K);
}

bool GlobeEngine::IsValid() const {
  return impl_->valid;
}

float GlobeEngine::GetPitch() const {
  return static_cast<float>(impl_->camera.tiltDeg);
}

float GlobeEngine::GetYaw() const {
  return static_cast<float>(JsRadianToDegree(impl_->camera.ea.x));
}

double GlobeEngine::GetCamZ() const {
  return impl_->camera.camZ / static_cast<double>(GLOBE_RADIUS_K);
}

int GlobeEngine::GetScreenWidth() const {
  return impl_->config.windowWidth;
}

int GlobeEngine::GetScreenHeight() const {
  return impl_->config.windowHeight;
}

double GlobeEngine::GetFPS() const {
  return impl_->fpsValue;
}

glm::dvec3 GlobeEngine::GetEulerAngles() const {
  return glm::dvec3(impl_->camera.ea.x, impl_->camera.ea.y, impl_->camera.ea.z);
}

size_t GlobeEngine::GetTextureCacheSize() const {
  if (!impl_) return 0;
  size_t total = 0;
  // Base layer tiles
  for (const auto& [key, tile] : impl_->tiles) {
    total += tile.estimatedBytes;
  }
  // Multi-layer tiles
  for (const auto& [layerId, layerData] : impl_->layerTiles) {
    for (const auto& [key, tile] : layerData.tiles) {
      total += tile.estimatedBytes;
    }
  }
  return total;
}

double GlobeEngine::GetCenterLat() const {
  return impl_->centerLat;
}

double GlobeEngine::GetCenterLon() const {
  return impl_->centerLon;
}

double GlobeEngine::GetAltitude() const {
  return impl_->altitudeWorld / GLOBE_RADIUS_K;
}

double GlobeEngine::LodFromAltitudeMeters(double altitudeMeters) const {
  int minZoom = impl_->config.minZoom;
  int maxZoom = impl_->config.useFixedZoom ? impl_->config.fixedZoom : impl_->config.maxZoom;
  return FindLodFromAltitudeMeters(altitudeMeters, minZoom, maxZoom);
}

double GlobeEngine::AltitudeMetersFromLod(double lod) const {
  return FindAltitudeFromLod(lod);
}

double GlobeEngine::GetNorthAngle() const {
  return JsRadianToDegree(impl_->camera.ea.x);
}

// JS parity: FixNorthAngle - normalize to [-180, 180]
static double FixNorthAngle(double angleDeg) {
  while (angleDeg > 180.0) angleDeg -= 360.0;
  while (angleDeg < -180.0) angleDeg += 360.0;
  return angleDeg;
}

void GlobeEngine::SetNorthAngle(double angleDeg) {
  angleDeg = FixNorthAngle(angleDeg);
  impl_->StopAnimation(false);
  impl_->camera.ea.x = impl_->lockNorth ? 0.0 : JsDegreeToRadian(angleDeg);
  JsEulToHMatrix(impl_->camera.ea, impl_->camera.arcball.abQuat);
  impl_->m_newCamera.SetHeading(impl_->lockNorth ? 0.0 : angleDeg);
}

void GlobeEngine::TurnToNorthAngle(double angleDeg, double duration) {
  angleDeg = FixNorthAngle(angleDeg);
  
  double lat, lon, alt;
  impl_->m_newCamera.GetLatLonAlt(lat, lon, alt); // alt is in meters? 
  // GetLatLonAlt in PerspectiveCamera: 
  // alt = (r_val - GLOBE_RADIUS) * 1000.0; (Yes, meters)
  
  // Use FlightController via FlyToPoint to ensure smooth transition
  FlyToPoint(lat, lon, alt, angleDeg, impl_->m_newCamera.GetTilt(), duration > 0.0 ? duration : 0.5);
}

bool GlobeEngine::IsScreenMoving() const {
  if (!impl_->is2D) {
      return impl_->m_flightController.IsMoving();
  }
  return impl_->dragging || impl_->animating;
}

bool GlobeEngine::GetCursorPos(double& outX, double& outY) const {
  outX = impl_->cursorX;
  outY = impl_->cursorY;
  return true;
}

void GlobeEngine::UpdateCursorPosFromScreen(int screenX, int screenY) {
  impl_->cursorX = static_cast<double>(screenX);
  impl_->cursorY = static_cast<double>(screenY);
  double lat = 0.0;
  double lon = 0.0;
  impl_->lastMouseOnGlobe = ScreenToGeo(screenX, screenY, lat, lon);
  if (impl_->lastMouseOnGlobe) {
    impl_->lastMouseLat = lat;
    impl_->lastMouseLon = lon;
  }
}

double GlobeEngine::GetTiltAngle() const {
  return impl_->camera.tiltDeg;
}

// JS Navigation parity APIs
void GlobeEngine::SetLockNorth(bool lock) {
  impl_->lockNorth = lock;
  impl_->m_flightController.SetLockNorth(lock);
  if (lock) {
    // Reset north angle to 0 when locking
    impl_->camera.ea.x = 0.0;
    JsEulToHMatrix(impl_->camera.ea, impl_->camera.arcball.abQuat);
  }
}

bool GlobeEngine::GetLockNorth() const {
  return impl_->lockNorth;
}

void GlobeEngine::SetMouseWheelMode(bool zoomToCursor) {
  impl_->mouseWheelZoomToCursor = zoomToCursor;
  impl_->m_flightController.SetMouseWheelSettings(impl_->mouseWheelZoomToCursor, impl_->mouseWheelReverse);
}

bool GlobeEngine::GetMouseWheelMode() const {
  return impl_->mouseWheelZoomToCursor;
}

void GlobeEngine::SetMouseWheelDirection(bool reverse) {
  impl_->mouseWheelReverse = reverse;
  impl_->m_flightController.SetMouseWheelSettings(impl_->mouseWheelZoomToCursor, impl_->mouseWheelReverse);
}

bool GlobeEngine::GetMouseWheelDirection() const {
  return impl_->mouseWheelReverse;
}

void GlobeEngine::SetNavigationSpeed(double speed) {
  impl_->navigationSpeed = speed > 0.0 ? speed : NAVIGATION_SPEED_DEFAULT;
  impl_->m_flightController.SetNavigationSpeed(impl_->navigationSpeed);
}

double GlobeEngine::GetNavigationSpeed() const {
  return impl_->navigationSpeed;
}

void GlobeEngine::SetArrowKeysNavSpeed(double speed) {
  impl_->arrowKeySpeed = speed > 0.0 ? speed : ARROW_KEY_SPEED_DEFAULT;
}

void GlobeEngine::StartWheelZoomInDist(double lonDeg, double latDeg) {
  impl_->wheelZoomActive = false;
  impl_->wheelZoomOrbitValid = false;
  double startOD = impl_->camera.dist;
  {
    glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                      static_cast<float>(impl_->config.windowWidth) / impl_->config.windowHeight,
                                      0.01f, 100.0f * GLOBE_RADIUS);
    glm::mat4 view = impl_->GetViewMatrix();
    impl_->FindOrbitPoint(proj, view);
    if (impl_->orbitValid) {
      startOD = glm::length(impl_->eyeWorld - impl_->orbitPoint);
    }
  }
  impl_->BeginWheelZoom(lonDeg, latDeg, startOD, true, impl_->orbitValid,
                        impl_->orbitValid ? impl_->orbitPoint : glm::dvec3(GLOBE_RADIUS, 0.0, 0.0), true);
}

void GlobeEngine::StartWheelZoomOutDist(double lonDeg, double latDeg) {
  impl_->wheelZoomActive = false;
  impl_->wheelZoomOrbitValid = false;
  double startOD = impl_->camera.dist;
  {
    glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                      static_cast<float>(impl_->config.windowWidth) / impl_->config.windowHeight,
                                      0.01f, 100.0f * GLOBE_RADIUS);
    glm::mat4 view = impl_->GetViewMatrix();
    impl_->FindOrbitPoint(proj, view);
    if (impl_->orbitValid) {
      startOD = glm::length(impl_->eyeWorld - impl_->orbitPoint);
    }
  }
  impl_->BeginWheelZoom(lonDeg, latDeg, startOD, false, impl_->orbitValid,
                        impl_->orbitValid ? impl_->orbitPoint : glm::dvec3(GLOBE_RADIUS, 0.0, 0.0), true);
}

void GlobeEngine::FlyToPoint(double lat, double lon, double altitude, double northDeg, double tiltDeg, double duration) {
  double distMeters = altitude;
  // Note: FlyToPoint altitude parameter is often distance from center or surface? 
  // API doc says "altitude". 
  // JS logic in api_FlyToPoint: 
  // if (distMeter < GLOBE_MIN_DIST) distMeter = GLOBE_MIN_DIST;
  // FlightController::FlyToLocation expects altitude relative to surface (distMeters argument name)
  
  if (distMeters < impl_->navMinDist / GLOBE_RADIUS_K) distMeters = impl_->navMinDist / GLOBE_RADIUS_K;
  if (distMeters > impl_->navMaxDist / GLOBE_RADIUS_K) distMeters = impl_->navMaxDist / GLOBE_RADIUS_K;

  double tilt = tiltDeg;
  if (impl_->is2D) {
    tilt = 0.0;
  } else {
    if (tilt <= 0.0) tilt = GLOBE_MIN_TILTANGLE;
    if (tilt < GLOBE_MIN_TILTANGLE) tilt = GLOBE_MIN_TILTANGLE;
    if (tilt > GLOBE_MAX_TILTANGLE) tilt = GLOBE_MAX_TILTANGLE;
  }

  impl_->m_flightController.FlyToLocation(lat, lon, distMeters, northDeg, tilt, duration);
}

void GlobeEngine::FlyToRegion(double minLat, double minLon, double maxLat, double maxLon, double duration, double padding) {
  // Fly to center of region
  double centerLat = (minLat + maxLat) / 2.0;
  double centerLon = (minLon + maxLon) / 2.0;

  // JS parity: Calculate distance from bounding box diagonal
  // Formula: GLOBE_RADIUS * distance2D(ll, ur) * 0.8 / padding
  double latSpanRad = JsDegreeToRadian(std::abs(maxLat - minLat));
  double lonSpanRad = JsDegreeToRadian(std::abs(maxLon - minLon));
  double dist2D = std::sqrt(latSpanRad * latSpanRad + lonSpanRad * lonSpanRad);
  
  double effectivePadding = (std::abs(padding) < 1e-6) ? 1.0 : padding;
  double boundSize = (GLOBE_RADIUS / GLOBE_RADIUS_K) * dist2D * 0.8 / effectivePadding;
  
  // Clamp to valid range
  double altitude = boundSize;
  if (altitude < 1000.0) altitude = 1000.0;
  if (altitude > 35786000.0) altitude = 35786000.0;

  const double north = GetNorthAngle();
  const double tilt = GetPitch();
  FlyToPoint(centerLat, centerLon, altitude, north, tilt, duration);
}

bool GlobeEngine::IsAnimating() const {
  return impl_->animating;
}

void GlobeEngine::CancelAnimation() {
  impl_->animating = false;
  impl_->m_flightController.StopAnimation();
}

LayerManager* GlobeEngine::GetLayerManager() {
  return &impl_->layerManager;
}

void GlobeEngine::AddRasterLayer(const RasterLayerConfig& config) {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  auto& layers = impl_->config.rasterLayers;
  auto it = std::find_if(layers.begin(), layers.end(),
                         [&config](const RasterLayerConfig& l) { return l.id == config.id; });
  if (it != layers.end()) {
    *it = config;
  } else {
    layers.push_back(config);
  }
  // Sort by zIndex for correct draw order
  std::sort(layers.begin(), layers.end(),
            [](const RasterLayerConfig& a, const RasterLayerConfig& b) {
              return a.zIndex < b.zIndex;
            });
}

void GlobeEngine::AddIconMap(const std::string& name, const std::string& imageUrl, const std::string& jsonUrl, std::function<void(bool)> callback) {
  if (impl_->iconMaps.count(name)) {
    if (callback) callback(true);
    return;
  }

  auto& iconMap = impl_->iconMaps[name];
  iconMap.name = name;
  iconMap.imageUrl = imageUrl;
  iconMap.jsonUrl = jsonUrl;

  // Asynchronous loading (simplified for now - using a thread)
  std::thread([this, name, imageUrl, jsonUrl, callback]() {
    earth::IconMap localMap;
    localMap.name = name;
    
    // 1. Fetch JSON
    std::string jsonStr;
    if (!jsonUrl.empty()) {
      jsonStr = impl_->FetchUrl(jsonUrl);
    }
    
    // 2. Fetch Image
    std::string imgData = impl_->FetchUrl(imageUrl);
    
    if (imgData.empty()) {
      if (callback) callback(false);
      return;
    }

    // 3. Decode Image (stbi)
    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(imgData.data()),
        static_cast<int>(imgData.size()), &width, &height, &channels, 4);
    
    if (!pixels) {
      if (callback) callback(false);
      return;
    }

    // 4. Parse JSON
    if (!jsonStr.empty()) {
      Value jsonVal;
      JsonParser parser(jsonStr);
      if (parser.Parse(jsonVal)) {
        // JS Parity: iconMap JSON usually has "frames" or "icons"
        // Let's assume a common format for now
        Value icons = jsonVal.Get("frames");
        if (icons.IsNull()) icons = jsonVal.Get("icons");
        
        if (icons.IsObject()) {
          for (auto const& [iconName, iconData] : icons.object) {
            earth::IconInfo info;
            info.name = iconName;
            Value frame = iconData->Get("frame");
            if (frame.IsObject()) {
              info.x = static_cast<int>(frame.Get("x").AsNumber());
              info.y = static_cast<int>(frame.Get("y").AsNumber());
              info.width = static_cast<int>(frame.Get("w").AsNumber());
              info.height = static_cast<int>(frame.Get("h").AsNumber());
            } else {
              // Direct properties
              info.x = static_cast<int>(iconData->Get("x").AsNumber());
              info.y = static_cast<int>(iconData->Get("y").AsNumber());
              info.width = static_cast<int>(iconData->Get("w").AsNumber());
              info.height = static_cast<int>(iconData->Get("h").AsNumber());
            }
            localMap.icons[iconName] = info;
          }
        }
      }
    }

    // 5. Push to pending queue for main thread upload
    {
      std::lock_guard<std::mutex> lock(impl_->iconMapMutex);
      impl_->pendingIconMaps.push_back({name, pixels, width, height, std::move(localMap), callback});
    }
  }).detach();
}

void GlobeEngine::RemoveRasterLayer(const std::string& layerId) {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  auto& layers = impl_->config.rasterLayers;
  layers.erase(std::remove_if(layers.begin(), layers.end(),
                              [&layerId](const RasterLayerConfig& l) { return l.id == layerId; }),
               layers.end());
}

void GlobeEngine::SetRasterLayerVisibility(const std::string& layerId, bool visible) {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  for (auto& layer : impl_->config.rasterLayers) {
    if (layer.id == layerId) {
      layer.visible = visible;
      break;
    }
  }
}

void GlobeEngine::SetRasterLayerOpacity(const std::string& layerId, float opacity) {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  for (auto& layer : impl_->config.rasterLayers) {
    if (layer.id == layerId) {
      layer.opacity = std::max(0.0f, std::min(1.0f, opacity));
      break;
    }
  }
}

void GlobeEngine::SetRasterLayerZIndex(const std::string& layerId, int zIndex) {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  for (auto& layer : impl_->config.rasterLayers) {
    if (layer.id == layerId) {
      layer.zIndex = zIndex;
      break;
    }
  }
  // Re-sort layers by zIndex
  std::sort(impl_->config.rasterLayers.begin(), impl_->config.rasterLayers.end(),
            [](const RasterLayerConfig& a, const RasterLayerConfig& b) {
              return a.zIndex < b.zIndex;
            });
}

bool GlobeEngine::GetRasterLayerConfigById(const std::string& layerId, RasterLayerConfig& out) const {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  for (const auto& layer : impl_->config.rasterLayers) {
    if (layer.id == layerId) {
      out = layer;
      return true;
    }
  }
  return false;
}

bool GlobeEngine::GetRasterLayerConfigByIndex(size_t index, RasterLayerConfig& out) const {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  if (index >= impl_->config.rasterLayers.size()) {
    return false;
  }
  out = impl_->config.rasterLayers[index];
  return true;
}

std::vector<std::string> GlobeEngine::GetRasterLayerIds() const {
  std::lock_guard<std::mutex> lock(impl_->configMutex);
  std::vector<std::string> ids;
  ids.reserve(impl_->config.rasterLayers.size());
  for (const auto& layer : impl_->config.rasterLayers) {
    ids.push_back(layer.id);
  }
  return ids;
}

bool GlobeEngine::ScreenToGeo(int screenX, int screenY, double& outLat, double& outLon) {
  if (!impl_) return false;
  impl_->UpdateCameraDerived();

  glm::dvec3 rayOrigin;
  glm::dvec3 rayDir;
  impl_->m_newCamera.GetRay(static_cast<double>(screenX), static_cast<double>(screenY),
                            impl_->config.windowWidth, impl_->config.windowHeight,
                            rayOrigin, rayDir);

  const double a = glm::dot(rayDir, rayDir);
  const double b = 2.0 * glm::dot(rayOrigin, rayDir);
  const double c = glm::dot(rayOrigin, rayOrigin) - static_cast<double>(GLOBE_RADIUS) * GLOBE_RADIUS;

  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0.0) {
    return false;
  }

  const double sqrtD = std::sqrt(discriminant);
  const double t1 = (-b - sqrtD) / (2.0 * a);
  const double t2 = (-b + sqrtD) / (2.0 * a);
  const double t = (t1 > 0.0) ? t1 : t2;
  if (t < 0.0) {
    return false;
  }

  const glm::dvec3 hitPoint = rayOrigin + rayDir * t;
  const glm::dvec3 normalizedHit = hitPoint / static_cast<double>(GLOBE_RADIUS);

  outLat = glm::degrees(std::asin(normalizedHit.z));
  outLon = glm::degrees(std::atan2(normalizedHit.y, normalizedHit.x));

  return true;
}

bool GlobeEngine::GeoToScreen(double lat, double lon, int& outX, int& outY) {
  // Convert lat/lon to 3D point on sphere (scaled by GLOBE_RADIUS)
  double latRad = glm::radians(lat);
  double lonRad = glm::radians(lon);
  glm::vec3 worldPos;
  worldPos.x = static_cast<float>(std::cos(latRad) * std::cos(lonRad) * GLOBE_RADIUS);
  worldPos.y = static_cast<float>(std::cos(latRad) * std::sin(lonRad) * GLOBE_RADIUS);
  worldPos.z = static_cast<float>(std::sin(latRad) * GLOBE_RADIUS);
  
  // Get current view/projection matrices
  glm::mat4 proj = glm::perspective(glm::radians(GLOBE_FOV),
                                    static_cast<float>(impl_->config.windowWidth) / impl_->config.windowHeight,
                                    0.1f * GLOBE_RADIUS, 100.0f * GLOBE_RADIUS);
  glm::mat4 view = impl_->GetViewMatrix();
  glm::mat4 mvp = proj * view;
  
  // Project to clip space
  glm::vec4 clipPos = mvp * glm::vec4(worldPos, 1.0f);
  
  // Check if behind camera
  if (clipPos.w <= 0.0f) {
    return false;
  }
  
  // Perspective divide
  glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
  
  // Check if outside view frustum
  if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f) {
    return false;
  }
  
  // Convert NDC to screen coordinates
  outX = static_cast<int>((ndc.x + 1.0f) * 0.5f * impl_->config.windowWidth);
  outY = static_cast<int>((1.0f - ndc.y) * 0.5f * impl_->config.windowHeight);
  
  return true;
}

std::vector<Feature*> GlobeEngine::QueryFeaturesAtScreen(int screenX, int screenY, double tolerancePx) {
  std::vector<Feature*> results;
  
  // Convert screen point to geo
  double lat, lon;
  if (!ScreenToGeo(screenX, screenY, lat, lon)) {
    return results;
  }
  
  // Calculate tolerance in degrees (approximate)
  // At current zoom level, estimate degrees per pixel
  double degPerPx = 360.0 / (256.0 * (1 << impl_->currentZoom));
  double toleranceDeg = tolerancePx * degPerPx;
  
  // Query layers
  auto features = impl_->layerManager.QueryByPoint(lon, lat, toleranceDeg);
  for (auto* f : features) {
    results.push_back(f);
  }
  
  return results;
}

bool GlobeEngine::SampleTerrainHeightMeters(double lon, double lat, int lod, double& outHeight) const {
  if (!impl_) return false;
  const auto& config = impl_->config;
  if (!config.demEnabled || config.demBaseUrl.empty()) {
    return false;
  }
  int meshN = std::max(2, config.demMeshN);
  
  // Phase 1 Parity: WGS84 now uses Mercator indexing, so we don't check spanLon/Lat here anymore.
  // The service expects requests aligned with Mercator tiles.

  // Use requested LOD for mesh level
  int meshLevel = lod;
  // Clamp to valid range (though LOD logic should already handle this)
  if (meshLevel < 0) meshLevel = 0;
  
  // Re-apply WGS84 Max LOD clamp to prevent requests beyond service capability
  if (config.meshType == MeshType::WGS84) {
      int maxLod = std::max(0, config.wgs84MaxLOD);
      if (meshLevel > maxLod) meshLevel = maxLod;
  }
  
  // Phase 1 Parity: Use GeoToTileXY for BOTH WGS84 and XYZ_MERCATOR
  // This ensures we request the same tile index that corresponds to the raster tile
  // which implies the service expects bounds derived from that tile.
  auto [tx, ty] = GeoToTileXY(lat, lon, meshLevel);
  int n = 1 << meshLevel;
  int tileX = WrapTileX(tx, n);
  int tileY = ClampTileY(ty, n);

  std::string key = std::to_string(tileX) + ":" + std::to_string(tileY) + ":" +
                    std::to_string(meshLevel);

  auto& dem = impl_->dem;
  
  // 1. Try to find the EXACT tile
  {
      std::lock_guard<std::mutex> lock(impl_->demMutex);
      const size_t perTile = static_cast<size_t>(meshN * meshN);
      auto it = dem.tiles.find(key);
      if (it != dem.tiles.end() && it->second.grid.size() >= perTile) {
        dem.lru.erase(it->second.lruIt);
        dem.lru.push_front(key);
        it->second.lruIt = dem.lru.begin();
        const auto& tile = it->second;
        
        // Normalize U/V relative to the specific tile bounds
        double u = (lon - tile.llx) / (tile.urx - tile.llx);
        double v = (lat - tile.lly) / (tile.ury - tile.lly);
        
        if (config.demRowsNorthToSouth) {
          v = 1.0 - v;
        }
        outHeight = SampleDemBilinearStitched(tile.grid, meshN, u, v, 
                                               tile.boundaries, tile.boundariesValid,
                                               tile.childBoundariesValid,
                                               config.demRowsNorthToSouth);
        if (outHeight > 1.0 && config.demDebug) fprintf(stderr, "DEM Sample: Hit! h=%.2f at %s\n", outHeight, key.c_str());
        return true;
      }
  }
  
  // 2. Queue the request for the EXACT tile (if not found or invalid)
  auto floorDiv = [](int value, int divisor) {
    return static_cast<int>(std::floor(static_cast<double>(value) / divisor));
  };
  
  int batchGrid = std::max(1, config.demBatchGrid);
  int baseX = floorDiv(tileX, batchGrid) * batchGrid;
  int baseY = floorDiv(tileY, batchGrid) * batchGrid;

  std::vector<DemCell> cells;
  cells.reserve(static_cast<size_t>(batchGrid * batchGrid));
  for (int bx = 0; bx < batchGrid; ++bx) {
    for (int by = 0; by < batchGrid; ++by) {
      int cx = baseX + bx;
      int cy = baseY + by;
      DemCell cell;
      cell.tileX = cx;
      cell.tileY = cy;
      cell.level = meshLevel;
      
      // Phase 1 Parity: Use TileToBBox4326 for bounds calculation for BOTH types
      // This is the key fix: WGS84 mode now sends the bounds of the MERCATOR tile
      // instead of a fixed degree span grid.
      double minLon, minLat, maxLon, maxLat;
      TileToBBox4326(meshLevel, cx, cy, minLon, minLat, maxLon, maxLat);
      cell.llx = minLon;
      cell.lly = minLat;
      cell.urx = maxLon;
      cell.ury = maxLat;
      
      cells.push_back(cell);
    }
  }
  // Queue logic (same as before)
  {
      auto flushPending = [&](int mN, int mL) {
        if (impl_->pendingDemBatches.size() >= kMaxPendingDemBatches) {
          return;
        }
        const size_t maxCount = static_cast<size_t>(std::max(1, config.demBatchMaxCount));
        while (!impl_->demPendingCells.empty()) {
          size_t count = std::min(impl_->demPendingCells.size(), maxCount);
          std::vector<DemCell> batch;
          batch.reserve(count);
          for (size_t i = 0; i < count; ++i) {
            const DemCell cell = impl_->demPendingCells[i];
            batch.push_back(cell);
            std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                               std::to_string(cell.level);
            impl_->pendingDemCells.erase(ckey);
          }
          const auto eraseCount =
              static_cast<std::vector<DemCell>::difference_type>(count);
          impl_->demPendingCells.erase(impl_->demPendingCells.begin(),
                                       impl_->demPendingCells.begin() + eraseCount);
          DemJob job;
          job.meshN = mN;
          job.batchGrid = 1;
          job.retryCount = 0;
          job.cells = std::move(batch);
          job.batchKey = std::to_string(mL) + ":" + std::to_string(mN) + ":" +
                         std::to_string(impl_->demBatchCounter++);
          job.url = BuildDemBatchUrl(impl_->NextMeshUrl(), config.meshType, mN, job.cells, config.demDebug);
          impl_->pendingDemBatches.insert(job.batchKey);
          impl_->demQueue.push(std::move(job));
          impl_->demCv.notify_one();
        }
        impl_->demBatchStartTime = 0.0;
      };

      if (!impl_->demPendingCells.empty() &&
          (impl_->demPendingMeshLevel != meshLevel || impl_->demPendingMeshN != meshN)) {
        flushPending(impl_->demPendingMeshN, impl_->demPendingMeshLevel);
      }

      if (impl_->demPendingCells.empty()) {
        impl_->demBatchStartTime = glfwGetTime();
        impl_->demPendingMeshLevel = meshLevel;
        impl_->demPendingMeshN = meshN;
      }

      for (const auto& cell : cells) {
        std::string ckey = std::to_string(cell.tileX) + ":" + std::to_string(cell.tileY) + ":" +
                           std::to_string(cell.level);
        if (impl_->pendingDemCells.insert(ckey).second) {
          impl_->demPendingCells.push_back(cell);
        }
      }

      const double now = glfwGetTime();
      const double waitSec = std::max(0.0, config.demBatchMaxWaitSec);
      if (impl_->demPendingCells.size() >= static_cast<size_t>(std::max(1, config.demBatchMaxCount)) ||
          (waitSec > 0.0 && (now - impl_->demBatchStartTime) >= waitSec)) {
        flushPending(meshN, meshLevel);
      }
  }
  
  // 3. FALLBACK: Try ancestors (Parent Child Relationship)
  // If we can't find the exact tile, try to sample from a parent tile
  // This allows us to show *some* terrain shape while the high-res one loads
  // Phase 1 Parity: WGS84 now uses Mercator indexing, so ancestor fallback works for both types
  if (meshLevel > 0) {
      std::lock_guard<std::mutex> lock(impl_->demMutex);
      int currZ = meshLevel - 1;
      int currX = tileX >> 1;
      int currY = tileY >> 1;
      
      while (currZ >= 0) {
          std::string ancestorKey = std::to_string(currX) + ":" + std::to_string(currY) + ":" + std::to_string(currZ);
          auto it = dem.tiles.find(ancestorKey);
          if (it != dem.tiles.end() && it->second.grid.size() >= static_cast<size_t>(meshN * meshN)) {
              const auto& tile = it->second;
              // Sample from ancestor
              double u = (lon - tile.llx) / (tile.urx - tile.llx);
              double v = (lat - tile.lly) / (tile.ury - tile.lly);
              if (config.demRowsNorthToSouth) {
                  v = 1.0 - v;
              }
              // We found an ancestor! Use it.
              outHeight = SampleDemBilinearStitched(tile.grid, meshN, u, v,
                                                     tile.boundaries, tile.boundariesValid,
                                                     tile.childBoundariesValid,
                                                     config.demRowsNorthToSouth);
              return true; // Use fallback
          }
          currZ--;
          currX >>= 1;
          currY >>= 1;
      }
  }

  return false;
}

// ============================================================================
// FAZ 0: PARITY SNAPSHOT API - JS↔C++ karşılaştırma için
// ============================================================================

GlobeEngine::ParitySnapshot GlobeEngine::GetParitySnapshot() const {
  ParitySnapshot snap = {};
  if (!impl_) return snap;
  
  // Altitude ve LOD
  snap.altitude = impl_->altitudeWorld / GLOBE_RADIUS_K;  // meters
  // FAZ 1: SA_TABLE tabanlı LOD hesapla ve clamp et
  int minZoom = impl_->config.minZoom;
  int maxZoom = impl_->config.useFixedZoom ? impl_->config.fixedZoom : impl_->config.maxZoom;
  snap.lodExact = FindLodFromAltitudeMeters(snap.altitude, minZoom, maxZoom);
  snap.currentZoom = static_cast<int>(std::round(snap.lodExact));
  
  // Navigation limits
  snap.navMinLOD = impl_->navMinLOD;
  snap.navMaxLOD = impl_->navMaxLOD;
  snap.navMinDist = impl_->navMinDist;
  snap.navMaxDist = impl_->navMaxDist;
  
  // Mesh settings (JS: MESHN=5)
  snap.meshN = impl_->config.demMeshN;
  
  // Cell creation limits (JS: MaxCellCanBeCreated dinamik)
  // FAZ 1: Dinamik değer kullan
  snap.maxCellCanBeCreated = impl_->GetDynamicMaxCellCanBeCreated();
  snap.cellDivisionCount = impl_->cellDivisionCount;
  
  // Camera position
  snap.centerLat = impl_->centerLat;
  snap.centerLon = impl_->centerLon;
  snap.tiltDeg = impl_->camera.tiltDeg;
  snap.northDeg = JsRadianToDegree(impl_->camera.ea.x);
  snap.cameraDist = impl_->camera.dist;
  
  // Screen
  snap.screenWidth = impl_->config.windowWidth;
  snap.screenHeight = impl_->config.windowHeight;
  snap.fps = impl_->fpsValue;
  
  // State
  snap.isAnimating = impl_->animating;
  snap.isScreenMoving = impl_->dragging || impl_->wheelZoomActive || 
                        impl_->dblClickActive || impl_->midTurnActive;
  snap.is2DMode = impl_->is2D;
  snap.clampCount = impl_->clampCount;
  snap.lastClampMin = impl_->lastClampMin;
  snap.lastClampMax = impl_->lastClampMax;
  
  // Telemetry (Phase 0)
  snap.frameTimeMs = impl_->fpsValue > 0.0 ? 1000.0 / impl_->fpsValue : 0.0;
  
  // Calculate tile cache stats
  snap.tileCacheCount = impl_->tiles.size();
  for (const auto& kv : impl_->layerTiles) {
    snap.tileCacheCount += kv.second.tiles.size();
  }
  snap.tileCacheSize = GetTextureCacheSize();
  
  // Calculate tile load metrics from network log
  double totalLoadTime = 0.0;
  double maxLoadTime = 0.0;
  int loadCount = 0;
  
  std::lock_guard<std::mutex> lock(impl_->networkLogMutex);
  for (const auto& entry : impl_->networkLog) {
    if (entry.status == NetRequestStatus::Success && entry.endTime > entry.startTime) {
      double durationMs = (entry.endTime - entry.startTime) * 1000.0;
      totalLoadTime += durationMs;
      if (durationMs > maxLoadTime) maxLoadTime = durationMs;
      loadCount++;
    }
  }
  
  snap.avgTileLoadTimeMs = loadCount > 0 ? totalLoadTime / loadCount : 0.0;
  snap.maxTileLoadTimeMs = maxLoadTime;
  
  return snap;
}

std::string GlobeEngine::DumpParitySnapshot() const {
  ParitySnapshot snap = GetParitySnapshot();
  
  std::ostringstream json;
  json << std::fixed << std::setprecision(6);
  json << "{\n";
  json << "  \"altitude\": " << snap.altitude << ",\n";
  json << "  \"lodExact\": " << snap.lodExact << ",\n";
  json << "  \"currentZoom\": " << snap.currentZoom << ",\n";
  json << "  \"navMinLOD\": " << snap.navMinLOD << ",\n";
  json << "  \"navMaxLOD\": " << snap.navMaxLOD << ",\n";
  json << "  \"navMinDist\": " << snap.navMinDist << ",\n";
  json << "  \"navMaxDist\": " << snap.navMaxDist << ",\n";
  json << "  \"meshN\": " << snap.meshN << ",\n";
  json << "  \"maxCellCanBeCreated\": " << snap.maxCellCanBeCreated << ",\n";
  json << "  \"cellDivisionCount\": " << snap.cellDivisionCount << ",\n";
  json << "  \"centerLat\": " << snap.centerLat << ",\n";
  json << "  \"centerLon\": " << snap.centerLon << ",\n";
  json << "  \"tiltDeg\": " << snap.tiltDeg << ",\n";
  json << "  \"northDeg\": " << snap.northDeg << ",\n";
  json << "  \"cameraDist\": " << snap.cameraDist << ",\n";
  json << "  \"screenWidth\": " << snap.screenWidth << ",\n";
  json << "  \"screenHeight\": " << snap.screenHeight << ",\n";
  json << "  \"fps\": " << snap.fps << ",\n";
  json << "  \"isAnimating\": " << (snap.isAnimating ? "true" : "false") << ",\n";
  json << "  \"isScreenMoving\": " << (snap.isScreenMoving ? "true" : "false") << ",\n";
  json << "  \"is2DMode\": " << (snap.is2DMode ? "true" : "false") << ",\n";
  json << "  \"clampCount\": " << snap.clampCount << ",\n";
  json << "  \"lastClampMin\": " << snap.lastClampMin << ",\n";
  json << "  \"lastClampMax\": " << snap.lastClampMax << ",\n";
  json << "  \"frameTimeMs\": " << snap.frameTimeMs << ",\n";
  json << "  \"avgTileLoadTimeMs\": " << snap.avgTileLoadTimeMs << ",\n";
  json << "  \"maxTileLoadTimeMs\": " << snap.maxTileLoadTimeMs << ",\n";
  json << "  \"tileCacheCount\": " << snap.tileCacheCount << ",\n";
  json << "  \"tileCacheSize\": " << snap.tileCacheSize << "\n";
  json << "}";
  
  return json.str();
}

// ============================================================================
// Google Earth Style APIs - Async Elevation Query & Cache Pin/Unpin
// ============================================================================

void GlobeEngine::QueryElevationAsync(double lat, double lon, ElevationCallback callback) {
  if (!impl_ || !callback) return;
  double height = 0.0;
  int lod = impl_->currentZoom >= 0 ? impl_->currentZoom : impl_->config.minZoom;
  bool success = SampleTerrainHeightMeters(lon, lat, lod, height);
  
  if (success) {
    callback(true, height);
  } else {
    // Queue for later retry (Phase 4)
    Impl::PendingElevationQuery query;
    query.lat = lat;
    query.lon = lon;
    query.lod = lod;
    query.callback = callback;
    query.timestamp = glfwGetTime();
    impl_->pendingElevationQueries.push_back(query);
  }
}

double GlobeEngine::GetAbsoluteAltitude(double lat, double lon, double altitude, AltitudeMode mode) const {
  if (!impl_) return altitude;
  int lod = impl_->currentZoom >= 0 ? impl_->currentZoom : impl_->config.minZoom;
  switch (mode) {
    case AltitudeMode::ABSOLUTE:
      return altitude;
    case AltitudeMode::RELATIVE_TO_GROUND: {
      double terrainHeight = 0.0;
      if (SampleTerrainHeightMeters(lon, lat, lod, terrainHeight)) {
        return terrainHeight + altitude;
      }
      return altitude;
    }
    case AltitudeMode::CLAMP_TO_GROUND: {
      double terrainHeight = 0.0;
      if (SampleTerrainHeightMeters(lon, lat, lod, terrainHeight)) {
        return terrainHeight;
      }
      return 0.0;
    }
  }
  return altitude;
}

void GlobeEngine::PinTile(int z, int x, int y) {
  if (!impl_) return;
  std::string key = MakeTileKey(z, x, y);
  auto it = impl_->tiles.find(key);
  if (it != impl_->tiles.end()) {
    it->second.pinned = true;
  }
}

void GlobeEngine::UnpinTile(int z, int x, int y) {
  if (!impl_) return;
  std::string key = MakeTileKey(z, x, y);
  auto it = impl_->tiles.find(key);
  if (it != impl_->tiles.end()) {
    it->second.pinned = false;
  }
}

void GlobeEngine::PinTilesInRegion(double minLat, double minLon, double maxLat, double maxLon, int zoom) {
  if (!impl_) return;
  int minX = static_cast<int>(std::floor((minLon + 180.0) / 360.0 * (1 << zoom)));
  int maxX = static_cast<int>(std::floor((maxLon + 180.0) / 360.0 * (1 << zoom)));
  auto latToTileY = [zoom](double lat) {
    double latRad = lat * M_PI / 180.0;
    return static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * (1 << zoom)));
  };
  int minY = latToTileY(maxLat);
  int maxY = latToTileY(minLat);
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      PinTile(zoom, x, y);
    }
  }
}

void GlobeEngine::UnpinAllTiles() {
  if (!impl_) return;
  for (auto& [key, tile] : impl_->tiles) {
    tile.pinned = false;
  }
}

size_t GlobeEngine::GetPinnedTileCount() const {
  if (!impl_) return 0;
  size_t count = 0;
  for (const auto& [key, tile] : impl_->tiles) {
    if (tile.pinned) {
      count++;
    }
  }
  return count;
}

namespace {
std::vector<unsigned char> DownloadData(const std::string& url) {
    std::vector<unsigned char> buffer;
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            auto* buf = static_cast<std::vector<unsigned char>*>(userp);
            size_t realsize = size * nmemb;
            buf->insert(buf->end(), (unsigned char*)contents, (unsigned char*)contents + realsize);
            return realsize;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "NativeGlobe/1.0");
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return buffer;
}
}

namespace {
void CleanupOverlay(GlobeEngine::ImageOverlay& ov) {
    if (ov.textureId) glDeleteTextures(1, &ov.textureId);
    if (ov.vao) glDeleteVertexArrays(1, &ov.vao);
    if (ov.vbo) glDeleteBuffers(1, &ov.vbo);
    if (ov.ebo) glDeleteBuffers(1, &ov.ebo);
    ov.textureId = ov.vao = ov.vbo = ov.ebo = 0;
}
}

void GlobeEngine::AddImageOverlay(int id, const std::string& url, double minLon, double minLat, double maxLon, double maxLat, float opacity, float rotation) {
  {
      std::lock_guard<std::mutex> lock(impl_->overlayMutex);
      auto it = std::remove_if(impl_->imageOverlays.begin(), impl_->imageOverlays.end(), 
          [id](ImageOverlay& o) { 
              if (o.id == id) { CleanupOverlay(o); return true; }
              return false; 
          });
      if (it != impl_->imageOverlays.end()) {
          impl_->imageOverlays.erase(it, impl_->imageOverlays.end());
      }
      
      ImageOverlay ov;
      ov.id = id;
      ov.url = url;
      ov.minLat = minLat; ov.minLon = minLon;
      ov.maxLat = maxLat; ov.maxLon = maxLon;
      ov.opacity = opacity;
      ov.rotation = rotation;
      impl_->imageOverlays.push_back(ov);
  }
  
  std::thread([this, id, url]() {
      std::vector<unsigned char> buffer = DownloadData(url);
      if (buffer.empty()) return;
      
      int w, h, c;
      unsigned char* img = stbi_load_from_memory(buffer.data(), (int)buffer.size(), &w, &h, &c, 4);
      if (!img) return;
      
      std::lock_guard<std::mutex> lock(impl_->taskMutex);
      impl_->mainThreadTasks.push_back([this, id, img, w, h]() {
          std::lock_guard<std::mutex> lockOv(impl_->overlayMutex);
          for (auto& ov : impl_->imageOverlays) {
              if (ov.id == id) {
                  glGenTextures(1, &ov.textureId);
                  glBindTexture(GL_TEXTURE_2D, ov.textureId);
                  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
                  ov.width = w; ov.height = h;
                  ov.loaded = true;
                  break;
              }
          }
          stbi_image_free(img);
      });
  }).detach();
}

void GlobeEngine::DeleteImageOverlay(int id) {
    std::lock_guard<std::mutex> lock(impl_->overlayMutex);
    auto it = std::remove_if(impl_->imageOverlays.begin(), impl_->imageOverlays.end(), 
        [id](ImageOverlay& o) { 
            if (o.id == id) { CleanupOverlay(o); return true; }
            return false; 
        });
    impl_->imageOverlays.erase(it, impl_->imageOverlays.end());
}

void GlobeEngine::DeleteAllImageOverlays() {
    std::lock_guard<std::mutex> lock(impl_->overlayMutex);
    for (auto& ov : impl_->imageOverlays) {
        CleanupOverlay(ov);
    }
    impl_->imageOverlays.clear();
}

void GlobeEngine::SetImageOverlayColor(int id, const glm::vec4& color, float opacity) {
    std::lock_guard<std::mutex> lock(impl_->overlayMutex);
    for (auto& ov : impl_->imageOverlays) {
        if (ov.id == id) {
            ov.color = color;
            ov.opacity = opacity;
            break;
        }
    }
}

void GlobeEngine::ChangeImageOverlayURL(int id, const std::string& url) {
    double minLon, minLat, maxLon, maxLat;
    float opacity, rotation;
    glm::vec4 color = glm::vec4(1.0f);
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(impl_->overlayMutex);
        for (const auto& ov : impl_->imageOverlays) {
            if (ov.id == id) {
                minLon = ov.minLon; minLat = ov.minLat;
                maxLon = ov.maxLon; maxLat = ov.maxLat;
                opacity = ov.opacity; rotation = ov.rotation;
                color = ov.color;
                found = true;
                break;
            }
        }
    }
    if (found) {
        AddImageOverlay(id, url, minLon, minLat, maxLon, maxLat, opacity, rotation);
        SetImageOverlayColor(id, color, opacity);
    }
}

void GlobeEngine::Impl::RenderImageOverlays(const glm::mat4& mvp) {
    if (imageOverlays.empty()) return;
    
    std::lock_guard<std::mutex> lock(overlayMutex);
    
    if (!overlayProgram) {
        const char* vs = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aUV;
            uniform mat4 uMVP;
            uniform float uScale;
            out vec2 vUV;
            void main() {
                gl_Position = uMVP * vec4(aPos * uScale, 1.0);
                vUV = aUV;
            }
        )";
        const char* fs = R"(
            #version 330 core
            in vec2 vUV;
            uniform sampler2D uTex;
            uniform float uOpacity;
            uniform vec4 uColor;
            out vec4 FragColor;
            void main() {
                vec4 texColor = texture(uTex, vUV);
                FragColor = texColor * uColor * vec4(1,1,1, uOpacity);
            }
        )";
        overlayProgram = CreateProgram(vs, fs);
        overlayMvpLoc = glGetUniformLocation(overlayProgram, "uMVP");
        overlayScaleLoc = glGetUniformLocation(overlayProgram, "uScale");
        overlayTexLoc = glGetUniformLocation(overlayProgram, "uTex");
        overlayOpacityLoc = glGetUniformLocation(overlayProgram, "uOpacity");
        overlayColorLoc = glGetUniformLocation(overlayProgram, "uColor");
    }
    
    glUseProgram(overlayProgram);
    glUniformMatrix4fv(overlayMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(overlayScaleLoc, GLOBE_RADIUS);
    glUniform1i(overlayTexLoc, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    for (auto& ov : imageOverlays) {
        if (!ov.loaded || !ov.textureId) continue;
        
        if (ov.vao == 0) {
            std::vector<glm::vec3> verts;
            std::vector<glm::vec2> uvs;
            std::vector<uint16_t> indices;
            int gridN = 10;
            
            float rad = glm::radians(ov.rotation);
            float cosR = std::cos(rad);
            float sinR = std::sin(rad);
            double midLat = (ov.minLat + ov.maxLat) * 0.5;
            double midLon = (ov.minLon + ov.maxLon) * 0.5;
            
            for (int j = 0; j <= gridN; ++j) {
                float v = (float)j / gridN;
                for (int i = 0; i <= gridN; ++i) {
                    float u = (float)i / gridN;
                    
                    double dLon = (u - 0.5) * (ov.maxLon - ov.minLon);
                    double dLat = (v - 0.5) * (ov.maxLat - ov.minLat);
                    
                    // Simple rotation around center (Lat/Lon approximation)
                    double rotLon = dLon * cosR - dLat * sinR;
                    double rotLat = dLon * sinR + dLat * cosR;
                    
                    double lat = midLat + rotLat;
                    double lon = midLon + rotLon;
                    
                    double latRad = glm::radians(lat);
                    double lonRad = glm::radians(lon);
                    glm::vec3 p;
                    p.x = std::cos(latRad) * std::cos(lonRad);
                    p.y = std::cos(latRad) * std::sin(lonRad);
                    p.z = std::sin(latRad);
                    verts.push_back(p);
                    uvs.push_back(glm::vec2(u, 1.0f - v));
                }
            }
            for (int j = 0; j < gridN; ++j) {
                for (int i = 0; i < gridN; ++i) {
                    int p00 = j * (gridN + 1) + i;
                    int p10 = p00 + 1;
                    int p01 = (j + 1) * (gridN + 1) + i;
                    int p11 = p01 + 1;
                    indices.push_back(p00); indices.push_back(p10); indices.push_back(p01);
                    indices.push_back(p10); indices.push_back(p11); indices.push_back(p01);
                }
            }
            glGenVertexArrays(1, &ov.vao);
            glGenBuffers(1, &ov.vbo);
            glGenBuffers(1, &ov.ebo);
            glBindVertexArray(ov.vao);
            glBindBuffer(GL_ARRAY_BUFFER, ov.vbo);
            struct Vertex { glm::vec3 p; glm::vec2 uv; };
            std::vector<Vertex> vdata;
            for(size_t k=0; k<verts.size(); ++k) vdata.push_back({verts[k], uvs[k]});
            glBufferData(GL_ARRAY_BUFFER, vdata.size() * sizeof(Vertex), vdata.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ov.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)sizeof(glm::vec3));
            ov.vertexCount = indices.size();
        }
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ov.textureId);
        glUniform1f(overlayOpacityLoc, ov.opacity);
        glUniform4fv(overlayColorLoc, 1, glm::value_ptr(ov.color));
        glBindVertexArray(ov.vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)ov.vertexCount, GL_UNSIGNED_SHORT, 0);
    }
}

namespace {
glm::dvec3 LatLonAltToECEF(double lat, double lon, double alt) {
    double lat_rad = glm::radians(lat);
    double lon_rad = glm::radians(lon);
    double R = 6378.137;
    double x = (R + alt * 0.001) * std::cos(lat_rad) * std::cos(lon_rad);
    double y = (R + alt * 0.001) * std::cos(lat_rad) * std::sin(lon_rad);
    double z = (R + alt * 0.001) * std::sin(lat_rad);
    return glm::dvec3(x, y, z);
}
}

GlobeEngine::LineOfSightResult GlobeEngine::CheckLineOfSight(double lat1, double lon1, double alt1, double lat2, double lon2, double alt2) {
    LineOfSightResult result;
    result.visible = true;
    
    glm::dvec3 p1 = LatLonAltToECEF(lat1, lon1, alt1);
    glm::dvec3 p2 = LatLonAltToECEF(lat2, lon2, alt2);
    
    double dist = glm::length(p2 - p1);
    int steps = std::max(20, static_cast<int>(dist / (100.0 * GLOBE_RADIUS_K))); 
    if (steps > 500) steps = 500;
    
    for (int i = 1; i < steps; ++i) {
        double t = (double)i / steps;
        glm::dvec3 p = glm::mix(p1, p2, t);
        
        double r = glm::length(p);
        double lat = glm::degrees(std::asin(p.z / r));
        double lon = glm::degrees(std::atan2(p.y, p.x));
        
        double terrainH = 0.0;
        SampleTerrainHeightMeters(lon, lat, 14, terrainH);
        
        double terrainR = GLOBE_RADIUS + terrainH * GLOBE_RADIUS_K;
        
        if (r < terrainR) {
            result.visible = false;
            result.hitLat = lat;
            result.hitLon = lon;
            result.hitAlt = terrainH;
            return result;
        }
    }
    return result;
}

std::vector<GlobeEngine::ProfileSample> GlobeEngine::GetElevationProfile(double lat1, double lon1, double lat2, double lon2, int samples, int lod) {
    std::vector<ProfileSample> result;
    
    glm::dvec3 n1 = LatLonAltToECEF(lat1, lon1, 0.0);
    glm::dvec3 n2 = LatLonAltToECEF(lat2, lon2, 0.0);
    n1 = glm::normalize(n1);
    n2 = glm::normalize(n2);
    
    double dot = glm::clamp(glm::dot(n1, n2), -1.0, 1.0);
    double theta = std::acos(dot);
    double sinTheta = std::sin(theta);
    
    for (int i = 0; i <= samples; ++i) {
        double t = (double)i / samples;
        glm::dvec3 n;
        if (std::abs(sinTheta) < 1e-6) {
            n = glm::mix(n1, n2, t);
        } else {
            double w1 = std::sin((1.0 - t) * theta) / sinTheta;
            double w2 = std::sin(t * theta) / sinTheta;
            n = w1 * n1 + w2 * n2;
        }
        n = glm::normalize(n);
        
        double sLat = glm::degrees(std::asin(n.z));
        double sLon = glm::degrees(std::atan2(n.y, n.x));
        
        double h = 0.0;
        SampleTerrainHeightMeters(sLon, sLat, lod, h);
        
        double d = 6378137.0 * theta * t;
        
        result.push_back({sLat, sLon, d, h});
    }
    return result;
}
