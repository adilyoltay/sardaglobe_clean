#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>

// WGS84 Constants
constexpr double EARTH_RADIUS_EQUATOR = 6378137.0;
constexpr double EARTH_RADIUS_POLAR = 6356752.3142;

namespace earth {

// Camera Types (Google Earth Parity)
enum class CameraType {
    PERSPECTIVE,
    ORTHOGRAPHIC
};

// Abstract Camera Base
class Camera {
public:
    virtual ~Camera() = default;
    
    // Position
    virtual void SetLatLonAlt(double lat, double lon, double alt) = 0;
    virtual void GetLatLonAlt(double& lat, double& lon, double& alt) const = 0;
    
    // Orientation
    virtual void SetHeading(double heading_deg) = 0;
    virtual double GetHeading() const = 0;
    
    virtual void SetTilt(double tilt_deg) = 0;
    virtual double GetTilt() const = 0;
    
    virtual void SetRoll(double roll_deg) = 0;
    virtual double GetRoll() const = 0;
    
    // Matrices
    virtual glm::dmat4 GetViewMatrix() const = 0;
    virtual glm::dmat4 GetProjectionMatrix() const = 0;
    virtual glm::dmat4 GetViewProjectionMatrix() const = 0;
    
    // Frustum
    // Returns 6 planes: Left, Right, Bottom, Top, Near, Far
    // Each plane is vec4 (nx, ny, nz, d)
    virtual std::array<glm::dvec4, 6> GetFrustumPlanes() const = 0;
    
    // Helper: Get forward/up/right vectors in world space
    virtual void GetBasisVectors(glm::dvec3& forward, glm::dvec3& up, glm::dvec3& right) const = 0;
};

// Perspective Camera (3D Flight)
class PerspectiveCamera : public Camera {
public:
    PerspectiveCamera();
    
    // Camera Interface Implementation
    void SetLatLonAlt(double lat, double lon, double alt) override;
    void GetLatLonAlt(double& lat, double& lon, double& alt) const override;
    
    void SetHeading(double heading_deg) override;
    double GetHeading() const override;
    
    void SetTilt(double tilt_deg) override;
    double GetTilt() const override;
    
    void SetRoll(double roll_deg) override;
    double GetRoll() const override;
    
    glm::dmat4 GetViewMatrix() const override;
    glm::dmat4 GetProjectionMatrix() const override;
    glm::dmat4 GetViewProjectionMatrix() const override;
    std::array<glm::dvec4, 6> GetFrustumPlanes() const override;
    void GetBasisVectors(glm::dvec3& forward, glm::dvec3& up, glm::dvec3& right) const override;

    // Perspective Specifics
    void SetFov(double fov_deg);
    double GetFov() const;
    void SetAspectRatio(double aspect);
    void SetNearFar(double near_plane, double far_plane);
    
    // Direct position setting (for smooth animations)
    void SetPositionECEF(const glm::dvec3& pos);
    glm::dvec3 GetPositionECEF() const;

    // Raycasting for Interaction
    void GetRay(double screenX, double screenY, int screenW, int screenH, glm::dvec3& origin, glm::dvec3& direction) const;

private:
    void UpdateMatrices() const;
    glm::dvec3 LatLonAltToECEF(double lat, double lon, double alt) const;

    // State
    double m_lat = 0.0;
    double m_lon = 0.0;
    double m_alt = 25512546.06; // LOD 2 default
    
    double m_heading = 0.0;
    double m_tilt = 0.0;
    double m_roll = 0.0;
    
    double m_fov = 45.0;
    double m_aspect = 1.777;
    double m_near = 0.1;
    double m_far = 100000000.0;
    
    // Cached
    mutable glm::dvec3 m_pos_ecef;
    mutable glm::dmat4 m_view_matrix;
    mutable glm::dmat4 m_proj_matrix;
    mutable bool m_view_dirty = true;
    mutable bool m_proj_dirty = true;
    
    // Hysteresis for pole singularity
    mutable bool m_at_pole_latch = false;
};

} // namespace earth
