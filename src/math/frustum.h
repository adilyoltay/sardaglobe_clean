#pragma once

#include <glm/glm.hpp>
#include <array>

namespace globe {

// Frustum plane
struct Plane {
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
    
    float DistanceToPoint(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

// View frustum for culling
class Frustum {
public:
    enum Side { Left, Right, Bottom, Top, Near, Far, Count };
    
    Frustum() = default;
    
    // Extract frustum planes from MVP matrix
    void Extract(const glm::mat4& mvp) {
        // Left
        planes_[Left].normal.x = mvp[0][3] + mvp[0][0];
        planes_[Left].normal.y = mvp[1][3] + mvp[1][0];
        planes_[Left].normal.z = mvp[2][3] + mvp[2][0];
        planes_[Left].distance = mvp[3][3] + mvp[3][0];
        
        // Right
        planes_[Right].normal.x = mvp[0][3] - mvp[0][0];
        planes_[Right].normal.y = mvp[1][3] - mvp[1][0];
        planes_[Right].normal.z = mvp[2][3] - mvp[2][0];
        planes_[Right].distance = mvp[3][3] - mvp[3][0];
        
        // Bottom
        planes_[Bottom].normal.x = mvp[0][3] + mvp[0][1];
        planes_[Bottom].normal.y = mvp[1][3] + mvp[1][1];
        planes_[Bottom].normal.z = mvp[2][3] + mvp[2][1];
        planes_[Bottom].distance = mvp[3][3] + mvp[3][1];
        
        // Top
        planes_[Top].normal.x = mvp[0][3] - mvp[0][1];
        planes_[Top].normal.y = mvp[1][3] - mvp[1][1];
        planes_[Top].normal.z = mvp[2][3] - mvp[2][1];
        planes_[Top].distance = mvp[3][3] - mvp[3][1];
        
        // Near
        planes_[Near].normal.x = mvp[0][3] + mvp[0][2];
        planes_[Near].normal.y = mvp[1][3] + mvp[1][2];
        planes_[Near].normal.z = mvp[2][3] + mvp[2][2];
        planes_[Near].distance = mvp[3][3] + mvp[3][2];
        
        // Far
        planes_[Far].normal.x = mvp[0][3] - mvp[0][2];
        planes_[Far].normal.y = mvp[1][3] - mvp[1][2];
        planes_[Far].normal.z = mvp[2][3] - mvp[2][2];
        planes_[Far].distance = mvp[3][3] - mvp[3][2];
        
        // Normalize planes
        for (auto& plane : planes_) {
            float len = glm::length(plane.normal);
            if (len > 0.0001f) {
                plane.normal /= len;
                plane.distance /= len;
            }
        }
    }
    
    // Test if sphere is visible (fully or partially)
    bool IsSphereVisible(const glm::vec3& center, float radius) const {
        for (const auto& plane : planes_) {
            if (plane.DistanceToPoint(center) < -radius) {
                return false;  // Fully outside this plane
            }
        }
        return true;
    }
    
    // Test if point is inside frustum
    bool IsPointInside(const glm::vec3& point) const {
        for (const auto& plane : planes_) {
            if (plane.DistanceToPoint(point) < 0.0f) {
                return false;
            }
        }
        return true;
    }
    
    const Plane& GetPlane(int index) const { return planes_[index]; }

private:
    std::array<Plane, Count> planes_;
};

// Horizon culling for globe
class HorizonCuller {
public:
    void Update(const glm::vec3& cameraPos, float globeRadius) {
        cameraPos_ = cameraPos;
        globeRadius_ = globeRadius;
        cameraHeight_ = glm::length(cameraPos);
        
        // Horizon distance from camera
        if (cameraHeight_ > globeRadius) {
            horizonDist_ = std::sqrt(cameraHeight_ * cameraHeight_ - globeRadius * globeRadius);
            // Horizon angle (angle from camera-center line to horizon)
            horizonCosAngle_ = globeRadius / cameraHeight_;
        } else {
            horizonDist_ = 0.0f;
            horizonCosAngle_ = 1.0f;  // On surface, everything visible
        }
    }
    
    // Test if point on globe surface is above horizon
    bool IsVisible(const glm::vec3& surfacePoint) const {
        glm::vec3 toPoint = glm::normalize(surfacePoint - cameraPos_);
        glm::vec3 toCenter = -glm::normalize(cameraPos_);
        float dot = glm::dot(toPoint, toCenter);
        return dot < horizonCosAngle_;
    }
    
    // Test if sphere is potentially visible (conservative)
    bool IsSphereVisible(const glm::vec3& center, float radius) const {
        // If camera is inside/on globe, disable horizon culling.
        if (cameraHeight_ <= globeRadius_ + 0.001f) {
            return true;
        }

        glm::vec3 toTarget = center - cameraPos_;
        float targetDist = glm::length(toTarget);
        if (targetDist < 0.001f) return true;

        glm::vec3 dir = toTarget / targetDist;

        // Intersect camera ray with globe sphere (origin-centered).
        // If first hit is significantly before target's near boundary,
        // target is occluded by globe horizon.
        float b = glm::dot(cameraPos_, dir);
        float c = glm::dot(cameraPos_, cameraPos_) - globeRadius_ * globeRadius_;
        float disc = b * b - c;
        if (disc <= 0.0f) {
            return true;  // No globe hit along this ray => visible.
        }

        float sqrtDisc = std::sqrt(disc);
        float t0 = -b - sqrtDisc;  // nearest intersection
        float t1 = -b + sqrtDisc;
        float tHit = (t0 > 0.0f) ? t0 : t1;
        if (tHit <= 0.0f) {
            return true;
        }

        // Conservative allowance for target radius and numeric noise.
        float nearTarget = std::max(0.0f, targetDist - radius);
        return tHit + 1e-3f >= nearTarget;
    }

private:
    glm::vec3 cameraPos_{0.0f};
    float globeRadius_ = 0.0f;
    float cameraHeight_ = 0.0f;
    float horizonDist_ = 0.0f;
    float horizonCosAngle_ = 1.0f;
};

} // namespace globe
