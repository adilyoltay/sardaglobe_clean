// Reversed-Z Precision Test
// Faz 1A: Validates infinite far plane projection and depth buffer precision

#include "../src/camera/earth_camera.h"
#include <cmath>
#include <iostream>

using namespace earth;

namespace {

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

void Report(const char* test) {
    std::cerr << "PASSED: " << test << '\n';
}

} // namespace

int main() {
    int failures = 0;
    
    // Test 1: Infinite far projection maps near to Z=1
    {
        PerspectiveCamera camera;
        camera.SetFov(45.0);
        camera.SetAspectRatio(16.0 / 9.0);
        camera.SetNearFar(1.0, 1000000.0);
        camera.SetReverseZEnabled(true);
        
        auto proj = camera.GetProjectionMatrix();
        glm::dvec4 nearPoint(0, 0, -1.0, 1);
        glm::dvec4 nearClip = proj * nearPoint;
        
        if (!Expect(nearClip.w > 0.0, "Near clip W should be positive")) failures++;
        double nearNDC = nearClip.z / nearClip.w;
        if (!Expect(Near(nearNDC, 1.0), "Near plane should map to NDC Z = 1")) failures++;
        
        if (failures == 0) Report("InfiniteFarProjectionMapsNearToOne");
    }
    
    // Test 2: Infinite far projection maps far to Z=0
    {
        PerspectiveCamera camera;
        camera.SetFov(45.0);
        camera.SetAspectRatio(16.0 / 9.0);
        camera.SetNearFar(1.0, 1000000.0);
        camera.SetReverseZEnabled(true);
        
        auto proj = camera.GetProjectionMatrix();
        glm::dvec4 farPoint(0, 0, -1000000.0, 1);
        glm::dvec4 farClip = proj * farPoint;
        
        if (!Expect(farClip.w > 0.0, "Far clip W should be positive")) failures++;
        double farNDC = farClip.z / farClip.w;
        if (!Expect(Near(farNDC, 0.0, 0.01), "Far plane should map close to NDC Z = 0")) failures++;
        
        if (failures == 0) Report("InfiniteFarProjectionMapsFarToZero");
    }
    
    // Test 3: Standard vs Reversed-Z produce different matrices
    {
        PerspectiveCamera camera;
        camera.SetFov(45.0);
        camera.SetAspectRatio(16.0 / 9.0);
        camera.SetNearFar(1.0, 1000000.0);
        
        camera.SetReverseZEnabled(false);
        auto standardProj = camera.GetProjectionMatrix();
        
        camera.SetReverseZEnabled(true);
        auto reversedProj = camera.GetProjectionMatrix();
        
        bool different = false;
        for (int i = 0; i < 4 && !different; ++i) {
            for (int j = 0; j < 4 && !different; ++j) {
                if (std::fabs(standardProj[i][j] - reversedProj[i][j]) > 1e-10) {
                    different = true;
                }
            }
        }
        
        if (!Expect(different, "Standard and Reversed-Z projections should differ")) failures++;
        else Report("StandardProjectionDifferentFromReversedZ");
    }
    
    // Test 4: Projection is consistent across multiple calls
    {
        PerspectiveCamera camera;
        camera.SetReverseZEnabled(true);
        
        auto proj1 = camera.GetProjectionMatrix();
        auto proj2 = camera.GetProjectionMatrix();
        
        bool same = true;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                if (proj1[i][j] != proj2[i][j]) same = false;
            }
        }
        
        if (!Expect(same, "Projection should be consistent across calls")) failures++;
        else Report("ProjectionIsConsistent");
    }
    
    // Test 5: Reversed-Z monotonicity (values decrease as distance increases)
    {
        PerspectiveCamera camera;
        camera.SetFov(45.0);
        camera.SetAspectRatio(16.0 / 9.0);
        camera.SetNearFar(1.0, 1000000.0);
        camera.SetReverseZEnabled(true);
        
        auto proj = camera.GetProjectionMatrix();
        
        // Test monotonicity: Z values should decrease as distance increases
        std::vector<double> distances = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};
        std::vector<double> ndcZs;
        
        // Debug output
        // std::cerr << "Reversed-Z debug:\n";
        for (double d : distances) {
            glm::dvec4 p(0, 0, -d, 1);
            glm::dvec4 clip = proj * p;
            double ndcZ = clip.z / clip.w;
            ndcZs.push_back(ndcZ);
            // std::cerr << "  d=" << d << "m -> Z=" << ndcZ << "\n";
        }
        
        // Check monotonicity (strictly decreasing)
        bool monotonic = true;
        for (size_t i = 1; i < ndcZs.size(); ++i) {
            if (ndcZs[i] >= ndcZs[i-1]) {
                monotonic = false;
                break;
            }
        }
        
        // In Reversed-Z:
        // - Near plane (1m) should map to Z = 1
        // - As distance increases, Z should approach 0
        bool nearIsOne = Near(ndcZs[0], 1.0, 0.01);  // 1m should be very close to 1
        bool farIsSmall = ndcZs.back() < 0.5;         // 100km should be significantly less than 0.5
        bool decreasing = ndcZs.front() > ndcZs.back(); // Should decrease overall
        
        if (!Expect(monotonic, "Reversed-Z Z values should decrease monotonically with distance")) failures++;
        if (!Expect(nearIsOne, "Reversed-Z near plane should map to Z near 1")) failures++;
        if (!Expect(farIsSmall, "Reversed-Z far plane should map to Z < 0.5")) failures++;
        if (!Expect(decreasing, "Reversed-Z should decrease from near to far")) failures++;
        
        if (monotonic && nearIsOne && farIsSmall && decreasing) {
            Report("ReversedZMonotonicityAndRange");
        }
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll Reversed-Z tests PASSED\n";
    return 0;
}
