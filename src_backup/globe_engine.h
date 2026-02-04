#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "earth_camera.h"
#include "globe_config.h"

class LayerManager;
struct Feature;




// main.js constants for full parity
constexpr float GLOBE_RADIUS_K = 0.001f;                    // Scale factor
constexpr float GLOBE_RADIUS = 6378137.0f * GLOBE_RADIUS_K; // 6378.137 normalized
constexpr float GLOBE_START_DIST_YATAY = 1.1f * GLOBE_RADIUS + GLOBE_RADIUS;
constexpr float GLOBE_MIN_TILTANGLE = 0.05f;
constexpr float GLOBE_MAX_TILTANGLE = 80.0f;  // JS parity: uo.GLOBE_MAX_TILTANGLE = 80
constexpr float GLOBE_FOV = 50.0f;                          // Field of view in degrees

// JS parity: Default min/max dist (can be changed by Navigation API)
constexpr float GLOBE_DEFAULT_MIN_DIST = 10.0f * GLOBE_RADIUS_K;  // 0.01
constexpr float GLOBE_DEFAULT_MAX_DIST = 4.0f * GLOBE_RADIUS;     // 4R
constexpr int GLOBE_DEFAULT_MIN_LOD = 2;
constexpr int GLOBE_DEFAULT_MAX_LOD = 22;
constexpr int GLOBE_MIN_CELL_LEVEL = 19;  // JS: nav zoom limit

// P2: Cell/Tile selection limits - JS parity
constexpr int GLOBE_MAX_CELL_CREATE = 100;    // JS: default max cells to create per frame
constexpr int GLOBE_MAX_CELL_MOUSEDOWN = 5;   // JS: reduced during mouse interaction
constexpr int GLOBE_MAX_CELL_ZOOMING = 2;     // JS: reduced during zoom animation

// P4: FlyTo animation constants - JS parity (globe_constants.js:97-100)
constexpr double GLOBE_FLYTO_NORTH_DIV = 10.0;   // JS: GLOBE_FLYTO_NOTRH_DIV
constexpr double GLOBE_FLYTO_TURN_DIV = 600.0;   // JS: GLOBE_FLYTO_TURN_DIV
constexpr double GLOBE_FLYTO_TILT3D2D = 1.5;     // JS: GLOBE_FLYTO_TILT3D2D

// P3: Mesh interval constants - JS parity
constexpr int LAYER_CHECKMESH_INTERVAL_MS = 2000;  // JS: mesh check interval
constexpr int CAMERA_POS_CHANGED_INTERVAL_MS = 2000;



// Backward compatibility aliases
constexpr float GLOBE_MIN_DIST = GLOBE_DEFAULT_MIN_DIST;
constexpr float GLOBE_MAX_DIST = GLOBE_DEFAULT_MAX_DIST;

// JS navigation constants
constexpr double NAVIGATION_SPEED_DEFAULT = 1.2;  // JS parity: default nav speed
constexpr double ZOOM_DRAG_SPEED = 80.0 * 1.5;              // JS: 80 * NAVIGATION_SPEED * 1.5
constexpr double WHEEL_ZOOM_FACTOR = 0.15;                   // JS wheel zoom step
constexpr double INERTIA_DECAY = 0.92;                       // JS inertia decay rate
constexpr double INERTIA_THRESHOLD = 0.001;                  // Stop inertia below this
constexpr double DOUBLE_CLICK_TIME = 0.3;                    // seconds
constexpr double ARROW_KEY_SPEED_DEFAULT = 100.0;            // pixels per second



class GlobeEngine {
public:
  GlobeEngine();
  ~GlobeEngine();

  bool Init(const GlobeConfig& config);
  void Run();
  bool RunLodTest();  // Automated LOD test mode
  bool RunDemTest();  // Automated DEM/mesh height test mode
  bool Run2DClampTest();  // Automated 2D clamp test mode
  bool RunParityTest();   // Automated Parity Snapshot test (Phase 0)
  void Shutdown();

  void SetDistance(float distance);
  void SetPitch(float degrees);
  void SetYaw(float degrees);
  void SetNorthAngle(double angleDeg);
  void TurnToNorthAngle(double angleDeg, double duration = 0.5);
  void SetDirectPos(double lonDeg, double latDeg, double distMeters, double northDeg, double tiltDeg);
  void SetCenterLatLon(double latDeg, double lonDeg);
  void SetFixedZoom(int zoom);
  void ZoomToLOD(int lod);  // P1: JS parity - fly to Sa[lod] altitude
  void ZoomToAltitude(double altitudeMeters);
  void SetZoomLimits(int minZoom, int maxZoom);
  void ResetZoomLimits();
  void SetTileRadius(int radius);
  void Set2DMode(bool enabled);
  void SetMeshCacheSize(size_t size);
  void SetMeshRetryOptions(bool retry, bool continueDivision);
  void SetLang(const std::string& lang);
  void SetFlashPeriod(int ms);
  int GetFlashPeriod() const;
  void SetScreenWidthMeters(double widthMeters, bool lock);

  // JS Navigation parity APIs (P0)
  void SetMinNavigationLOD(int lod);
  void SetMaxNavigationLOD(int lod);
  void SetMinNavigationDist(double distMeters);
  void SetMaxNavigationDist(double distMeters);
  void SetNavigationLOD(int lod);
  void SetNavigationDist(double distMeters);  // Sa-table based
  void ResetNavigationLimits();
  double GetNavMinDist() const;
  double GetNavMaxDist() const;

  // Screen position history (JS FScreenLocPrevNext)
  bool GoToPreviousPosition();
  bool GoToNextPosition();
  bool IsPreviousPositionAvailable() const;
  bool IsNextPositionAvailable() const;
  void ResetPositionHistory();
  
  void SetLockNorth(bool lock);
  bool GetLockNorth() const;
  void SetMouseWheelMode(bool zoomToCursor);
  bool GetMouseWheelMode() const;
  void SetMouseWheelDirection(bool reverse);
  bool GetMouseWheelDirection() const;
  void SetNavigationSpeed(double speed);
  double GetNavigationSpeed() const;
  void SetArrowKeysNavSpeed(double speed);
  void StartWheelZoomInDist(double lonDeg, double latDeg);
  void StartWheelZoomOutDist(double lonDeg, double latDeg);

  int GetCurrentZoom() const;
  double GetCurrentZoomExact() const;
  int GetMaxDrawnZoom() const; // JS Parity: FDRAWED_MAX_LEVEL
  double GetDrawnMaxLevelExact() const; // JS Parity: FDRAWED_MAX_LEVEL + FLOAT
  int GetMinZoom() const;
  int GetMaxZoom() const;
  float GetDistance() const;
  double GetCameraDistMeters() const;
  float GetPitch() const;
  float GetYaw() const;
  double GetCamZ() const;
  int GetScreenWidth() const;
  int GetScreenHeight() const;
  double GetFPS() const;
  size_t GetTextureCacheSize() const; // Returns estimated texture memory usage in bytes
  glm::dvec3 GetEulerAngles() const;  // Returns Euler angles (x=heading, y=tilt, z=roll) in radians
  double GetCenterLat() const;
  double GetCenterLon() const;
  double GetAltitude() const;
  double GetNorthAngle() const;
  double LodFromAltitudeMeters(double altitudeMeters) const;
  double AltitudeMetersFromLod(double lod) const;
  bool IsValid() const;
  bool IsScreenMoving() const;
  bool GetCursorPos(double& outX, double& outY) const;
  void UpdateCursorPosFromScreen(int screenX, int screenY);
  double GetTiltAngle() const;

  // Animation
  void FlyToPoint(double lat, double lon, double altitude, double northDeg, double tiltDeg, double duration);
  void FlyToRegion(double minLat, double minLon, double maxLat, double maxLon, double duration, double padding = 1.0);
  bool IsAnimating() const;
  void CancelAnimation();

  // Layer management
  LayerManager* GetLayerManager();
  void AddIconMap(const std::string& name, const std::string& imageUrl, const std::string& jsonUrl, std::function<void(bool)> callback = nullptr);

  // Raster layer management
  void AddRasterLayer(const RasterLayerConfig& config);
  void RemoveRasterLayer(const std::string& layerId);
  void SetRasterLayerVisibility(const std::string& layerId, bool visible);
  void SetRasterLayerOpacity(const std::string& layerId, float opacity);
  void SetRasterLayerZIndex(const std::string& layerId, int zIndex);
  bool GetRasterLayerConfigById(const std::string& layerId, RasterLayerConfig& out) const;
  bool GetRasterLayerConfigByIndex(size_t index, RasterLayerConfig& out) const;
  std::vector<std::string> GetRasterLayerIds() const;

  // Query/Picking
  bool ScreenToGeo(int screenX, int screenY, double& outLat, double& outLon);
  bool GeoToScreen(double lat, double lon, int& outX, int& outY);
  std::vector<Feature*> QueryFeaturesAtScreen(int screenX, int screenY, double tolerancePx = 5.0);
  bool SampleTerrainHeightMeters(double lon, double lat, int lod, double& outHeight) const;
  
  // Async Elevation Query (Google Earth style callback-based)
  using ElevationCallback = std::function<void(bool success, double elevation)>;
  void QueryElevationAsync(double lat, double lon, ElevationCallback callback);
  double GetAbsoluteAltitude(double lat, double lon, double altitude, AltitudeMode mode) const;
  
  // Cache Pin/Unpin (Google Earth style - prevent eviction of important tiles)
  void PinTile(int z, int x, int y);
  void UnpinTile(int z, int x, int y);
  void PinTilesInRegion(double minLat, double minLon, double maxLat, double maxLon, int zoom);
  void UnpinAllTiles();
  size_t GetPinnedTileCount() const;

  // Parity Snapshot API (Faz 0) - JS↔C++ karşılaştırma için
  struct ParitySnapshot {
    double altitude;           // Kamera yüksekliği (meters)
    double lodExact;           // Kesirli LOD değeri
    int currentZoom;           // Yuvarlanmış LOD
    int navMinLOD;             // Navigasyon min LOD
    int navMaxLOD;             // Navigasyon max LOD
    double navMinDist;         // Min mesafe (normalized)
    double navMaxDist;         // Max mesafe (normalized)
    int meshN;                 // Mesh grid boyutu (JS: MESHN=5)
    int maxCellCanBeCreated;   // Frame başı max cell (JS: dinamik 5/2/100)
    int cellDivisionCount;     // Bu frame'de oluşturulan cell sayısı
    double centerLat;          // Merkez enlem
    double centerLon;          // Merkez boylam
    double tiltDeg;            // Tilt açısı
    double northDeg;           // Kuzey açısı
    double cameraDist;         // Kamera mesafesi (normalized)
    int screenWidth;           // Ekran genişliği
    int screenHeight;          // Ekran yüksekliği
    double fps;                // FPS
    bool isAnimating;          // Animasyon durumu
    bool isScreenMoving;       // Ekran hareket durumu
    bool is2DMode;             // 2D modda mı?
    int clampCount;            // Toplam clamp sayısı
    double lastClampMin;       // Son clamp min değeri (normalized)
    double lastClampMax;       // Son clamp max değeri (normalized)
    
    // Telemetry (Phase 0)
    double frameTimeMs;        // Frame render time (ms)
    double avgTileLoadTimeMs;  // Avg tile download+decode time (ms)
    double maxTileLoadTimeMs;  // Max tile download+decode time (ms)
    size_t tileCacheCount;     // Total cached tiles
    size_t tileCacheSize;      // Total cached size (bytes)
  };
  ParitySnapshot GetParitySnapshot() const;
  std::string DumpParitySnapshot() const;  // JSON formatında
  
  // Image Overlays (Phase 19)
  struct ImageOverlay {
    int id;
    std::string url;
    double minLat, minLon, maxLat, maxLon;
    float opacity = 1.0f;
    float rotation = 0.0f;
    glm::vec4 color = glm::vec4(1.0f);
    
    uint32_t textureId = 0;
    bool loaded = false;
    int width = 0, height = 0;
    uint32_t vao = 0, vbo = 0, ebo = 0;
    size_t vertexCount = 0;
  };
  
  void AddImageOverlay(int id, const std::string& url, double minLon, double minLat, double maxLon, double maxLat, float opacity = 1.0f, float rotation = 0.0f);
  void DeleteImageOverlay(int id);
  void DeleteAllImageOverlays();
  void SetImageOverlayColor(int id, const glm::vec4& color, float opacity);
  void ChangeImageOverlayURL(int id, const std::string& url);
  
  // Analysis (Phase 19)
  struct LineOfSightResult {
      bool visible = true;
      double hitLat = 0, hitLon = 0, hitAlt = 0;
  };
  LineOfSightResult CheckLineOfSight(double lat1, double lon1, double alt1, double lat2, double lon2, double alt2);
  
  struct ProfileSample {
      double lat, lon, dist, height;
  };
  std::vector<ProfileSample> GetElevationProfile(double lat1, double lon1, double lat2, double lon2, int samples, int lod = 14);

private:
  struct Impl;
  Impl* impl_ = nullptr;
};
