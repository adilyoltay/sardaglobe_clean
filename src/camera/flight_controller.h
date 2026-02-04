#pragma once

#include "earth_camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <cmath>

namespace earth {

// ============================================================================
// GOOGLE EARTH CONFIGURATION PARAMETERS (from WASM reverse engineering)
// Source: /mirth/camera/* configuration paths
// ============================================================================

// Globe constants
constexpr double EARTH_RADIUS_KM = 6378.137;
constexpr double EARTH_RADIUS_M = EARTH_RADIUS_KM * 1000.0;

// MapCameraManipulator config
constexpr double CAMERA_MIN_ALTITUDE_M = 10.0;
constexpr double CAMERA_MAX_ALTITUDE_M = 50000000.0;

// Tilt fade config (altitude-based tilt limiting)
constexpr double BEGIN_TILT_FADE_KM = 500.0;
constexpr double END_TILT_FADE_KM = 10000.0;

// EarthPanRotateZoomAction config
constexpr double HORIZON_PAN_THRESHOLD = 0.05;
constexpr double TILT_ALTITUDE_ADJUST_LIMIT = 0.8;

// OrbitAction config
constexpr double HORIZON_TILT_THRESHOLD = 89.0;
constexpr double HEADING_SENSITIVITY = 0.25;
constexpr double TILT_SENSITIVITY = 0.15;

// DampedVelocityAction config
constexpr double DAMPING_COEFFICIENT = 4.0;
constexpr double MIN_VELOCITY_THRESHOLD = 0.001;
constexpr double MOMENTUM_TIMEOUT_SEC = 3.0;

// Zoom config
constexpr double ZOOM_SCROLL_STEP = 0.15;
constexpr double ZOOM_DRAG_SENSITIVITY = 0.003;

// Drag threshold (prevents accidental drags)
constexpr double DRAG_THRESHOLD_PIXELS = 3.0;

// ============================================================================
// THROW ANIMATION TYPES (Google Earth: EarthPanRotateZoomAction::ThrowAnimation)
// ============================================================================
enum class ThrowAnimationType {
    None,
    Rotation,       // RotationThrowAnimation - Great circle rotation
    Arcball,        // ArcballThrowAnimation - Arcball rotation (fallback)
    Zoom,           // ZoomThrowAnimation - Zoom momentum
    Lla             // LlaThrowAnimation - Lat/Lon/Alt based
};

// ============================================================================
// TRAJECTORY MODE (Google Earth: TrajectoryMode)
// ============================================================================
enum class TrajectoryMode {
    Direct,         // Straight line
    Parabolic,      // Arc trajectory (for long distances)
    Ballistic       // Ballistic arc
};

// ============================================================================
// FLIGHT CONTROLLER - 100% Google Earth Parity
// Based on: EarthPanRotateZoomAction, OrbitAction, DampedVelocityAction
// ============================================================================
class FlightController {
public:
    explicit FlightController(PerspectiveCamera& camera);
    
    // === Input Events (MapCameraManipulator interface) ===
    void OnMouseDown(int button, double x, double y, double time);
    void OnMouseUp(int button, double time);
    void OnMouseMove(double x, double y, double time);
    void OnScroll(double xoffset, double yoffset);
    void OnDoubleClick(double x, double y);
    void OnKeyDown(int key);
    void OnKeyUp(int key);
    void OnModifiers(bool shift, bool ctrl);
    void OnWindowResize(int width, int height);
    
    // === Navigation API (CameraManager interface) ===
    void FlyTo(double lat, double lon, double altM, double heading, double tilt,
               double duration = 2.0, TrajectoryMode mode = TrajectoryMode::Parabolic);
    void FlyToLocation(double lat, double lon, double altM, double heading, double tilt,
                       double duration = 2.0) { FlyTo(lat, lon, altM, heading, tilt, duration); }
    void SetCameraTo(double lat, double lon, double altM, double heading, double tilt);
    void StopAnimation();
    
    // === Configuration ===
    void SetNavigationSpeed(double speed) { m_navSpeed = speed; }
    void SetLockNorth(bool lock) { m_lockNorth = lock; }
    void SetZoomToCursor(bool enable) { m_zoomToCursor = enable; }
    
    // === Status ===
    bool IsMoving() const;
    bool IsAnimating() const { return m_flyToActive || m_throwActive; }
    bool GetPivot(glm::dvec3& outPoint) const;
    
    // === Terrain Picking ===
    using PickCallback = std::function<bool(double x, double y, glm::dvec3& hitPoint)>;
    void SetPickCallback(PickCallback cb) { m_pickCallback = std::move(cb); }
    
    // === Frame Update ===
    void Update(double dt, double currentTime);

private:
    PerspectiveCamera& m_camera;
    PickCallback m_pickCallback;
    
    // === Window State ===
    int m_windowW = 1280;
    int m_windowH = 720;
    
    // === Configuration ===
    double m_navSpeed = 1.0;
    bool m_lockNorth = false;
    bool m_zoomToCursor = true;
    
    // === Modifier Keys ===
    bool m_shiftDown = false;
    bool m_ctrlDown = false;
    
    // === Mouse State ===
    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_mouseDownX = 0.0;
    double m_mouseDownY = 0.0;
    double m_lastMoveTime = 0.0;
    double m_dragStartTime = 0.0;
    bool m_dragThresholdExceeded = false;
    double m_accumulatedDrag = 0.0;
    
    // === Action State (EarthPanRotateZoomAction) ===
    enum class ActionMode { None, Pan, Orbit, Zoom };
    ActionMode m_actionMode = ActionMode::None;
    bool m_isDragging = false;
    
    // === Pan State ===
    glm::dvec3 m_panAnchorWorld{0.0};    // World position of anchor point (with terrain height)
    glm::dvec3 m_panAnchorDir{0.0};      // Normalized direction to anchor
    double m_panAnchorHeight = 0.0;      // Terrain height at anchor (distance from sphere surface)
    bool m_hasPanAnchor = false;
    
    // Screen position tracking for anchor stability
    double m_panScreenX = 0.0;           // Current cursor X during pan
    double m_panScreenY = 0.0;           // Current cursor Y during pan
    
    // Velocity tracking for pan momentum
    glm::dvec3 m_lastPanAxis{0.0};
    double m_lastPanAngle = 0.0;
    double m_lastPanTime = 0.0;
    
    // === Orbit State (OrbitAction) ===
    glm::dvec3 m_orbitPivot{0.0};
    glm::dvec3 m_orbitStartPivotToCam{0.0};  // Baseline pivot-to-cam for total rotation
    bool m_hasOrbitPivot = false;
    double m_orbitStartHeading = 0.0;
    double m_orbitStartTilt = 0.0;
    double m_orbitStartX = 0.0;
    double m_orbitStartY = 0.0;
    
    // Velocity tracking for orbit
    double m_headingVelocity = 0.0;
    double m_tiltVelocity = 0.0;
    
    // === Zoom State ===
    double m_zoomStartY = 0.0;
    double m_zoomVelocity = 0.0;
    
    // === ThrowAnimation State (DampedVelocityAction) ===
    bool m_throwActive = false;
    double m_throwStartTime = 0.0;
    ThrowAnimationType m_throwType = ThrowAnimationType::None;
    
    // RotationThrowAnimation data
    glm::dvec3 m_throwRotationAxis{0.0};
    double m_throwAngularVelocity = 0.0;
    
    // OrbitThrowAnimation data  
    double m_throwHeadingVel = 0.0;
    double m_throwTiltVel = 0.0;
    
    // ZoomThrowAnimation data
    double m_throwZoomVel = 0.0;
    
    // === FlyTo Animation State ===
    bool m_flyToActive = false;
    double m_flyToStartTime = 0.0;
    double m_flyToDuration = 2.0;
    TrajectoryMode m_flyToMode = TrajectoryMode::Parabolic;
    
    double m_flyToFromLat, m_flyToFromLon, m_flyToFromAlt;
    double m_flyToFromHeading, m_flyToFromTilt;
    double m_flyToToLat, m_flyToToLon, m_flyToToAlt;
    double m_flyToToHeading, m_flyToToTilt;
    double m_flyToPeakAlt = 0.0;
    double m_flyToDistance = 0.0;
    
    // === Helper Methods ===
    
    // Ray casting
    bool RayGlobeIntersect(const glm::dvec3& origin, const glm::dvec3& dir,
                           glm::dvec3& outHit) const;
    void ScreenToRay(double x, double y, glm::dvec3& origin, glm::dvec3& dir) const;
    
    // Camera manipulation
    void RotateCameraGreatCircle(const glm::dvec3& axis, double angle);
    double GetAltitudeM() const;
    double GetAltitudeKm() const { return GetAltitudeM() / 1000.0; }
    
    // Tilt fade (MapCameraManipulator::GetTiltFadeFactor)
    double GetTiltFadeFactor() const;
    double GetMaxTilt() const;
    
    // Drag threshold filter (DragThresholdFilterActionAdapter)
    bool CheckDragThreshold(double dx, double dy);
    
    // ThrowAnimation helpers
    void StartThrowAnimation(ThrowAnimationType type, double time);
    void UpdateThrowAnimation(double dt, double time);
    void StopThrowAnimation();
    
    // FlyTo helpers
    void UpdateFlyToAnimation(double time);
    double CalculateGreatCircleDistance(double lat1, double lon1, double lat2, double lon2) const;
    
    // Easing function
    static double EaseInOutCubic(double t) {
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }
};

} // namespace earth
