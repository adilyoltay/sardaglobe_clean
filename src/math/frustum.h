#pragma once

#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <cmath>

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
        // GLM is column-major (`m[col][row]`). Build explicit rows first and
        // then apply the canonical extraction: row3 +/- row{0,1,2}.
        glm::vec4 row0(mvp[0][0], mvp[1][0], mvp[2][0], mvp[3][0]);
        glm::vec4 row1(mvp[0][1], mvp[1][1], mvp[2][1], mvp[3][1]);
        glm::vec4 row2(mvp[0][2], mvp[1][2], mvp[2][2], mvp[3][2]);
        glm::vec4 row3(mvp[0][3], mvp[1][3], mvp[2][3], mvp[3][3]);

        const glm::vec4 left = row3 + row0;
        const glm::vec4 right = row3 - row0;
        const glm::vec4 bottom = row3 + row1;
        const glm::vec4 top = row3 - row1;
        const glm::vec4 nearPlane = row3 + row2;
        const glm::vec4 farPlane = row3 - row2;

        planes_[Left].normal = glm::vec3(left);
        planes_[Left].distance = left.w;

        planes_[Right].normal = glm::vec3(right);
        planes_[Right].distance = right.w;

        planes_[Bottom].normal = glm::vec3(bottom);
        planes_[Bottom].distance = bottom.w;

        planes_[Top].normal = glm::vec3(top);
        planes_[Top].distance = top.w;

        planes_[Near].normal = glm::vec3(nearPlane);
        planes_[Near].distance = nearPlane.w;

        planes_[Far].normal = glm::vec3(farPlane);
        planes_[Far].distance = farPlane.w;
        
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
    
    // Set safety margin for conservative culling (to avoid false negatives)
    void SetSafetyMargin(float marginFactor) {
        safetyMargin_ = marginFactor;
    }
    
    float GetSafetyMargin() const { return safetyMargin_; }
    
    // Test if point on globe surface is above horizon
    bool IsVisible(const glm::vec3& surfacePoint) const {
        glm::vec3 toPoint = glm::normalize(surfacePoint - cameraPos_);
        glm::vec3 toCenter = -glm::normalize(cameraPos_);
        float dot = glm::dot(toPoint, toCenter);
        // Apply safety margin: make culling more conservative
        return dot < (horizonCosAngle_ * safetyMargin_);
    }
    
    // Test if sphere is potentially visible (conservative)
    bool IsSphereVisible(const glm::vec3& center, float radius) const {
        // If camera is inside/on globe, disable horizon culling.
        if (cameraHeight_ <= globeRadius_ + 0.001f) {
            return true;
        }

        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
            return true;
        }

        // Horizon plane (exact for points on the occluder sphere):
        // Tangent points satisfy: C·P = R^2 where C is camera position (origin-centered).
        //
        // For a target sphere, test if ANY point of that sphere can lie on the visible side:
        // max(C·P) over P in targetSphere = C·center + |C|*radius.
        //
        // This is conservative and avoids false negatives that lead to low-LOD coverage holes.
        float dotCenter = glm::dot(cameraPos_, center);
        // Apply safety margin: shrink the horizon threshold to be more conservative
        float adjustedRadius = radius * safetyMargin_;
        float maxDot = dotCenter + std::max(0.0f, adjustedRadius) * cameraHeight_;
        float horizon = globeRadius_ * globeRadius_;
        return maxDot >= horizon - 1e-3f;
    }
    
    // Get horizon distance
    float GetHorizonDistance() const { return horizonDist_; }
    
    // Get horizon angle (cosine)
    float GetHorizonCosAngle() const { return horizonCosAngle_; }

public:
    // Statistics for debugging
    struct Stats {
        int totalTested = 0;
        int culledCount = 0;
        int keptCount = 0;
        void Reset() { totalTested = culledCount = keptCount = 0; }
    };
    
    const Stats& GetStats() const { return stats_; }
    void ResetStats() { stats_.Reset(); }
    
private:
    glm::vec3 cameraPos_{0.0f};
    float globeRadius_ = 0.0f;
    float cameraHeight_ = 0.0f;
    float horizonDist_ = 0.0f;
    float horizonCosAngle_ = 1.0f;
    float safetyMargin_ = 1.0f;  // Default 1.0 = no additional margin
    mutable Stats stats_;  // Mutable for const-correctness in test functions
};

} // namespace globe
