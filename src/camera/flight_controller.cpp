#include "flight_controller.h"
#include <algorithm>
#include <GLFW/glfw3.h>

namespace earth {

// ============================================================================
// CONSTRUCTOR
// ============================================================================
FlightController::FlightController(PerspectiveCamera& camera)
    : m_camera(camera) {}

// ============================================================================
// WINDOW & MODIFIERS
// ============================================================================
void FlightController::OnWindowResize(int width, int height) {
    m_windowW = width;
    m_windowH = height;
}

void FlightController::OnModifiers(bool shift, bool ctrl) {
    m_shiftDown = shift;
    m_ctrlDown = ctrl;
}

// ============================================================================
// STATUS
// ============================================================================
bool FlightController::IsMoving() const {
    return m_isDragging || m_throwActive || m_flyToActive;
}

bool FlightController::GetPivot(glm::dvec3& outPoint) const {
    if (m_hasOrbitPivot) {
        outPoint = m_orbitPivot;
        return true;
    }
    return false;
}

// ============================================================================
// RAY-GLOBE INTERSECTION
// ============================================================================
bool FlightController::RayGlobeIntersect(const glm::dvec3& origin,
                                          const glm::dvec3& dir,
                                          glm::dvec3& outHit) const {
    // Camera uses KM units (R = 6378.137)
    const double R = EARTH_RADIUS_KM;
    double a = glm::dot(dir, dir);
    double b = 2.0 * glm::dot(origin, dir);
    double c = glm::dot(origin, origin) - R * R;
    double disc = b * b - 4.0 * a * c;
    
    if (disc < 0.0) return false;
    
    double sqrtD = std::sqrt(disc);
    double t = (-b - sqrtD) / (2.0 * a);
    if (t < 0.0) t = (-b + sqrtD) / (2.0 * a);
    if (t < 0.0) return false;
    
    outHit = origin + dir * t;
    return true;
}

void FlightController::ScreenToRay(double x, double y,
                                    glm::dvec3& origin, glm::dvec3& dir) const {
    m_camera.GetRay(x, y, m_windowW, m_windowH, origin, dir);
    dir = glm::normalize(dir);
}

// ============================================================================
// CAMERA HELPERS
// ============================================================================
double FlightController::GetAltitudeM() const {
    // Camera ECEF is in KM units, convert to meters
    glm::dvec3 pos = m_camera.GetPositionECEF();
    double altKm = glm::length(pos) - EARTH_RADIUS_KM;
    return altKm * 1000.0;  // Convert km to meters
}

double FlightController::GetTiltFadeFactor() const {
    double altKm = GetAltitudeKm();
    if (altKm < BEGIN_TILT_FADE_KM) return 1.0;
    if (altKm > END_TILT_FADE_KM) return 0.0;
    return (END_TILT_FADE_KM - altKm) / (END_TILT_FADE_KM - BEGIN_TILT_FADE_KM);
}

double FlightController::GetMaxTilt() const {
    double factor = GetTiltFadeFactor();
    return HORIZON_TILT_THRESHOLD * factor + 45.0 * (1.0 - factor);
}

void FlightController::RotateCameraGreatCircle(const glm::dvec3& axis, double angle) {
    if (std::abs(angle) < 1e-12) return;
    
    glm::dvec3 normAxis = glm::normalize(axis);
    glm::dvec3 pos = m_camera.GetPositionECEF();
    
    glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), angle, normAxis);
    glm::dvec3 newPos = glm::dvec3(rot * glm::dvec4(pos, 1.0));
    
    double r = glm::length(newPos);
    double lat = glm::degrees(std::asin(std::clamp(newPos.z / r, -1.0, 1.0)));
    double lon = glm::degrees(std::atan2(newPos.y, newPos.x));
    double altM = (r - EARTH_RADIUS_KM) * 1000.0;  // Convert km to meters
    
    m_camera.SetLatLonAlt(lat, lon, altM);
}

bool FlightController::CheckDragThreshold(double dx, double dy) {
    if (m_dragThresholdExceeded) return true;
    
    m_accumulatedDrag += std::sqrt(dx * dx + dy * dy);
    if (m_accumulatedDrag > DRAG_THRESHOLD_PIXELS) {
        m_dragThresholdExceeded = true;
        return true;
    }
    return false;
}

double FlightController::CalculateGreatCircleDistance(double lat1, double lon1,
                                                       double lat2, double lon2) const {
    double phi1 = glm::radians(lat1);
    double phi2 = glm::radians(lat2);
    double dPhi = phi2 - phi1;
    double dLambda = glm::radians(lon2 - lon1);
    
    double a = std::sin(dPhi / 2.0) * std::sin(dPhi / 2.0) +
               std::cos(phi1) * std::cos(phi2) *
               std::sin(dLambda / 2.0) * std::sin(dLambda / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    
    return EARTH_RADIUS_KM * c;
}

// ============================================================================
// MOUSE DOWN (MapCameraManipulator::OnMouseDown)
// ============================================================================
void FlightController::OnMouseDown(int button, double x, double y, double time) {
    StopThrowAnimation();
    m_flyToActive = false;
    
    m_mouseX = x;
    m_mouseY = y;
    m_mouseDownX = x;
    m_mouseDownY = y;
    m_lastMoveTime = time;
    m_dragStartTime = time;
    m_dragThresholdExceeded = false;
    m_accumulatedDrag = 0.0;
    m_isDragging = true;
    
    // Reset velocities
    m_headingVelocity = 0.0;
    m_tiltVelocity = 0.0;
    m_zoomVelocity = 0.0;
    
    // Cast ray to globe
    glm::dvec3 origin, dir;
    ScreenToRay(x, y, origin, dir);
    
    glm::dvec3 hitPoint;
    bool hitGlobe = false;
    
    if (m_pickCallback) {
        hitGlobe = m_pickCallback(x, y, hitPoint);
    }
    if (!hitGlobe) {
        hitGlobe = RayGlobeIntersect(origin, dir, hitPoint);
    }
    
    // LEFT BUTTON
    if (button == 0) {
        if (m_shiftDown) {
            // Shift+Left = Orbit (OrbitAction)
            m_actionMode = ActionMode::Orbit;
            
            if (m_orbitPivotMode == OrbitPivotMode::ScreenCenter) {
                // Screen center pivot (Google Earth default)
                double centerX = m_windowW * 0.5;
                double centerY = m_windowH * 0.5;
                glm::dvec3 centerHitPoint;
                bool centerHit = m_pickCallback ? m_pickCallback(centerX, centerY, centerHitPoint) : false;
                if (!centerHit) {
                    glm::dvec3 org, d;
                    ScreenToRay(centerX, centerY, org, d);
                    centerHit = RayGlobeIntersect(org, d, centerHitPoint);
                }
                m_hasOrbitPivot = centerHit;
                m_orbitPivot = centerHit ? centerHitPoint : glm::dvec3(0.0);
            } else {
                // CursorPick mode: use cursor position (no snap)
                m_hasOrbitPivot = hitGlobe;
                m_orbitPivot = hitGlobe ? hitPoint : glm::dvec3(0.0);
            }
            m_orbitBaselineSet = false;
        } else {
            // Left = Pan (EarthPanRotateZoomAction)
            m_actionMode = ActionMode::Pan;
            m_hasPanAnchor = hitGlobe;
            m_panAnchorWorld = hitPoint;
            m_panAnchorDir = hitGlobe ? glm::normalize(hitPoint) : glm::dvec3(0.0);
            // Store terrain height (distance from sphere surface)
            m_panAnchorHeight = hitGlobe ? (glm::length(hitPoint) - EARTH_RADIUS_KM) : 0.0;
            m_panScreenX = x;
            m_panScreenY = y;
            m_lastPanTime = time;
            m_lastPanAngle = 0.0;
        }
    }
    // MIDDLE BUTTON = Orbit
    else if (button == 2) {
        m_actionMode = ActionMode::Orbit;
        
        if (m_orbitPivotMode == OrbitPivotMode::ScreenCenter) {
            // Screen center pivot (Google Earth default)
            double centerX = m_windowW * 0.5;
            double centerY = m_windowH * 0.5;
            glm::dvec3 centerHitPoint;
            bool centerHit = m_pickCallback ? m_pickCallback(centerX, centerY, centerHitPoint) : false;
            if (!centerHit) {
                glm::dvec3 org, d;
                ScreenToRay(centerX, centerY, org, d);
                centerHit = RayGlobeIntersect(org, d, centerHitPoint);
            }
            m_hasOrbitPivot = centerHit;
            m_orbitPivot = centerHit ? centerHitPoint : glm::dvec3(0.0);
        } else {
            // CursorPick mode: use cursor position (no snap)
            m_hasOrbitPivot = hitGlobe;
            m_orbitPivot = hitGlobe ? hitPoint : glm::dvec3(0.0);
        }
        m_orbitBaselineSet = false;
    }
    // RIGHT BUTTON = Zoom
    else if (button == 1) {
        m_actionMode = ActionMode::Zoom;
        m_zoomStartY = y;
        // Zoom target stored for potential future use
    }
}

// ============================================================================
// MOUSE UP (MapCameraManipulator::OnMouseUp)
// ============================================================================
void FlightController::OnMouseUp(int button, double time) {
    if (!m_isDragging) return;
    
    double dragDuration = time - m_dragStartTime;
    bool quickDrag = dragDuration < 0.4;
    
    // Start throw animation based on mode
    if (quickDrag && m_dragThresholdExceeded) {
        if (m_actionMode == ActionMode::Pan) {
            if (std::abs(m_lastPanAngle) > MIN_VELOCITY_THRESHOLD) {
                m_throwRotationAxis = m_lastPanAxis;
                m_throwAngularVelocity = m_lastPanAngle / std::max(time - m_lastPanTime, 0.001);
                m_throwAngularVelocity = std::clamp(m_throwAngularVelocity, -3.0, 3.0);
                StartThrowAnimation(ThrowAnimationType::Rotation, time);
            }
        }
        else if (m_actionMode == ActionMode::Orbit) {
            if (std::abs(m_headingVelocity) > 0.5 || std::abs(m_tiltVelocity) > 0.5) {
                m_throwHeadingVel = std::clamp(m_headingVelocity, -100.0, 100.0);
                m_throwTiltVel = std::clamp(m_tiltVelocity, -50.0, 50.0);
                // Store pivot data for pivot-centered throw
                m_throwHasOrbitPivot = m_hasOrbitPivot;
                m_throwOrbitPivot = m_orbitPivot;
                m_throwPivotToCam = m_camera.GetPositionECEF() - m_orbitPivot;
                // Store current forward for CursorPick throw
                glm::dvec3 fwd, up, right;
                m_camera.GetBasisVectors(fwd, up, right);
                m_throwForward = fwd;
                StartThrowAnimation(ThrowAnimationType::Arcball, time);
            }
        }
        else if (m_actionMode == ActionMode::Zoom) {
            // Zoom throw (ZoomThrowAnimation)
            if (std::abs(m_zoomVelocity) > 0.01) {
                m_throwZoomVel = std::clamp(m_zoomVelocity, -2.0, 2.0);
                StartThrowAnimation(ThrowAnimationType::Zoom, time);
            }
        }
    }
    
    m_isDragging = false;
    m_actionMode = ActionMode::None;
    m_hasPanAnchor = false;
    // Keep m_hasOrbitPivot during throw for gizmo visibility
    // It will be cleared when throw stops or on next mouse down
}

// ============================================================================
// MOUSE MOVE (MapCameraManipulator::OnMouseMove)
// ============================================================================
void FlightController::OnMouseMove(double x, double y, double time) {
    double dx = x - m_mouseX;
    double dy = y - m_mouseY;
    double dt = time - m_lastMoveTime;
    
    m_mouseX = x;
    m_mouseY = y;
    m_lastMoveTime = time;
    
    if (!m_isDragging || m_actionMode == ActionMode::None) return;
    if (dt < 0.0001) return;
    
    // Check drag threshold
    if (!CheckDragThreshold(dx, dy)) return;
    
    // ========== PAN (EarthPanRotateZoomAction) ==========
    if (m_actionMode == ActionMode::Pan) {
        if (m_hasPanAnchor) {
            // HEIGHT-AWARE ANCHOR ALGORITHM
            // Goal: Keep the anchor point exactly under the cursor at all times
            
            // Get current cursor ray
            glm::dvec3 origin, dir;
            ScreenToRay(x, y, origin, dir);
            
            // Prefer terrain-aware pick (Google Earth parity) when available.
            // The constant-radius sphere approximation can feel "sticky/slow" at low altitude
            // because the cursor ray intersects at a different lat/lon than the actual terrain surface.
            glm::dvec3 targetDir;
            bool hasTarget = false;
            
            glm::dvec3 pickedHit;
            if (m_pickCallback && m_pickCallback(x, y, pickedHit)) {
                targetDir = glm::normalize(pickedHit);
                hasTarget = true;
            }
            
            if (!hasTarget) {
                // Fallback: intersect a constant-radius sphere at the anchor's height.
                double targetRadius = EARTH_RADIUS_KM + m_panAnchorHeight;
                double a = glm::dot(dir, dir);
                double b = 2.0 * glm::dot(origin, dir);
                double c = glm::dot(origin, origin) - targetRadius * targetRadius;
                double disc = b * b - 4.0 * a * c;

                if (disc >= 0.0) {
                    double sqrtD = std::sqrt(disc);
                    double t = (-b - sqrtD) / (2.0 * a);
                    if (t < 0.0) t = (-b + sqrtD) / (2.0 * a);
                    if (t > 0.0) {
                        glm::dvec3 targetPoint = origin + dir * t;
                        targetDir = glm::normalize(targetPoint);
                        hasTarget = true;
                    }
                }
            }

            if (!hasTarget) {
                // Last resort: base globe intersection (sky drag will fall back elsewhere).
                glm::dvec3 currentHit;
                if (RayGlobeIntersect(origin, dir, currentHit)) {
                    targetDir = glm::normalize(currentHit);
                    hasTarget = true;
                }
            }

            if (!hasTarget) {
                // Cursor ray is off-globe (sky drag). Switch to screen-space pan for the rest of
                // this drag so navigation doesn't stall near the horizon.
                m_hasPanAnchor = false;

                // Use an altitude floor so close-to-ground sky-drag does not become unusably slow.
                double altKm = std::max(GetAltitudeKm(), 1.0);
                double sensitivity = 0.0002 * m_navSpeed * altKm;

                glm::dvec3 camPos = m_camera.GetPositionECEF();
                glm::dvec3 up = glm::normalize(camPos);
                glm::dvec3 fwd, camUp, right;
                m_camera.GetBasisVectors(fwd, camUp, right);

                glm::dvec3 moveDir = -right * dx + camUp * dy;
                moveDir = moveDir - up * glm::dot(moveDir, up);

                if (glm::length(moveDir) > 1e-8) {
                    glm::dvec3 axis = glm::normalize(glm::cross(camPos, moveDir));
                    double angle = glm::length(moveDir) * sensitivity;
                    RotateCameraGreatCircle(axis, angle);

                    m_lastPanAxis = axis;
                    m_lastPanAngle = angle;
                    m_lastPanTime = time;
                }

                // Update screen position
                m_panScreenX = x;
                m_panScreenY = y;
                return;
            }
            
            if (hasTarget) {
                // Calculate rotation to move anchor to target position
                // IMPORTANT: At low altitude, the great-circle angle between two nearby
                // surface directions can be extremely small (<< 1e-6 rad). Using an
                // over-aggressive dot() threshold causes pan to "stall" and feel slow.
                //
                // Use a stable atan2 formulation instead of acos(dot) with a coarse cutoff.
                double cosAngle = std::clamp(glm::dot(m_panAnchorDir, targetDir), -1.0, 1.0);
                glm::dvec3 axis = glm::cross(targetDir, m_panAnchorDir);
                double axisLen = glm::length(axis);
                
                if (axisLen > 1e-12) {
                    axis /= axisLen;
                    double angle = std::atan2(axisLen, cosAngle);
                        
                        // Apply rotation to camera
                        RotateCameraGreatCircle(axis, angle);
                        
                        // Track for momentum
                        m_lastPanAxis = axis;
                        m_lastPanAngle = angle;
                        m_lastPanTime = time;
                        
                        // UPDATE ANCHOR DIRECTION after rotation
                        // The anchor should now be under the current cursor position
                        // Recalculate from current cursor to verify stability
                        glm::dvec3 newAnchorHit;
                        if (m_pickCallback && m_pickCallback(x, y, newAnchorHit)) {
                            m_panAnchorWorld = newAnchorHit;
                            m_panAnchorDir = glm::normalize(newAnchorHit);
                            m_panAnchorHeight = glm::length(newAnchorHit) - EARTH_RADIUS_KM;
                        } else {
                            glm::dvec3 newOrigin, newDir;
                            ScreenToRay(x, y, newOrigin, newDir);
                            double targetRadius = EARTH_RADIUS_KM + m_panAnchorHeight;
                            double a = glm::dot(newDir, newDir);
                            double b = 2.0 * glm::dot(newOrigin, newDir);
                            double c = glm::dot(newOrigin, newOrigin) - targetRadius * targetRadius;
                            double disc = b * b - 4.0 * a * c;
                            if (disc >= 0.0) {
                                double sqrtD = std::sqrt(disc);
                                double t = (-b - sqrtD) / (2.0 * a);
                                if (t < 0.0) t = (-b + sqrtD) / (2.0 * a);
                                if (t > 0.0) {
                                    glm::dvec3 newAnchor = newOrigin + newDir * t;
                                    m_panAnchorWorld = newAnchor;
                                    m_panAnchorDir = glm::normalize(newAnchor);
                                }
                            }
                        }
                }
            }
            
            // Update screen position
            m_panScreenX = x;
            m_panScreenY = y;
            
        } else {
            // Fallback: screen-space pan (for sky drag)
            // Use an altitude floor so close-to-ground sky-drag does not become unusably slow.
            double altKm = std::max(GetAltitudeKm(), 1.0);
            double sensitivity = 0.0002 * m_navSpeed * altKm;
            
            glm::dvec3 camPos = m_camera.GetPositionECEF();
            glm::dvec3 up = glm::normalize(camPos);
            glm::dvec3 fwd, camUp, right;
            m_camera.GetBasisVectors(fwd, camUp, right);
            
            glm::dvec3 moveDir = -right * dx + camUp * dy;
            moveDir = moveDir - up * glm::dot(moveDir, up);
            
            if (glm::length(moveDir) > 1e-8) {
                glm::dvec3 axis = glm::normalize(glm::cross(camPos, moveDir));
                double angle = glm::length(moveDir) * sensitivity;
                RotateCameraGreatCircle(axis, angle);
                
                m_lastPanAxis = axis;
                m_lastPanAngle = angle;
                m_lastPanTime = time;
            }
        }
    }
    
    // ========== ORBIT (OrbitAction - Pivot-Centered Rotation) ==========
    else if (m_actionMode == ActionMode::Orbit) {
        // Reset baseline on first move after drag threshold (prevents flick)
        if (!m_orbitBaselineSet) {
            m_orbitStartX = x;
            m_orbitStartY = y;
            m_orbitStartPivotToCam = m_camera.GetPositionECEF() - m_orbitPivot;
            m_orbitStartHeading = m_camera.GetHeading();
            m_orbitStartTilt = m_camera.GetTilt();
            // Store baseline forward for CursorPick mode (world-space)
            glm::dvec3 fwd, up, right;
            m_camera.GetBasisVectors(fwd, up, right);
            m_orbitStartForward = fwd;
            m_orbitBaselineSet = true;
            // Zero velocities on baseline frame to prevent spike
            m_headingVelocity = 0.0;
            m_tiltVelocity = 0.0;
            return;  // Skip this frame - no rotation, no velocity sampling
        }
        
        double totalDx = x - m_orbitStartX;
        double totalDy = y - m_orbitStartY;
        
        double deltaHeading = -totalDx * HEADING_SENSITIVITY * m_navSpeed;
        double deltaTilt = totalDy * TILT_SENSITIVITY * m_navSpeed;
        
        if (m_lockNorth) deltaHeading = 0.0;
        
        // Track velocity for momentum (respect lockNorth)
        m_headingVelocity = m_lockNorth ? 0.0 : -dx * HEADING_SENSITIVITY * m_navSpeed / dt;
        m_tiltVelocity = dy * TILT_SENSITIVITY * m_navSpeed / dt;
        
        // PIVOT-CENTERED ROTATION (Google Earth style)
        // Apply TOTAL rotation to BASELINE pivot-to-cam to prevent overshoot
        if (m_hasOrbitPivot) {
            // Get pivot local frame (from pivot position)
            glm::dvec3 pivotUp = glm::normalize(m_orbitPivot);
            glm::dvec3 pivotEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), pivotUp));
            if (glm::length(pivotEast) < 0.1) pivotEast = glm::dvec3(1, 0, 0);

            // Apply TOTAL heading first, then compute a tilt axis from the heading-rotated view.
            // This avoids "vertical drift" artifacts when combining total heading+tilt.
            double headingRad = glm::radians(deltaHeading);
            glm::dmat4 headingRot = glm::rotate(glm::dmat4(1.0), headingRad, pivotUp);
            glm::dvec3 pivotToCamAfterHeading =
                glm::dvec3(headingRot * glm::dvec4(m_orbitStartPivotToCam, 0.0));

            glm::dvec3 viewDirAfterHeading = glm::normalize(-pivotToCamAfterHeading);
            glm::dvec3 tiltAxis = glm::normalize(glm::cross(pivotUp, viewDirAfterHeading));
            if (glm::length(tiltAxis) < 0.1) tiltAxis = pivotEast;

            double tiltRad = glm::radians(deltaTilt);
            glm::dmat4 tiltRot = glm::rotate(glm::dmat4(1.0), tiltRad, tiltAxis);

            glm::dvec3 newPivotToCam =
                glm::dvec3(tiltRot * glm::dvec4(pivotToCamAfterHeading, 0.0));
            glm::dvec3 newCamPos = m_orbitPivot + newPivotToCam;
            
            // Clamp altitude (camera uses KM units)
            double newR = glm::length(newCamPos);
            double minR = EARTH_RADIUS_KM + CAMERA_MIN_ALTITUDE_M / 1000.0;
            if (newR < minR) newCamPos = glm::normalize(newCamPos) * minR;
            
            // Update camera position
            double r = glm::length(newCamPos);
            double lat = glm::degrees(std::asin(std::clamp(newCamPos.z / r, -1.0, 1.0)));
            double lon = glm::degrees(std::atan2(newCamPos.y, newCamPos.x));
            double altM = (r - EARTH_RADIUS_KM) * 1000.0;
            
            m_camera.SetLatLonAlt(lat, lon, altM);
            
            // Heading/Tilt update depends on pivot mode
            if (m_orbitPivotMode == OrbitPivotMode::ScreenCenter) {
                // ScreenCenter: derive heading/tilt from camera->pivot (pivot stays centered)
                glm::dvec3 finalCamPos = m_camera.GetPositionECEF();
                glm::dvec3 camToPivot = m_orbitPivot - finalCamPos;
                glm::dvec3 camUp = glm::normalize(finalCamPos);
                
                glm::dvec3 camToPivotDir = glm::normalize(camToPivot);
                double dotUp = glm::dot(camUp, camToPivotDir);
                double newTilt = glm::degrees(std::acos(std::clamp(-dotUp, -1.0, 1.0)));
                newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
                
                glm::dvec3 camEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), camUp));
                if (glm::length(camEast) < 0.1) camEast = glm::dvec3(1, 0, 0);
                glm::dvec3 camNorth = glm::cross(camUp, camEast);
                
                glm::dvec3 horizDir = camToPivotDir - camUp * glm::dot(camToPivotDir, camUp);
                if (glm::length(horizDir) > 1e-6) {
                    horizDir = glm::normalize(horizDir);
                    double hRad = std::atan2(glm::dot(horizDir, camEast), glm::dot(horizDir, camNorth));
                    double newHeading = glm::degrees(hRad);
                    while (newHeading < 0.0) newHeading += 360.0;
                    while (newHeading >= 360.0) newHeading -= 360.0;
                    if (!m_lockNorth) m_camera.SetHeading(newHeading);
                }
                m_camera.SetTilt(newTilt);
            } else {
                // CursorPick: apply SAME rotation to forward vector, then derive heading/tilt
                // This prevents drift caused by ENU frame change as camera moves
                glm::dvec3 rotatedForward = glm::normalize(glm::dvec3(tiltRot * headingRot * glm::dvec4(m_orbitStartForward, 0.0)));
                
                // Derive heading/tilt from rotated forward in new camera's local frame
                glm::dvec3 newCamUp = glm::normalize(m_camera.GetPositionECEF());
                glm::dvec3 newCamEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), newCamUp));
                if (glm::length(newCamEast) < 0.1) newCamEast = glm::dvec3(1, 0, 0);
                glm::dvec3 newCamNorth = glm::cross(newCamUp, newCamEast);
                
                // Tilt = angle between forward and horizontal plane
                double dotUp = glm::dot(rotatedForward, newCamUp);
                double newTilt = glm::degrees(std::acos(std::clamp(-dotUp, -1.0, 1.0)));
                newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
                
                // Heading = azimuth of forward in horizontal plane
                glm::dvec3 horizFwd = rotatedForward - newCamUp * dotUp;
                if (glm::length(horizFwd) > 1e-6) {
                    horizFwd = glm::normalize(horizFwd);
                    double hRad = std::atan2(glm::dot(horizFwd, newCamEast), glm::dot(horizFwd, newCamNorth));
                    double newHeading = glm::degrees(hRad);
                    while (newHeading < 0.0) newHeading += 360.0;
                    while (newHeading >= 360.0) newHeading -= 360.0;
                    if (!m_lockNorth) m_camera.SetHeading(newHeading);
                }
                m_camera.SetTilt(newTilt);
            }
        } else {
            // No pivot - just update heading/tilt from deltas
            double newHeading = m_orbitStartHeading + deltaHeading;
            double newTilt = m_orbitStartTilt + deltaTilt;
            
            while (newHeading > 360.0) newHeading -= 360.0;
            while (newHeading < 0.0) newHeading += 360.0;
            newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
            
            m_camera.SetHeading(newHeading);
            m_camera.SetTilt(newTilt);
        }
    }
    
    // ========== ZOOM (Right Drag) ==========
    else if (m_actionMode == ActionMode::Zoom) {
        double factor = 1.0 + (y - m_zoomStartY) * ZOOM_DRAG_SENSITIVITY;
        factor = std::clamp(factor, 0.9, 1.1);
        
        double altM = GetAltitudeM();
        double newAltM = altM * factor;
        newAltM = std::clamp(newAltM, CAMERA_MIN_ALTITUDE_M, CAMERA_MAX_ALTITUDE_M);
        
        double lat, lon, oldAlt;
        m_camera.GetLatLonAlt(lat, lon, oldAlt);
        m_camera.SetLatLonAlt(lat, lon, newAltM);
        
        m_zoomStartY = y;
        m_zoomVelocity = (newAltM - altM) / altM / dt;
    }
}

// ============================================================================
// SCROLL (MapCameraManipulator::OnMouseWheel)
// ============================================================================
void FlightController::OnScroll(double xoffset, double yoffset) {
    StopThrowAnimation();
    m_flyToActive = false;
    
    // Shift+Scroll = Tilt
    if (m_shiftDown) {
        double deltaTilt = yoffset * 5.0;
        double newTilt = m_camera.GetTilt() - deltaTilt;
        newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
        m_camera.SetTilt(newTilt);
        return;
    }
    
    // Logarithmic zoom (pre-clamp to prevent log(0) or log(negative))
    double altM = std::max(GetAltitudeM(), CAMERA_MIN_ALTITUDE_M);
    double logAlt = std::log(altM);
    double logStep = yoffset * ZOOM_SCROLL_STEP;
    double newAltM = std::exp(logAlt - logStep);
    newAltM = std::clamp(newAltM, CAMERA_MIN_ALTITUDE_M, CAMERA_MAX_ALTITUDE_M);
    
    // Point-stable zoom (zoom toward/away from cursor - BOTH directions)
    if (m_zoomToCursor) {
        glm::dvec3 origin, dir;
        ScreenToRay(m_mouseX, m_mouseY, origin, dir);
        
        glm::dvec3 target;
        bool hasTarget = false;
        
        if (m_pickCallback) {
            hasTarget = m_pickCallback(m_mouseX, m_mouseY, target);
        }
        if (!hasTarget) {
            hasTarget = RayGlobeIntersect(origin, dir, target);
        }
        
        if (hasTarget) {
            glm::dvec3 camPos = m_camera.GetPositionECEF();
            glm::dvec3 camDir = glm::normalize(camPos);
            glm::dvec3 targetDir = glm::normalize(target);
            
            double dot = std::clamp(glm::dot(camDir, targetDir), -1.0, 1.0);
            double theta = std::acos(dot);
            
            if (theta > 1e-6) {
                double zoomRatio = newAltM / altM;
                // For zoom in (yoffset > 0): move toward target
                // For zoom out (yoffset < 0): move away from target
                double moveAngle = theta * (1.0 - zoomRatio) * 0.4;
                
                glm::dvec3 axis = glm::cross(camDir, targetDir);
                if (glm::length(axis) > 1e-8) {
                    axis = glm::normalize(axis);
                    RotateCameraGreatCircle(axis, moveAngle);
                }
            }
        }
    }
    
    double lat, lon, oldAlt;
    m_camera.GetLatLonAlt(lat, lon, oldAlt);
    m_camera.SetLatLonAlt(lat, lon, newAltM);
}

// ============================================================================
// DOUBLE CLICK
// ============================================================================
void FlightController::OnDoubleClick(double x, double y) {
    StopThrowAnimation();
    m_flyToActive = false;
    
    glm::dvec3 origin, dir;
    ScreenToRay(x, y, origin, dir);
    
    glm::dvec3 hitPoint;
    bool hit = false;
    
    if (m_pickCallback) {
        hit = m_pickCallback(x, y, hitPoint);
    }
    if (!hit) {
        hit = RayGlobeIntersect(origin, dir, hitPoint);
    }
    
    if (hit) {
        double r = glm::length(hitPoint);
        double lat = glm::degrees(std::asin(hitPoint.z / r));
        double lon = glm::degrees(std::atan2(hitPoint.y, hitPoint.x));
        double newAltM = std::max(GetAltitudeM() * 0.25, CAMERA_MIN_ALTITUDE_M);
        
        FlyTo(lat, lon, newAltM, m_camera.GetHeading(), m_camera.GetTilt(), 1.5);
    }
}

// ============================================================================
// KEYBOARD
// ============================================================================
void FlightController::OnKeyDown(int key) {
    double altM = GetAltitudeM();
    double moveAngle = altM * 0.00001 * m_navSpeed;
    
    glm::dvec3 pos = m_camera.GetPositionECEF();
    glm::dvec3 up = glm::normalize(pos);
    glm::dvec3 fwd, camUp, right;
    m_camera.GetBasisVectors(fwd, camUp, right);
    
    glm::dvec3 fwdH = fwd - up * glm::dot(fwd, up);
    if (glm::length(fwdH) > 1e-6) fwdH = glm::normalize(fwdH);
    glm::dvec3 rightH = glm::cross(fwdH, up);
    
    glm::dvec3 moveDir{0.0};
    
    switch (key) {
        case GLFW_KEY_LEFT:  case GLFW_KEY_A: moveDir = -rightH; break;
        case GLFW_KEY_RIGHT: case GLFW_KEY_D: moveDir = rightH; break;
        case GLFW_KEY_UP:    case GLFW_KEY_W: moveDir = fwdH; break;
        case GLFW_KEY_DOWN:  case GLFW_KEY_S: moveDir = -fwdH; break;
        case GLFW_KEY_Q: case GLFW_KEY_PAGE_UP: {
            double newAlt = altM * 0.9;
            double lat, lon, oldAlt;
            m_camera.GetLatLonAlt(lat, lon, oldAlt);
            m_camera.SetLatLonAlt(lat, lon, std::max(newAlt, CAMERA_MIN_ALTITUDE_M));
            return;
        }
        case GLFW_KEY_E: case GLFW_KEY_PAGE_DOWN: {
            double newAlt = altM * 1.1;
            double lat, lon, oldAlt;
            m_camera.GetLatLonAlt(lat, lon, oldAlt);
            m_camera.SetLatLonAlt(lat, lon, std::min(newAlt, CAMERA_MAX_ALTITUDE_M));
            return;
        }
    }
    
    if (glm::length(moveDir) > 0.001) {
        glm::dvec3 axis = glm::normalize(glm::cross(pos, moveDir));
        RotateCameraGreatCircle(axis, moveAngle);
    }
}

void FlightController::OnKeyUp(int key) {}

// ============================================================================
// THROW ANIMATION (DampedVelocityAction)
// ============================================================================
void FlightController::StartThrowAnimation(ThrowAnimationType type, double time) {
    m_throwActive = true;
    m_throwStartTime = time;
    m_throwType = type;
}

void FlightController::StopThrowAnimation() {
    m_throwActive = false;
    m_throwType = ThrowAnimationType::None;
    m_hasOrbitPivot = false;  // Clear pivot when throw ends
}

void FlightController::UpdateThrowAnimation(double dt, double time) {
    if (!m_throwActive) return;
    
    double elapsed = time - m_throwStartTime;
    if (elapsed > MOMENTUM_TIMEOUT_SEC) {
        StopThrowAnimation();
        return;
    }
    
    double decay = std::exp(-DAMPING_COEFFICIENT * elapsed);
    
    switch (m_throwType) {
        case ThrowAnimationType::Rotation: {
            double vel = m_throwAngularVelocity * decay;
            if (std::abs(vel) < MIN_VELOCITY_THRESHOLD) {
                StopThrowAnimation();
            } else {
                RotateCameraGreatCircle(m_throwRotationAxis, vel * dt);
            }
            break;
        }
        
        case ThrowAnimationType::Arcball: {
            // Respect lockNorth during throw
            double headingVel = m_lockNorth ? 0.0 : m_throwHeadingVel * decay;
            double tiltVel = m_throwTiltVel * decay;
            
            if (std::abs(headingVel) < 0.1 && std::abs(tiltVel) < 0.1) {
                StopThrowAnimation();
            } else {
                if (m_throwHasOrbitPivot) {
                    // PIVOT-CENTERED ORBIT THROW
                    glm::dvec3 pivotUp = glm::normalize(m_throwOrbitPivot);
                    glm::dvec3 pivotEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), pivotUp));
                    if (glm::length(pivotEast) < 0.1) pivotEast = glm::dvec3(1, 0, 0);
                    
                    // Incremental rotation (skip heading if lockNorth)
                    double headingRad = m_lockNorth ? 0.0 : glm::radians(headingVel * dt);
                    double tiltRad = glm::radians(tiltVel * dt);
                    
                    glm::dmat4 headingRot = glm::rotate(glm::dmat4(1.0), headingRad, pivotUp);

                    // Apply heading first, then compute tilt axis from the heading-rotated view direction.
                    glm::dvec3 pivotToCamAfterHeading =
                        glm::dvec3(headingRot * glm::dvec4(m_throwPivotToCam, 0.0));
                    glm::dvec3 viewDirAfterHeading = glm::normalize(-pivotToCamAfterHeading);
                    glm::dvec3 tiltAxis = glm::normalize(glm::cross(pivotUp, viewDirAfterHeading));
                    if (glm::length(tiltAxis) < 0.1) tiltAxis = pivotEast;
                    glm::dmat4 tiltRot = glm::rotate(glm::dmat4(1.0), tiltRad, tiltAxis);

                    // Update pivot-to-cam
                    m_throwPivotToCam =
                        glm::dvec3(tiltRot * glm::dvec4(pivotToCamAfterHeading, 0.0));
                    glm::dvec3 newCamPos = m_throwOrbitPivot + m_throwPivotToCam;
                    
                    // Clamp altitude
                    double newR = glm::length(newCamPos);
                    double minR = EARTH_RADIUS_KM + CAMERA_MIN_ALTITUDE_M / 1000.0;
                    if (newR < minR) newCamPos = glm::normalize(newCamPos) * minR;
                    
                    // Update camera position
                    double r = glm::length(newCamPos);
                    double lat = glm::degrees(std::asin(std::clamp(newCamPos.z / r, -1.0, 1.0)));
                    double lon = glm::degrees(std::atan2(newCamPos.y, newCamPos.x));
                    double altM = (r - EARTH_RADIUS_KM) * 1000.0;
                    m_camera.SetLatLonAlt(lat, lon, altM);
                    
                    // Heading/Tilt update depends on pivot mode
                    if (m_orbitPivotMode == OrbitPivotMode::ScreenCenter) {
                        // ScreenCenter: derive heading/tilt from camera->pivot
                        glm::dvec3 camUp = glm::normalize(newCamPos);
                        glm::dvec3 camToPivot = glm::normalize(m_throwOrbitPivot - newCamPos);
                        double dotUp = glm::dot(camUp, camToPivot);
                        double newTilt = glm::degrees(std::acos(std::clamp(-dotUp, -1.0, 1.0)));
                        newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
                        
                        glm::dvec3 camEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), camUp));
                        if (glm::length(camEast) < 0.1) camEast = glm::dvec3(1, 0, 0);
                        glm::dvec3 camNorth = glm::cross(camUp, camEast);
                        glm::dvec3 horizDir = camToPivot - camUp * glm::dot(camToPivot, camUp);
                        if (glm::length(horizDir) > 1e-6 && !m_lockNorth) {
                            horizDir = glm::normalize(horizDir);
                            double hRad = std::atan2(glm::dot(horizDir, camEast), glm::dot(horizDir, camNorth));
                            double newHeading = glm::degrees(hRad);
                            while (newHeading < 0.0) newHeading += 360.0;
                            m_camera.SetHeading(newHeading);
                        }
                        m_camera.SetTilt(newTilt);
                    } else {
                        // CursorPick: apply SAME rotation to forward, then derive heading/tilt
                        // This prevents drift caused by ENU frame change
                        m_throwForward = glm::normalize(glm::dvec3(tiltRot * headingRot * glm::dvec4(m_throwForward, 0.0)));
                        
                        glm::dvec3 newCamUp = glm::normalize(newCamPos);
                        glm::dvec3 newCamEast = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), newCamUp));
                        if (glm::length(newCamEast) < 0.1) newCamEast = glm::dvec3(1, 0, 0);
                        glm::dvec3 newCamNorth = glm::cross(newCamUp, newCamEast);
                        
                        double dotUp = glm::dot(m_throwForward, newCamUp);
                        double newTilt = glm::degrees(std::acos(std::clamp(-dotUp, -1.0, 1.0)));
                        newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
                        
                        glm::dvec3 horizFwd = m_throwForward - newCamUp * dotUp;
                        if (glm::length(horizFwd) > 1e-6 && !m_lockNorth) {
                            horizFwd = glm::normalize(horizFwd);
                            double hRad = std::atan2(glm::dot(horizFwd, newCamEast), glm::dot(horizFwd, newCamNorth));
                            double newHeading = glm::degrees(hRad);
                            while (newHeading < 0.0) newHeading += 360.0;
                            while (newHeading >= 360.0) newHeading -= 360.0;
                            m_camera.SetHeading(newHeading);
                        }
                        m_camera.SetTilt(newTilt);
                    }
                } else {
                    // No pivot - just heading/tilt (lockNorth already applied to headingVel)
                    double newTilt = m_camera.GetTilt() + tiltVel * dt;
                    newTilt = std::clamp(newTilt, 0.1, GetMaxTilt());
                    m_camera.SetTilt(newTilt);
                    
                    if (!m_lockNorth) {
                        double newHeading = m_camera.GetHeading() + headingVel * dt;
                        while (newHeading > 360.0) newHeading -= 360.0;
                        while (newHeading < 0.0) newHeading += 360.0;
                        m_camera.SetHeading(newHeading);
                    }
                }
            }
            break;
        }
        
        case ThrowAnimationType::Zoom: {
            double vel = m_throwZoomVel * decay;
            if (std::abs(vel) < 0.01) {
                StopThrowAnimation();
            } else {
                double altM = GetAltitudeM();
                // Positive velocity = zoom out (altitude increases)
                // Negative velocity = zoom in (altitude decreases)
                double newAltM = altM * std::exp(vel * dt);
                newAltM = std::clamp(newAltM, CAMERA_MIN_ALTITUDE_M, CAMERA_MAX_ALTITUDE_M);
                
                double lat, lon, oldAlt;
                m_camera.GetLatLonAlt(lat, lon, oldAlt);
                m_camera.SetLatLonAlt(lat, lon, newAltM);
            }
            break;
        }
        
        default:
            StopThrowAnimation();
            break;
    }
}

// ============================================================================
// FLY TO (CameraManager::FlyCameraTo)
// ============================================================================
void FlightController::FlyTo(double lat, double lon, double altM,
                              double heading, double tilt, double duration,
                              TrajectoryMode mode) {
    StopThrowAnimation();
    
    glm::dvec3 pos = m_camera.GetPositionECEF();
    double r = glm::length(pos);
    
    m_flyToFromLat = glm::degrees(std::asin(pos.z / r));
    m_flyToFromLon = glm::degrees(std::atan2(pos.y, pos.x));
    m_flyToFromAlt = (r - EARTH_RADIUS_KM) * 1000.0;
    m_flyToFromHeading = m_camera.GetHeading();
    m_flyToFromTilt = m_camera.GetTilt();
    
    m_flyToToLat = lat;
    m_flyToToLon = lon;
    m_flyToToAlt = altM;
    m_flyToToHeading = heading;
    m_flyToToTilt = tilt;
    
    m_flyToDistance = CalculateGreatCircleDistance(m_flyToFromLat, m_flyToFromLon, lat, lon);
    m_flyToMode = mode;
    
    // Calculate peak altitude for parabolic trajectory
    if (mode == TrajectoryMode::Parabolic && m_flyToDistance > 50.0) {
        double maxAlt = std::max(m_flyToFromAlt, altM);
        m_flyToPeakAlt = maxAlt + std::min(m_flyToDistance * 500.0, 5000000.0);
    } else {
        m_flyToPeakAlt = 0.0;
    }
    
    // Duration guard - prevent division by zero
    m_flyToDuration = std::max(duration, 0.1);
    m_flyToStartTime = 0.0;
    m_flyToActive = true;
}

void FlightController::SetCameraTo(double lat, double lon, double altM,
                                    double heading, double tilt) {
    StopThrowAnimation();
    m_flyToActive = false;
    
    m_camera.SetLatLonAlt(lat, lon, altM);
    m_camera.SetHeading(heading);
    m_camera.SetTilt(tilt);
}

void FlightController::StopAnimation() {
    StopThrowAnimation();
    m_flyToActive = false;
}

void FlightController::UpdateFlyToAnimation(double time) {
    if (!m_flyToActive) return;
    
    if (m_flyToStartTime == 0.0) {
        m_flyToStartTime = time;
    }
    
    double t = (time - m_flyToStartTime) / m_flyToDuration;
    if (t >= 1.0) {
        t = 1.0;
        m_flyToActive = false;
    }
    
    double st = EaseInOutCubic(t);
    
    // Interpolate lat/lon
    double lat = glm::mix(m_flyToFromLat, m_flyToToLat, st);
    
    double lonDiff = m_flyToToLon - m_flyToFromLon;
    if (lonDiff > 180.0) lonDiff -= 360.0;
    if (lonDiff < -180.0) lonDiff += 360.0;
    double lon = m_flyToFromLon + lonDiff * st;
    
    // Altitude with parabolic arc
    double alt;
    if (m_flyToMode == TrajectoryMode::Parabolic && m_flyToPeakAlt > 0.0) {
        double arcT = 4.0 * st * (1.0 - st);
        double baseAlt = glm::mix(m_flyToFromAlt, m_flyToToAlt, st);
        alt = baseAlt + (m_flyToPeakAlt - baseAlt) * arcT;
    } else {
        alt = glm::mix(m_flyToFromAlt, m_flyToToAlt, st);
    }
    
    // Heading with wrap
    double h0 = m_flyToFromHeading;
    double h1 = m_flyToToHeading;
    double hDiff = h1 - h0;
    if (hDiff > 180.0) h1 -= 360.0;
    if (hDiff < -180.0) h1 += 360.0;
    double heading = glm::mix(h0, h1, st);
    
    double tilt = glm::mix(m_flyToFromTilt, m_flyToToTilt, st);
    
    m_camera.SetLatLonAlt(lat, lon, alt);
    m_camera.SetHeading(heading);
    m_camera.SetTilt(tilt);
}

// ============================================================================
// UPDATE (Called every frame)
// ============================================================================
void FlightController::Update(double dt, double currentTime) {
    if (m_flyToActive) {
        UpdateFlyToAnimation(currentTime);
        return;
    }
    
    if (m_throwActive && !m_isDragging) {
        UpdateThrowAnimation(dt, currentTime);
    }
}

} // namespace earth
