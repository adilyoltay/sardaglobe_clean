#include "flight_controller.h"
#include <algorithm>
#include <cmath>
#include <glm/gtx/vector_angle.hpp>
#include <GLFW/glfw3.h>

namespace earth {

FlightController::FlightController(PerspectiveCamera& camera) 
    : m_camera(camera) {}

void FlightController::OnWindowResize(int width, int height) {
    m_windowW = width;
    m_windowH = height;
}

void FlightController::OnModifiers(bool shift, bool ctrl) {
    m_shiftDown = shift;
    m_ctrlDown = ctrl;
}

bool FlightController::IsMoving() const {
    return m_dragMode != DragMode::None || 
           m_momentum.active || 
           m_flyTo.active;
}

void FlightController::SetNavigationLimits(double minDist, double maxDist) {
    m_minDist = minDist;
    m_maxDist = maxDist;
}

void FlightController::SetMouseWheelSettings(bool zoomToCursor, bool reverse) {
    m_wheelZoomToCursor = zoomToCursor;
    m_wheelReverse = reverse;
}

bool FlightController::GetPivot(glm::dvec3& outPoint) const {
    if (m_hasOrbitPivot) {
        outPoint = m_orbitPivot;
        return true;
    }
    return false;
}

// ============================================================================
// SPHERE INTERSECTION
// ============================================================================
bool FlightController::IntersectGlobe(const glm::dvec3& origin, const glm::dvec3& dir, 
                                       glm::dvec3& outHit, double radius) {
    double b = 2.0 * glm::dot(origin, dir);
    double c = glm::dot(origin, origin) - radius * radius;
    double disc = b * b - 4.0 * c;
    
    if (disc < 0.0) return false;
    
    double t = (-b - std::sqrt(disc)) / 2.0;
    if (t < 0.0) t = (-b + std::sqrt(disc)) / 2.0;
    if (t < 0.0) return false;
    
    outHit = origin + dir * t;
    return true;
}

// ============================================================================
// LOCAL ENU FRAME (Pole-Safe)
// ============================================================================
void FlightController::GetLocalENU(const glm::dvec3& point, 
                                    glm::dvec3& east, glm::dvec3& north, glm::dvec3& up) {
    up = glm::normalize(point);
    
    // Check if near pole (up.z close to ±1)
    double upZ = std::abs(up.z);
    if (upZ > 0.999) {
        // At pole: use camera heading to define east
        double headingRad = glm::radians(m_camera.GetHeading());
        // At north pole, "east" points toward lon=heading+90
        // At south pole, sign flips
        double sign = (up.z > 0) ? 1.0 : -1.0;
        east = glm::dvec3(-std::sin(headingRad), std::cos(headingRad), 0.0);
        north = sign * glm::dvec3(-std::cos(headingRad), -std::sin(headingRad), 0.0);
    } else {
        // Standard ENU: east = Z × up (normalized)
        glm::dvec3 globalZ(0.0, 0.0, 1.0);
        east = glm::normalize(glm::cross(globalZ, up));
        north = glm::cross(up, east);
    }
}

void FlightController::ComputeHeadingTilt(const glm::dvec3& pos, const glm::dvec3& fwd,
                                          double fallbackHeading, 
                                          double& outHeading, double& outTilt) {
    double r = glm::length(pos);
    if (r < 1e-9) {
        outHeading = fallbackHeading;
        outTilt = 0.0;
        return;
    }

    glm::dvec3 east, north, up;
    GetLocalENU(pos, east, north, up);

    // Tilt: angle from nadir (straight down)
    glm::dvec3 f = glm::normalize(fwd);
    double dotDown = glm::dot(f, -up);
    outTilt = glm::degrees(std::acos(std::clamp(dotDown, -1.0, 1.0)));

    // Heading: project forward onto horizontal plane
    glm::dvec3 fHoriz = f - up * glm::dot(f, up);
    if (glm::length(fHoriz) < 1e-9) {
        outHeading = fallbackHeading;
    } else {
        fHoriz = glm::normalize(fHoriz);
        outHeading = glm::degrees(std::atan2(glm::dot(fHoriz, east), glm::dot(fHoriz, north)));
    }
}

// ============================================================================
// MOUSE DOWN
// ============================================================================
void FlightController::OnMouseDown(int button, double x, double y, double time) {
    // Stop any active momentum/animation
    m_momentum.active = false;
    m_flyTo.active = false;
    
    m_mouseX = x;
    m_mouseY = y;
    m_lastMoveTime = time;
    m_dragStartTime = time;
    
    // Reset input smoother for fresh drag
    m_inputSmoother.Reset();
    
    // Raycast to globe
    glm::dvec3 origin, dir;
    m_camera.GetRay(x, y, m_windowW, m_windowH, origin, dir);
    dir = glm::normalize(dir);
    
    glm::dvec3 hit;
    bool onGlobe = false;
    
    // Try terrain pick first
    if (m_pickCallback) {
        onGlobe = m_pickCallback(x, y, hit);
    }
    if (!onGlobe) {
        onGlobe = IntersectGlobe(origin, dir, hit);
    }

    if (button == 0) { // Left mouse
        if (m_shiftDown) {
            // Shift+Left = Orbit
            m_dragMode = DragMode::Orbit;
            m_hasOrbitPivot = onGlobe;
            if (onGlobe) {
                m_orbitPivot = hit;
            } else {
                // Fallback: use screen center
                glm::dvec3 cOrigin, cDir;
                m_camera.GetRay(m_windowW/2.0, m_windowH/2.0, m_windowW, m_windowH, cOrigin, cDir);
                m_hasOrbitPivot = IntersectGlobe(cOrigin, glm::normalize(cDir), m_orbitPivot);
            }
            m_orbitStartHeading = m_camera.GetHeading();
            m_orbitStartTilt = m_camera.GetTilt();
            m_orbitStartX = x;
            m_orbitStartY = y;
            m_orbitPivotScreenPos = {x, y}; // Save screen position for correction
        } else {
            // Left = Pan (Grab Earth)
            m_dragMode = DragMode::Pan;
            if (onGlobe) {
                m_panAnchor = hit;
                m_panAnchorRadius = glm::length(hit);
            } else {
                m_panAnchorRadius = GLOBE_RADIUS_KM;
            }
            m_hasPanPrevHit = false;
        }
    } 
    else if (button == 1) { // Right = Zoom drag
        m_dragMode = DragMode::Zoom;
        m_zoomStartY = y;
        m_zoomStartDist = glm::length(m_camera.GetPositionECEF());
    }
    else if (button == 2) { // Middle = Orbit
        m_dragMode = DragMode::Orbit;
        m_hasOrbitPivot = onGlobe;
        if (onGlobe) {
            m_orbitPivot = hit;
        } else {
            glm::dvec3 cOrigin, cDir;
            m_camera.GetRay(m_windowW/2.0, m_windowH/2.0, m_windowW, m_windowH, cOrigin, cDir);
            m_hasOrbitPivot = IntersectGlobe(cOrigin, glm::normalize(cDir), m_orbitPivot);
        }
        m_orbitStartHeading = m_camera.GetHeading();
        m_orbitStartTilt = m_camera.GetTilt();
        m_orbitStartX = x;
        m_orbitStartY = y;
        m_orbitPivotScreenPos = {x, y}; // Save screen position for correction
    }
}

// ============================================================================
// MOUSE UP
// ============================================================================
void FlightController::OnMouseUp(int button, double time) {
    if (m_dragMode == DragMode::None) return;
    
    // Check if drag was long enough for momentum
    double dragDuration = time - m_dragStartTime;
    double timeSinceMove = time - m_lastMoveTime;
    
    // Only activate momentum if: moved recently (< 100ms) and dragged for > 50ms
    bool shouldMomentum = (timeSinceMove < 0.1) && (dragDuration > 0.05);
    
    if (shouldMomentum) {
        // Compute smoothed velocity from history
        double avgVel = 0.0;
        for (int i = 0; i < Momentum::HISTORY_SIZE; i++) {
            avgVel += m_momentum.velHistory[i];
        }
        avgVel /= Momentum::HISTORY_SIZE;
        
        if (m_dragMode == DragMode::Pan && std::abs(avgVel) > 0.001) {
            m_momentum.active = true;
            m_momentum.orbitMode = false;
            m_momentum.startTime = time;
            m_momentum.velocity = avgVel;
            m_momentum.axis = m_lastDragAxis;
            // Adjust friction based on velocity - faster flicks have less friction
            m_momentum.friction = 3.0 + 2.0 / (1.0 + std::abs(avgVel));
        }
        else if (m_dragMode == DragMode::Orbit) {
            double orbitSpeed = std::sqrt(m_momentum.headingVel * m_momentum.headingVel + 
                                          m_momentum.tiltVel * m_momentum.tiltVel);
            if (orbitSpeed > 1.0) { // deg/s threshold
                m_momentum.active = true;
                m_momentum.orbitMode = true;
                m_momentum.startTime = time;
                m_momentum.friction = 4.0;
            }
        }
    }
    
    m_dragMode = DragMode::None;
    m_hasPanPrevHit = false;
}

// ============================================================================
// MOUSE MOVE
// ============================================================================
void FlightController::OnMouseMove(double x, double y, double time) {
    double dx = x - m_mouseX;
    double dy = y - m_mouseY;
    
    // Skip if no significant movement
    if (std::abs(dx) < 0.5 && std::abs(dy) < 0.5) {
        return;
    }
    
    double dt = time - m_lastMoveTime;
    m_lastMoveTime = time;
    m_mouseX = x;
    m_mouseY = y;
    
    if (m_dragMode == DragMode::None) return;
    
    // Input smoothing: add sample and get smoothed delta
    m_inputSmoother.AddSample(dx, dy, time);
    double smoothDx, smoothDy;
    m_inputSmoother.GetSmoothed(smoothDx, smoothDy);

    // ========== PAN (Grab Earth) ==========
    if (m_dragMode == DragMode::Pan) {
        glm::dvec3 origin, dir;
        m_camera.GetRay(x, y, m_windowW, m_windowH, origin, dir);
        dir = glm::normalize(dir);
        
        glm::dvec3 currHit;
        bool currOnGlobe = IntersectGlobe(origin, dir, currHit, m_panAnchorRadius);
        
        // Check if ray is near-tangent (horizon)
        glm::dvec3 toCenter = -glm::normalize(origin);
        bool nearHorizon = glm::dot(dir, toCenter) < 0.05;
        
        if (m_hasPanPrevHit && currOnGlobe && !nearHorizon) {
            // Great Circle Pan: rotate camera so prevHit moves to currHit
            glm::dvec3 vPrev = glm::normalize(m_panPrevHit);
            glm::dvec3 vCurr = glm::normalize(currHit);
            
            // Rotation axis = cross(curr, prev) - moves camera opposite to mouse
            glm::dvec3 axis = glm::cross(vCurr, vPrev);
            double sinAngle = glm::length(axis);
            
            if (sinAngle > 1e-6) {
                double cosAngle = glm::dot(vPrev, vCurr);
                double angle = std::atan2(sinAngle, cosAngle);
                axis = glm::normalize(axis);
                
                ApplyPanRotation(axis, angle);
                
                // Track velocity for momentum (with history)
                if (dt > 0.001) {
                    double vel = std::min(angle / dt, 3.0); // Clamp max velocity
                    
                    // Store in velocity history for smoothing
                    m_momentum.velHistory[m_momentum.historyIndex] = vel;
                    m_momentum.historyIndex = (m_momentum.historyIndex + 1) % Momentum::HISTORY_SIZE;
                    
                    // Track last valid axis
                    m_lastDragAxis = axis;
                    m_lastDragVelocity = vel;
                    m_lastDragTime = time;
                }
            }
        }
        
        // Update prev hit for next frame (after rotation applied)
        if (currOnGlobe && !nearHorizon) {
            m_camera.GetRay(x, y, m_windowW, m_windowH, origin, dir);
            if (IntersectGlobe(origin, glm::normalize(dir), currHit, m_panAnchorRadius)) {
                m_panPrevHit = currHit;
                m_hasPanPrevHit = true;
            }
        } else {
            m_hasPanPrevHit = false;
        }
    }
    
    // ========== ORBIT (Heading/Tilt) ==========
    else if (m_dragMode == DragMode::Orbit) {
        double sensitivity = 0.3 * m_navSpeed;
        // Use smoothed input for orbit
        double deltaHeading = -smoothDx * sensitivity;
        double deltaTilt = smoothDy * sensitivity;
        
        if (m_lockNorth) deltaHeading = 0.0;
        
        ApplyOrbit(deltaHeading, deltaTilt);
        
        // Track velocity for momentum (with smoothing)
        if (dt > 0.001) {
            double hVel = deltaHeading / dt;
            double tVel = deltaTilt / dt;
            m_momentum.headingVel = glm::mix(m_momentum.headingVel, hVel, 0.4);
            m_momentum.tiltVel = glm::mix(m_momentum.tiltVel, tVel, 0.4);
        }
    }
    
    // ========== ZOOM (Right drag) ==========
    else if (m_dragMode == DragMode::Zoom) {
        double factor = 1.0 + (y - m_zoomStartY) * 0.005;
        factor = std::clamp(factor, 0.5, 2.0);
        ApplyZoom(factor);
        m_zoomStartY = y;
    }
}

// ============================================================================
// SCROLL (Zoom or Shift+Tilt)
// ============================================================================
void FlightController::OnScroll(double xoffset, double yoffset) {
    m_momentum.active = false;
    m_flyTo.active = false;
    
    // Shift+Scroll = Tilt change (with altitude-based limit)
    if (m_shiftDown) {
        double deltaTilt = yoffset * 5.0; // 5 degrees per notch
        double t = m_camera.GetTilt() - deltaTilt;
        
        // Apply altitude-based tilt limit
        glm::dvec3 camPos = m_camera.GetPositionECEF();
        double altKm = glm::length(camPos) - GLOBE_RADIUS_KM;
        double maxTilt = GetMaxTiltForAltitude(altKm);
        
        t = std::clamp(t, 0.1, maxTilt);
        m_camera.SetTilt(t);
        return;
    }
    
    // Normal scroll = Zoom to cursor (point-stable, logarithmic)
    double scrollDir = m_wheelReverse ? -yoffset : yoffset;
    
    // Get zoom target under cursor
    glm::dvec3 origin, dir;
    m_camera.GetRay(m_mouseX, m_mouseY, m_windowW, m_windowH, origin, dir);
    dir = glm::normalize(dir);
    
    glm::dvec3 target;
    bool hasTarget = false;
    if (m_pickCallback) {
        hasTarget = m_pickCallback(m_mouseX, m_mouseY, target);
    }
    if (!hasTarget) {
        hasTarget = IntersectGlobe(origin, dir, target);
    }
    
    if (hasTarget && m_wheelZoomToCursor) {
        // Point-stable zoom with LOGARITHMIC interpolation
        // Human perception of distance is logarithmic, not linear
        glm::dvec3 camPos = m_camera.GetPositionECEF();
        double camAlt = glm::length(camPos) - GLOBE_RADIUS_KM;
        
        // Logarithmic zoom: equal scroll = equal perceived zoom at all distances
        double logAlt = std::log(camAlt);
        double logStep = scrollDir * 0.3; // Log-space step
        double newLogAlt = logAlt - logStep;
        double newAlt = std::exp(newLogAlt);
        newAlt = std::clamp(newAlt, MIN_CAMERA_ALT_M / 1000.0, MAX_CAMERA_ALT_M / 1000.0);
        
        // Calculate linear factor for position interpolation
        double zoomFactor = newAlt / camAlt;
        
        // Interpolate camera position toward target based on zoom
        double t = 1.0 - zoomFactor; // How much to move toward target
        
        glm::dvec3 camDir = glm::normalize(camPos);
        glm::dvec3 targetDir = glm::normalize(target);
        
        // Spherical interpolation on unit sphere
        double dot = glm::dot(camDir, targetDir);
        dot = std::clamp(dot, -1.0, 1.0);
        double theta = std::acos(dot);
        
        glm::dvec3 newDir;
        if (theta < 1e-6) {
            newDir = camDir;
        } else {
            // Slerp: move t fraction toward target
            double moveAngle = theta * t;
            glm::dvec3 axis = glm::cross(camDir, targetDir);
            if (glm::length(axis) > 1e-6) {
                axis = glm::normalize(axis);
                glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), moveAngle, axis);
                newDir = glm::normalize(glm::dvec3(rot * glm::dvec4(camDir, 0.0)));
            } else {
                newDir = camDir;
            }
        }
        
        double newR = GLOBE_RADIUS_KM + newAlt;
        glm::dvec3 newPos = newDir * newR;
        
        double lat = glm::degrees(std::asin(newPos.z / newR));
        double lon = glm::degrees(std::atan2(newPos.y, newPos.x));
        
        m_camera.SetLatLonAlt(lat, lon, newAlt * 1000.0);
    } else {
        // Fallback: zoom along camera forward (logarithmic)
        glm::dvec3 camPos = m_camera.GetPositionECEF();
        double camAlt = glm::length(camPos) - GLOBE_RADIUS_KM;
        double logAlt = std::log(camAlt);
        double logStep = scrollDir * 0.3;
        double newAlt = std::exp(logAlt - logStep);
        newAlt = std::clamp(newAlt, MIN_CAMERA_ALT_M / 1000.0, MAX_CAMERA_ALT_M / 1000.0);
        ApplyZoom(newAlt / camAlt);
    }
}

// ============================================================================
// DOUBLE CLICK (FlyTo)
// ============================================================================
void FlightController::OnDoubleClick(double x, double y) {
    m_momentum.active = false;
    m_flyTo.active = false;
    
    glm::dvec3 origin, dir;
    m_camera.GetRay(x, y, m_windowW, m_windowH, origin, dir);
    
    glm::dvec3 hit;
    bool onGlobe = false;
    
    if (m_pickCallback) {
        onGlobe = m_pickCallback(x, y, hit);
    }
    if (!onGlobe) {
        onGlobe = IntersectGlobe(origin, glm::normalize(dir), hit);
    }
    
    if (onGlobe) {
        double r = glm::length(hit);
        double lat = glm::degrees(std::asin(hit.z / r));
        double lon = glm::degrees(std::atan2(hit.y, hit.x));
        
        // Zoom in 4x
        glm::dvec3 camPos = m_camera.GetPositionECEF();
        double dist = glm::length(camPos - hit);
        double newAlt = std::max(dist * 0.25, MIN_CAMERA_ALT_M / 1000.0) * 1000.0;
        
        FlyToLocation(lat, lon, newAlt, m_camera.GetHeading(), m_camera.GetTilt(), 1.5);
    }
}

// ============================================================================
// KEYBOARD
// ============================================================================
void FlightController::OnKeyDown(int key) {
    glm::dvec3 pos = m_camera.GetPositionECEF();
    double alt = glm::length(pos) - GLOBE_RADIUS_KM;
    double speed = std::max(alt * 0.001, 0.0001) * m_navSpeed;
    
    glm::dvec3 f, u, r;
    m_camera.GetBasisVectors(f, u, r);
    
    glm::dvec3 up = glm::normalize(pos);
    glm::dvec3 fHoriz = glm::normalize(f - up * glm::dot(f, up));
    glm::dvec3 rHoriz = glm::normalize(glm::cross(fHoriz, up));
    
    glm::dvec3 moveDir{0.0};
    
    switch (key) {
        case GLFW_KEY_LEFT:  moveDir = -rHoriz; break;
        case GLFW_KEY_RIGHT: moveDir = rHoriz; break;
        case GLFW_KEY_UP:    moveDir = fHoriz; break;
        case GLFW_KEY_DOWN:  moveDir = -fHoriz; break;
        case GLFW_KEY_PAGE_UP:   ApplyZoom(0.9); return;
        case GLFW_KEY_PAGE_DOWN: ApplyZoom(1.1); return;
    }
    
    if (glm::length(moveDir) > 0.001) {
        glm::dvec3 axis = glm::cross(pos, moveDir);
        if (glm::length(axis) > 0.001) {
            ApplyPanRotation(glm::normalize(axis), speed * 0.1);
        }
    }
}

void FlightController::OnKeyUp(int key) {
    // Nothing needed
}

// ============================================================================
// APPLY PAN ROTATION
// ============================================================================
void FlightController::ApplyPanRotation(const glm::dvec3& axis, double angle) {
    glm::dvec3 pos = m_camera.GetPositionECEF();
    glm::dvec3 fwd, up, right;
    m_camera.GetBasisVectors(fwd, up, right);
    
    // Rotate position and forward vector
    glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), angle, axis);
    glm::dvec3 newPos = glm::dvec3(rot * glm::dvec4(pos, 1.0));
    glm::dvec3 newFwd = glm::dvec3(rot * glm::dvec4(fwd, 0.0));
    
    // Convert back to LLA
    double r = glm::length(newPos);
    double lat = glm::degrees(std::asin(newPos.z / r));
    double lon = glm::degrees(std::atan2(newPos.y, newPos.x));
    double alt = (r - GLOBE_RADIUS_KM) * 1000.0; // meters
    
    // Compute new heading/tilt
    double heading, tilt;
    ComputeHeadingTilt(newPos, newFwd, m_camera.GetHeading(), heading, tilt);
    
    // Apply
    m_camera.SetLatLonAlt(lat, lon, alt);
    m_camera.SetHeading(heading);
    m_camera.SetTilt(tilt);
}

// ============================================================================
// APPLY ORBIT (Pivot-centered rotation - Google Earth style)
// ============================================================================
void FlightController::ApplyOrbit(double deltaHeading, double deltaTilt) {
    // Get altitude-based tilt limit
    glm::dvec3 camPos = m_camera.GetPositionECEF();
    double altKm = glm::length(camPos) - GLOBE_RADIUS_KM;
    double maxTilt = GetMaxTiltForAltitude(altKm);
    
    if (!m_hasOrbitPivot) {
        // Fallback: just change heading/tilt without moving camera
        double h = m_camera.GetHeading() + deltaHeading;
        double t = m_camera.GetTilt() + deltaTilt;
        while (h > 360.0) h -= 360.0;
        while (h < 0.0) h += 360.0;
        t = std::clamp(t, 0.1, maxTilt);
        m_camera.SetHeading(h);
        m_camera.SetTilt(t);
        return;
    }

    // Get current camera state (camPos already defined above)
    double heading = m_camera.GetHeading();
    double tilt = m_camera.GetTilt();
    
    // Vector from pivot to camera
    glm::dvec3 pivotToCam = camPos - m_orbitPivot;
    double dist = glm::length(pivotToCam);
    if (dist < 1e-6) return;
    
    // Get pivot's local ENU frame
    glm::dvec3 pivotUp = glm::normalize(m_orbitPivot);
    glm::dvec3 pivotEast, pivotNorth;
    
    double upZ = std::abs(pivotUp.z);
    if (upZ > 0.999) {
        double hRad = glm::radians(heading);
        pivotEast = glm::dvec3(-std::sin(hRad), std::cos(hRad), 0.0);
        pivotNorth = (pivotUp.z > 0 ? 1.0 : -1.0) * glm::dvec3(-std::cos(hRad), -std::sin(hRad), 0.0);
    } else {
        pivotEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), pivotUp));
        pivotNorth = glm::cross(pivotUp, pivotEast);
    }
    
    // Rotate camera position around pivot
    // Heading rotation: around pivot's up axis
    double headingRad = glm::radians(deltaHeading);
    glm::dmat4 headingRot = glm::rotate(glm::dmat4(1.0), -headingRad, pivotUp);
    
    // Tilt rotation: around the horizontal axis perpendicular to view
    glm::dvec3 viewDir = glm::normalize(-pivotToCam);
    glm::dvec3 tiltAxis = glm::normalize(glm::cross(pivotUp, viewDir));
    if (glm::length(tiltAxis) < 1e-6) tiltAxis = pivotEast;
    
    double tiltRad = glm::radians(deltaTilt);
    glm::dmat4 tiltRot = glm::rotate(glm::dmat4(1.0), tiltRad, tiltAxis);
    
    // Apply rotations
    glm::dvec3 newPivotToCam = glm::dvec3(headingRot * tiltRot * glm::dvec4(pivotToCam, 0.0));
    glm::dvec3 newCamPos = m_orbitPivot + newPivotToCam;
    
    // Ensure camera stays above ground
    double newR = glm::length(newCamPos);
    double minR = GLOBE_RADIUS_KM + MIN_CAMERA_ALT_M / 1000.0;
    if (newR < minR) {
        newCamPos = glm::normalize(newCamPos) * minR;
    }
    
    // Compute new forward (looking at pivot)
    glm::dvec3 newFwd = glm::normalize(m_orbitPivot - newCamPos);
    
    // Convert to LLA
    double r = glm::length(newCamPos);
    double lat = glm::degrees(std::asin(newCamPos.z / r));
    double lon = glm::degrees(std::atan2(newCamPos.y, newCamPos.x));
    double alt = (r - GLOBE_RADIUS_KM) * 1000.0;
    
    // Compute heading/tilt from new position looking at pivot
    double newHeading, newTilt;
    ComputeHeadingTilt(newCamPos, newFwd, heading + deltaHeading, newHeading, newTilt);
    
    // Clamp tilt with altitude-based limit
    double newAltKm = (r - GLOBE_RADIUS_KM);
    double newMaxTilt = GetMaxTiltForAltitude(newAltKm);
    newTilt = std::clamp(newTilt, 0.1, newMaxTilt);
    
    m_camera.SetLatLonAlt(lat, lon, alt);
    m_camera.SetHeading(newHeading);
    m_camera.SetTilt(newTilt);
    
    // Screen stability correction: keep pivot under original screen position
    // Project pivot to screen and compute correction if needed
    glm::dvec3 pivotDir = glm::normalize(m_orbitPivot - newCamPos);
    glm::dvec3 fwd, up, right;
    m_camera.GetBasisVectors(fwd, up, right);
    
    // Compute pivot's position in camera space
    double dotFwd = glm::dot(pivotDir, fwd);
    if (dotFwd > 0.1) { // Pivot is in front of camera
        double dotRight = glm::dot(pivotDir, right);
        double dotUp = glm::dot(pivotDir, up);
        
        // Convert to screen coordinates (approximate)
        double fov = 45.0; // Assume default FOV
        double aspect = static_cast<double>(m_windowW) / m_windowH;
        double tanHalfFov = std::tan(glm::radians(fov / 2.0));
        
        double screenX = m_windowW / 2.0 + (dotRight / dotFwd / tanHalfFov / aspect) * (m_windowW / 2.0);
        double screenY = m_windowH / 2.0 - (dotUp / dotFwd / tanHalfFov) * (m_windowH / 2.0);
        
        // Compute screen error
        double errorX = m_orbitPivotScreenPos.x - screenX;
        double errorY = m_orbitPivotScreenPos.y - screenY;
        double errorMag = std::sqrt(errorX * errorX + errorY * errorY);
        
        // Apply micro-pan correction if error is significant but not too large
        if (errorMag > 1.0 && errorMag < 50.0) {
            double corrStrength = std::min(0.3, errorMag / 100.0);
            double anglePerPixel = glm::radians(fov) / m_windowH;
            
            double corrYaw = -errorX * anglePerPixel * corrStrength;
            double corrPitch = errorY * anglePerPixel * corrStrength;
            
            // Apply small rotation to correct pivot position
            glm::dvec3 corrAxis = right * corrPitch + up * corrYaw;
            double corrAngle = glm::length(corrAxis);
            
            if (corrAngle > 1e-8 && corrAngle < 0.05) {
                corrAxis = glm::normalize(corrAxis);
                ApplyPanRotation(corrAxis, corrAngle);
            }
        }
    }
}

// ============================================================================
// APPLY ZOOM
// ============================================================================
void FlightController::ApplyZoom(double factor) {
    glm::dvec3 pos = m_camera.GetPositionECEF();
    double r = glm::length(pos);
    double alt = r - GLOBE_RADIUS_KM;
    
    double newAlt = alt * factor;
    newAlt = std::clamp(newAlt, MIN_CAMERA_ALT_M / 1000.0, MAX_CAMERA_ALT_M / 1000.0);
    
    double newR = GLOBE_RADIUS_KM + newAlt;
    glm::dvec3 newPos = glm::normalize(pos) * newR;
    
    double lat = glm::degrees(std::asin(newPos.z / newR));
    double lon = glm::degrees(std::atan2(newPos.y, newPos.x));
    
    m_camera.SetLatLonAlt(lat, lon, newAlt * 1000.0);
}

// ============================================================================
// FLY TO
// ============================================================================
void FlightController::FlyToLocation(double lat, double lon, double altMeters,
                                      double heading, double tilt, double duration) {
    m_momentum.active = false;
    
    glm::dvec3 pos = m_camera.GetPositionECEF();
    double r = glm::length(pos);
    
    m_flyTo.startLat = glm::degrees(std::asin(pos.z / r));
    m_flyTo.startLon = glm::degrees(std::atan2(pos.y, pos.x));
    m_flyTo.startAlt = (r - GLOBE_RADIUS_KM) * 1000.0;
    m_flyTo.startHeading = m_camera.GetHeading();
    m_flyTo.startTilt = m_camera.GetTilt();
    
    m_flyTo.endLat = lat;
    m_flyTo.endLon = lon;
    m_flyTo.endAlt = altMeters;
    m_flyTo.endHeading = heading;
    m_flyTo.endTilt = tilt;
    
    m_flyTo.duration = duration;
    m_flyTo.startTime = 0.0; // Will be set on first Update
    m_flyTo.active = true;
}

void FlightController::StopAnimation() {
    m_flyTo.active = false;
    m_momentum.active = false;
}

// ============================================================================
// UPDATE (Call every frame)
// ============================================================================
void FlightController::Update(double dt, double currentTime) {
    // ========== FlyTo Animation ==========
    if (m_flyTo.active) {
        if (m_flyTo.startTime == 0.0) {
            m_flyTo.startTime = currentTime;
        }
        
        double t = (currentTime - m_flyTo.startTime) / m_flyTo.duration;
        if (t >= 1.0) {
            t = 1.0;
            m_flyTo.active = false;
        }
        
        double st = easing::CubicOut(t);
        
        double lat = glm::mix(m_flyTo.startLat, m_flyTo.endLat, st);
        double lon = glm::mix(m_flyTo.startLon, m_flyTo.endLon, st);
        double alt = glm::mix(m_flyTo.startAlt, m_flyTo.endAlt, st);
        
        // Handle heading wrap-around
        double h0 = m_flyTo.startHeading;
        double h1 = m_flyTo.endHeading;
        double hDiff = h1 - h0;
        if (hDiff > 180.0) h1 -= 360.0;
        if (hDiff < -180.0) h1 += 360.0;
        
        double heading = glm::mix(h0, h1, st);
        double tilt = glm::mix(m_flyTo.startTilt, m_flyTo.endTilt, st);
        
        m_camera.SetLatLonAlt(lat, lon, alt);
        m_camera.SetHeading(heading);
        m_camera.SetTilt(tilt);
        return;
    }
    
    // ========== Momentum (Pan or Orbit) ==========
    if (m_momentum.active && m_dragMode == DragMode::None) {
        double elapsed = currentTime - m_momentum.startTime;
        
        // Friction-based deceleration (more natural than exponential decay)
        // v(t) = v0 * e^(-friction * t)
        double decay = std::exp(-m_momentum.friction * elapsed);
        
        if (m_momentum.orbitMode) {
            // Orbit momentum - heading and tilt
            double hVel = m_momentum.headingVel * decay;
            double tVel = m_momentum.tiltVel * decay;
            
            double speed = std::sqrt(hVel * hVel + tVel * tVel);
            if (speed < 0.1) { // deg/s threshold
                m_momentum.active = false;
                m_momentum.headingVel = 0.0;
                m_momentum.tiltVel = 0.0;
            } else {
                // Apply orbit with decayed velocities
                double h = m_camera.GetHeading() + hVel * dt;
                double t = m_camera.GetTilt() + tVel * dt;
                
                while (h > 360.0) h -= 360.0;
                while (h < 0.0) h += 360.0;
                t = std::clamp(t, 0.1, 85.0);
                
                m_camera.SetHeading(h);
                m_camera.SetTilt(t);
            }
        } else {
            // Pan momentum - great circle rotation
            double vel = m_momentum.velocity * decay;
            
            if (std::abs(vel) < m_momentum.minVelocity) {
                m_momentum.active = false;
                m_momentum.velocity = 0.0;
            } else {
                double angle = vel * dt;
                ApplyPanRotation(m_momentum.axis, angle);
            }
        }
        
        // Maximum momentum duration (safety)
        if (elapsed > 3.0) {
            m_momentum.active = false;
        }
    }
}

} // namespace earth
