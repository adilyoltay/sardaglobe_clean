#pragma once

#include "earth_camera.h"
#include <glm/glm.hpp>
#include <functional>
#include <deque>

namespace earth {

// Google Earth parity constants
constexpr double GLOBE_RADIUS_KM = 6378.137;
constexpr double MIN_CAMERA_ALT_M = 10.0;
constexpr double MAX_CAMERA_ALT_M = 100000000.0;
constexpr double MOMENTUM_DURATION_S = 1.0;
constexpr double DAMPING_COEFF = 6.0;

// Easing functions
namespace easing {
    inline double QuadOut(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }
    inline double CubicOut(double t) { return 1.0 - std::pow(1.0 - t, 3.0); }
}

// Altitude-based tilt limit (Google Earth style)
// At very high altitude, limit tilt to prevent disorientation
inline double GetMaxTiltForAltitude(double altitudeKm) {
    if (altitudeKm > 10000.0) return 60.0;   // > 10,000 km (space view)
    if (altitudeKm > 1000.0)  return 70.0;   // > 1,000 km
    if (altitudeKm > 100.0)   return 80.0;   // > 100 km
    return 89.0;                              // Normal max tilt
}

class FlightController {
public:
    FlightController(PerspectiveCamera& camera);

    // Input Events
    void OnMouseDown(int button, double x, double y, double time);
    void OnMouseUp(int button, double time);
    void OnMouseMove(double x, double y, double time);
    void OnScroll(double xoffset, double yoffset);
    void OnDoubleClick(double x, double y);
    void OnModifiers(bool shift, bool ctrl);
    void OnKeyDown(int key);
    void OnKeyUp(int key);
    void OnWindowResize(int width, int height);

    // Navigation API
    void FlyToLocation(double lat, double lon, double altMeters, 
                       double heading, double tilt, double duration = 1.5);
    void StopAnimation();

    // Configuration
    void SetNavigationSpeed(double speed) { m_navSpeed = speed; }
    void SetLockNorth(bool lock) { m_lockNorth = lock; }
    void SetNavigationLimits(double minDist, double maxDist);
    void SetMouseWheelSettings(bool zoomToCursor, bool reverse);

    // Status
    bool IsMoving() const;
    bool GetPivot(glm::dvec3& outPoint) const;

    // Terrain picking callback
    using PickCallback = std::function<bool(double x, double y, glm::dvec3& outPoint)>;
    void SetPickCallback(PickCallback cb) { m_pickCallback = cb; }

    // Update (call every frame)
    void Update(double dt, double currentTime);

private:
    PerspectiveCamera& m_camera;
    PickCallback m_pickCallback;
    
    int m_windowW = 1280;
    int m_windowH = 720;
    double m_navSpeed = 1.0;
    bool m_lockNorth = false;
    double m_minDist = MIN_CAMERA_ALT_M;
    double m_maxDist = MAX_CAMERA_ALT_M;
    bool m_wheelZoomToCursor = true;
    bool m_wheelReverse = false;

    // Modifier keys
    bool m_shiftDown = false;
    bool m_ctrlDown = false;

    // Mouse state
    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_lastMoveTime = 0.0;

    // Input Smoothing (Google Earth style - weighted multi-frame averaging)
    struct InputSmoother {
        static constexpr int HISTORY_SIZE = 5;
        static constexpr double WEIGHTS[HISTORY_SIZE] = {0.35, 0.25, 0.20, 0.12, 0.08};
        
        struct Sample {
            double x = 0.0, y = 0.0;
            double time = 0.0;
        };
        Sample history[HISTORY_SIZE] = {};
        int count = 0;
        
        void AddSample(double x, double y, double time) {
            // Shift history
            for (int i = HISTORY_SIZE - 1; i > 0; --i) {
                history[i] = history[i - 1];
            }
            history[0] = {x, y, time};
            if (count < HISTORY_SIZE) count++;
        }
        
        void GetSmoothed(double& outX, double& outY) const {
            if (count == 0) { outX = outY = 0.0; return; }
            
            double totalWeight = 0.0;
            outX = outY = 0.0;
            
            for (int i = 0; i < count; ++i) {
                outX += history[i].x * WEIGHTS[i];
                outY += history[i].y * WEIGHTS[i];
                totalWeight += WEIGHTS[i];
            }
            outX /= totalWeight;
            outY /= totalWeight;
        }
        
        void Reset() { count = 0; }
    };
    InputSmoother m_inputSmoother;

    // Drag modes (mutually exclusive)
    enum class DragMode { None, Pan, Orbit, Zoom };
    DragMode m_dragMode = DragMode::None;
    double m_dragStartTime = 0.0;

    // Pan state (Grab Earth)
    glm::dvec3 m_panAnchor{0.0};      // Point on globe being dragged
    double m_panAnchorRadius = GLOBE_RADIUS_KM;
    glm::dvec3 m_panPrevHit{0.0};     // Previous frame's hit point
    bool m_hasPanPrevHit = false;

    // Orbit state (Shift+Left or Middle)
    glm::dvec3 m_orbitPivot{0.0};     // Point we're orbiting around
    bool m_hasOrbitPivot = false;
    double m_orbitStartHeading = 0.0;
    double m_orbitStartTilt = 0.0;
    double m_orbitStartX = 0.0;
    double m_orbitStartY = 0.0;
    glm::dvec2 m_orbitPivotScreenPos{0.0}; // Screen position to maintain (for correction)

    // Zoom state (Right drag)
    double m_zoomStartY = 0.0;
    double m_zoomStartDist = 0.0;

    // Zoom to cursor state
    glm::dvec3 m_zoomTarget{0.0};
    bool m_hasZoomTarget = false;

    // Momentum (throw animation) - Google Earth style
    struct Momentum {
        bool active = false;
        double startTime = 0.0;
        
        // Pan momentum
        glm::dvec3 axis{0.0};         // Rotation axis
        double velocity = 0.0;        // Angular velocity (rad/s)
        
        // Orbit momentum  
        double headingVel = 0.0;      // Heading velocity (deg/s)
        double tiltVel = 0.0;         // Tilt velocity (deg/s)
        bool orbitMode = false;       // True if orbit momentum, false if pan
        
        // Zoom momentum (ZoomThrowAnimation - Google Earth style)
        bool zoomMode = false;
        double zoomVelocity = 0.0;    // Zoom velocity (log-space)
        glm::dvec3 zoomTarget{0.0};   // Target point for point-stable zoom
        bool hasZoomTarget = false;
        
        // Velocity history for smoothing
        static constexpr int HISTORY_SIZE = 5;
        double velHistory[HISTORY_SIZE] = {0};
        int historyIndex = 0;
        
        // Friction parameters
        double friction = 4.0;        // Deceleration rate
        double minVelocity = 0.0001;  // Stop threshold
    };
    Momentum m_momentum;
    
    // Scroll momentum tracking
    double m_lastScrollTime = 0.0;
    double m_scrollVelocity = 0.0;
    
    // Velocity tracking during drag
    double m_lastDragTime = 0.0;
    glm::dvec3 m_lastDragAxis{0.0};
    double m_lastDragVelocity = 0.0;

    // FlyTo animation
    struct FlyToAnim {
        bool active = false;
        double startTime = 0.0;
        double duration = 1.5;
        double startLat, startLon, startAlt;
        double startHeading, startTilt;
        double endLat, endLon, endAlt;
        double endHeading, endTilt;
    };
    FlyToAnim m_flyTo;

    // Helpers
    bool IntersectGlobe(const glm::dvec3& origin, const glm::dvec3& dir, 
                        glm::dvec3& outHit, double radius = GLOBE_RADIUS_KM);
    void ApplyPanRotation(const glm::dvec3& axis, double angle);
    void ApplyOrbit(double deltaHeading, double deltaTilt);
    void ApplyZoom(double factor);
    
    // ENU frame helpers (pole-safe)
    void GetLocalENU(const glm::dvec3& point, glm::dvec3& east, glm::dvec3& north, glm::dvec3& up);
    void ComputeHeadingTilt(const glm::dvec3& pos, const glm::dvec3& fwd,
                            double fallbackHeading, double& outHeading, double& outTilt);
};

} // namespace earth
