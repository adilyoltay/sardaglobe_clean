#include "earth_camera.h"
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_access.hpp>

namespace earth {

PerspectiveCamera::PerspectiveCamera() {
    m_pos_ecef = LatLonAltToECEF(m_lat, m_lon, m_alt);
    m_view_dirty = true;
    m_proj_dirty = true;
}

void PerspectiveCamera::SetLatLonAlt(double lat, double lon, double alt) {
    m_lat = lat;
    m_lon = lon;
    m_alt = alt;
    m_pos_ecef = LatLonAltToECEF(m_lat, m_lon, m_alt);
    m_view_dirty = true;
}

void PerspectiveCamera::GetLatLonAlt(double& lat, double& lon, double& alt) const {
    lat = m_lat;
    lon = m_lon;
    alt = m_alt;
}

void PerspectiveCamera::SetHeading(double heading_deg) {
    m_heading = heading_deg;
    m_view_dirty = true;
}

double PerspectiveCamera::GetHeading() const {
    return m_heading;
}

void PerspectiveCamera::SetTilt(double tilt_deg) {
    m_tilt = std::clamp(tilt_deg, 0.0, 180.0); // Allow looking straight up/down
    m_view_dirty = true;
}

double PerspectiveCamera::GetTilt() const {
    return m_tilt;
}

void PerspectiveCamera::SetRoll(double roll_deg) {
    m_roll = roll_deg;
    m_view_dirty = true;
}

double PerspectiveCamera::GetRoll() const {
    return m_roll;
}

void PerspectiveCamera::SetFov(double fov_deg) {
    m_fov = fov_deg;
    m_proj_dirty = true;
}

double PerspectiveCamera::GetFov() const {
    return m_fov;
}

void PerspectiveCamera::SetAspectRatio(double aspect) {
    m_aspect = aspect;
    m_proj_dirty = true;
}

void PerspectiveCamera::SetNearFar(double near_plane, double far_plane) {
    m_near = near_plane;
    m_far = far_plane;
    m_proj_dirty = true;
}

void PerspectiveCamera::SetReverseZEnabled(bool enabled) {
    if (m_reverseZ != enabled) {
        m_reverseZ = enabled;
        m_proj_dirty = true;
    }
}

void PerspectiveCamera::SetPositionECEF(const glm::dvec3& pos) {
    m_pos_ecef = pos;
    // Inverse conversion ECEF -> LatLon would go here if needed strictly
    // For now, we mainly use ECEF for view matrix
    m_view_dirty = true;
}

glm::dvec3 PerspectiveCamera::GetPositionECEF() const {
    if (m_view_dirty) {
        // Ensure sync if LatLon was set but ECEF not updated (though SetLatLon updates it)
        return LatLonAltToECEF(m_lat, m_lon, m_alt);
    }
    return m_pos_ecef;
}

glm::dvec3 PerspectiveCamera::LatLonAltToECEF(double lat, double lon, double alt) const {
    double lat_rad = glm::radians(lat);
    double lon_rad = glm::radians(lon);
    
    // WGS84 Ellipsoid
    double a = EARTH_RADIUS_EQUATOR;
    double b = EARTH_RADIUS_POLAR; // Not used in simple spherical, but here for future
    // Using simple sphere for parity with current native_globe logic first,
    // but structure is ready for ellipsoid.
    // native_globe constants: GLOBE_RADIUS = 6378137.0 * 0.001 (scaled)
    // We will use real meters here and scale in the engine if needed, 
    // BUT the prompt asks for native_globe integration.
    // native_globe uses a scaled world (R ~ 6378).
    // Let's stick to the engine's scale factor if passed, or work in meters and scale matrix.
    // Strategy: Work in Real Meters internally, View Matrix handles scale? 
    // No, standard practice is keeping engine units consistent.
    // Current engine uses `GLOBE_RADIUS` (~6378.137 units).
    
    // native_globe constants: GLOBE_RADIUS = 6378137.0f * 0.001f = 6378.137
    // To maintain parity with the engine's rendering scale:
    double R = 6378.137; 
    
    double x = (R + alt * 0.001) * std::cos(lat_rad) * std::cos(lon_rad);
    double y = (R + alt * 0.001) * std::cos(lat_rad) * std::sin(lon_rad);
    double z = (R + alt * 0.001) * std::sin(lat_rad);
    
    return glm::dvec3(x, y, z);
}

void PerspectiveCamera::GetBasisVectors(glm::dvec3& forward, glm::dvec3& up, glm::dvec3& right) const {
    // 1. Calculate local East-North-Up (ENU) vectors at camera position
    double lat_rad = glm::radians(m_lat);
    double lon_rad = glm::radians(m_lon);
    
    glm::dvec3 enu_up(
        std::cos(lat_rad) * std::cos(lon_rad),
        std::cos(lat_rad) * std::sin(lon_rad),
        std::sin(lat_rad)
    );
    
    glm::dvec3 enu_east;
    bool skip_heading_rot = false;
    
    // Pole singularity check with hysteresis
    double cos_lat = std::abs(std::cos(lat_rad));
    if (m_at_pole_latch) {
        if (cos_lat > 1.2e-5) m_at_pole_latch = false;
    } else {
        if (cos_lat < 1.0e-5) m_at_pole_latch = true;
    }
    
    if (m_at_pole_latch) {
        // At pole: East is determined by heading directly to prevent rubber-banding
        // Use (sin h, cos h, 0) for CW rotation alignment
        double headingRad = glm::radians(m_heading);
        enu_east = glm::dvec3(std::sin(headingRad), std::cos(headingRad), 0.0);
        skip_heading_rot = true; // Skip standard rotation, frame is already rotated
    } else {
        enu_east = glm::dvec3(
            -std::sin(lon_rad),
            std::cos(lon_rad),
            0.0
        );
        skip_heading_rot = false;
    }
    
    glm::dvec3 enu_north = glm::cross(enu_up, enu_east);
    
    // 2. Apply Camera Orientation (Heading, Tilt, Roll)
    // Heading: Rotate around Up (Z)
    // Tilt: Rotate around Right (X)
    // Roll: Rotate around Forward (Y)
    
    glm::dmat4 rotation(1.0);
    // Align to local ENU frame first
    // Default camera looks down (-Z) or forward (North)? 
    // Google Earth: Tilt 0 = Looking straight down (Nadir)
    // Tilt 90 = Looking at Horizon
    
    // Start pointing Down (towards center of earth) = -enu_up
    // But standard camera math usually starts pointing -Z. 
    // Let's define:
    // Basis: Look = -enu_up (Nadir)
    // Up = enu_north
    // Right = enu_east
    
    glm::dvec3 base_forward = -enu_up;
    glm::dvec3 base_up = enu_north;
    glm::dvec3 base_right = enu_east;
    
    // Apply Heading (Rotation around enu_up)
    // Positive Heading = Clockwise (North -> East)
    if (!skip_heading_rot) {
        glm::dmat4 rot_heading = glm::rotate(glm::dmat4(1.0), glm::radians(-m_heading), enu_up);
        base_forward = glm::dvec3(rot_heading * glm::dvec4(base_forward, 0.0));
        base_up = glm::dvec3(rot_heading * glm::dvec4(base_up, 0.0));
        base_right = glm::dvec3(rot_heading * glm::dvec4(base_right, 0.0));
    }
    
    // Apply Tilt (Rotation around base_right)
    // Tilt 0 = Nadir, Tilt 90 = Horizon
    glm::dmat4 rot_tilt = glm::rotate(glm::dmat4(1.0), glm::radians(m_tilt), base_right);
    base_forward = glm::dvec3(rot_tilt * glm::dvec4(base_forward, 0.0));
    base_up = glm::dvec3(rot_tilt * glm::dvec4(base_up, 0.0));
    
    // Apply Roll (Rotation around base_forward)
    glm::dmat4 rot_roll = glm::rotate(glm::dmat4(1.0), glm::radians(m_roll), base_forward);
    base_up = glm::dvec3(rot_roll * glm::dvec4(base_up, 0.0));
    base_right = glm::cross(base_forward, base_up); // Re-orthogonalize
    
    forward = base_forward;
    up = base_up;
    right = base_right;
}

void PerspectiveCamera::UpdateMatrices() const {
    if (m_view_dirty) {
        glm::dvec3 f, u, r;
        GetBasisVectors(f, u, r);
        // LookAt: Eye, Center, Up
        // Center = Eye + Forward
        m_view_matrix = glm::lookAt(m_pos_ecef, m_pos_ecef + f, u);
        m_view_dirty = false;
    }
    
    if (m_proj_dirty) {
        if (!m_reverseZ) {
            // Standard Z: near=0, far=1 in depth buffer
            m_proj_matrix = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
        } else {
            // Reversed-Z with infinite far plane
            // This gives maximum depth precision at far distances
            // Depth buffer: 0 = far, 1 = near
            // GL setup: glDepthFunc(GL_GEQUAL), glClearDepth(0.0f)
            
            const double f = 1.0 / std::tan(glm::radians(m_fov) * 0.5);
            const double n = m_near;
            
            // Infinite far plane Reversed-Z matrix
            // Clip space Z: +1 at near plane, approaches 0 at infinity
            m_proj_matrix = glm::dmat4(0.0);
            m_proj_matrix[0][0] = f / m_aspect;
            m_proj_matrix[1][1] = f;
            m_proj_matrix[2][2] = 0.0;      // Coefficient for Z: maps to 0 at infinity
            m_proj_matrix[2][3] = -1.0;     // Perspective divide
            m_proj_matrix[3][2] = n;        // Near plane offset: Z = n/w = 1 at near
            // Result: gl_FragCoord.z = n / (-viewZ) 
            // At near plane (viewZ = -n): z = 1
            // At infinity (viewZ = -inf): z approaches 0
        }
        m_proj_dirty = false;
    }
}

glm::dmat4 PerspectiveCamera::GetViewMatrix() const {
    UpdateMatrices();
    return m_view_matrix;
}

glm::dmat4 PerspectiveCamera::GetProjectionMatrix() const {
    UpdateMatrices();
    return m_proj_matrix;
}

glm::dmat4 PerspectiveCamera::GetViewProjectionMatrix() const {
    UpdateMatrices();
    return m_proj_matrix * m_view_matrix;
}

std::array<glm::dvec4, 6> PerspectiveCamera::GetFrustumPlanes() const {
    glm::dmat4 vp = GetViewProjectionMatrix();
    std::array<glm::dvec4, 6> planes;
    
    // Gribb/Hartmann extraction
    // Left
    planes[0] = glm::row(vp, 3) + glm::row(vp, 0);
    // Right
    planes[1] = glm::row(vp, 3) - glm::row(vp, 0);
    // Bottom
    planes[2] = glm::row(vp, 3) + glm::row(vp, 1);
    // Top
    planes[3] = glm::row(vp, 3) - glm::row(vp, 1);
    // Near
    planes[4] = glm::row(vp, 3) + glm::row(vp, 2);
    // Far
    planes[5] = glm::row(vp, 3) - glm::row(vp, 2);
    
    // Normalize
    for (auto& p : planes) {
        double len = glm::length(glm::dvec3(p));
        if (len > 0) p /= len;
    }
    
    return planes;
}

void PerspectiveCamera::GetRay(double screenX, double screenY, int screenW, int screenH, glm::dvec3& origin, glm::dvec3& direction) const {
    UpdateMatrices(); // Ensure matrices are fresh
    
    // NDC Coords (-1 to 1)
    double ndcX = (2.0 * screenX) / screenW - 1.0;
    double ndcY = 1.0 - (2.0 * screenY) / screenH; // Flip Y for GL
    
    // Inverse ViewProjection
    glm::dmat4 invVP = glm::inverse(GetViewProjectionMatrix());
    
    // Ray Start (Near Plane) - used only to derive direction.
    // IMPORTANT: Use the camera position as ray origin (not the near-plane point).
    // Near-plane origin skews picking/pan at low altitude and breaks terrain-aware navigation parity.
    double nearClipZ = m_reverseZ ? 1.0 : -1.0;
    double farClipZ = m_reverseZ ? -1.0 : 1.0;

    glm::dvec4 rayStartClip(ndcX, ndcY, nearClipZ, 1.0);
    glm::dvec4 rayStartWorld = invVP * rayStartClip;
    rayStartWorld /= rayStartWorld.w;
    
    // Ray origin is the camera ECEF position (km units).
    origin = LatLonAltToECEF(m_lat, m_lon, m_alt);
    glm::dvec3 dir = glm::dvec3(rayStartWorld) - origin;
    double len = glm::length(dir);
    if (len > 1e-12 && std::isfinite(len)) {
        direction = dir / len;
    } else {
        // Fallback: derive direction from far plane point.
        glm::dvec4 rayEndClip(ndcX, ndcY, farClipZ, 1.0);
        glm::dvec4 rayEndWorld = invVP * rayEndClip;
        rayEndWorld /= rayEndWorld.w;
        direction = glm::normalize(glm::dvec3(rayEndWorld) - origin);
    }
}

bool PerspectiveCamera::ScreenToGeo(double screenX, double screenY, int screenW, int screenH, double& outLon, double& outLat) const {
    glm::dvec3 origin, direction;
    GetRay(screenX, screenY, screenW, screenH, origin, direction);
    
    // Ray-sphere intersection with Earth
    // IMPORTANT: Camera ECEF is in km, so use km radius (not meters)
    const double R = 6378.137;  // Earth equatorial radius in km
    
    // Quadratic: |origin + t*direction|^2 = R^2
    // a*t^2 + b*t + c = 0
    double a = glm::dot(direction, direction);
    double b = 2.0 * glm::dot(origin, direction);
    double c = glm::dot(origin, origin) - R * R;
    
    double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) {
        return false; // No intersection
    }
    
    double sqrtD = std::sqrt(discriminant);
    double t1 = (-b - sqrtD) / (2.0 * a);
    double t2 = (-b + sqrtD) / (2.0 * a);
    
    // Pick closest positive t
    double t = (t1 > 0.0) ? t1 : t2;
    if (t < 0.0) {
        return false; // Behind camera
    }
    
    glm::dvec3 hitPoint = origin + t * direction;
    
    // ECEF to LatLon
    double x = hitPoint.x;
    double y = hitPoint.y;
    double z = hitPoint.z;
    
    outLon = glm::degrees(std::atan2(y, x));
    outLat = glm::degrees(std::asin(std::clamp(z / R, -1.0, 1.0)));
    
    return true;
}

bool PerspectiveCamera::GeoToScreen(double lon, double lat, double altM, int screenW, int screenH, double& outScreenX, double& outScreenY) const {
    // Convert geo to ECEF
    glm::dvec3 worldPos = LatLonAltToECEF(lat, lon, altM);
    
    // Project to clip space
    glm::dmat4 vp = GetViewProjectionMatrix();
    glm::dvec4 clipPos = vp * glm::dvec4(worldPos, 1.0);
    
    // Behind camera check
    if (clipPos.w <= 0.0) {
        return false;
    }
    
    // NDC
    glm::dvec3 ndc = glm::dvec3(clipPos) / clipPos.w;
    
    // Check if in view frustum
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < -1.0 || ndc.z > 1.0) {
        return false;
    }
    
    // NDC to screen
    outScreenX = (ndc.x + 1.0) * 0.5 * screenW;
    outScreenY = (1.0 - ndc.y) * 0.5 * screenH; // Flip Y for GL
    
    return true;
}

} // namespace earth
