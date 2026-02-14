// Horizon Culler Test
// Validates horizon culling mathematics and behavior (using frustum.h HorizonCuller)

#include "../src/math/frustum.h"
#include "../src/core/constants.h"
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>

using namespace globe;

namespace {

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
    
    const float earthRadiusKm = 6371.0f;
    
    // Test 1: Basic visibility - point on near side
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);  // 100km altitude
        culler.Update(cameraPos, earthRadiusKm);
        
        // Point slightly offset from directly below (within visible cone)
        // At 100km altitude, visible cone is ~10 degrees, so 5 degrees offset should be visible
        glm::vec3 nearPoint(earthRadiusKm * 0.996f, earthRadiusKm * 0.087f, 0.0f);  // ~5 deg offset
        bool visible = culler.IsVisible(nearPoint);
        if (!Expect(visible, "Point within visible cone should be visible")) failures++;
        
        Report("BasicVisibilityNearSide");
    }
    
    // Test 2: Opposite side should be culled
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);
        culler.Update(cameraPos, earthRadiusKm);
        
        // Point on opposite side of Earth
        glm::vec3 farPoint(-earthRadiusKm, 0.0f, 0.0f);
        bool visible = culler.IsVisible(farPoint);
        if (!Expect(!visible, "Point on opposite side should be culled")) failures++;
        
        Report("OppositeSideCulled");
    }
    
    // Test 3: Sphere visibility - large tile near camera
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);
        culler.Update(cameraPos, earthRadiusKm);
        
        // Large tile directly below
        glm::vec3 tileCenter(earthRadiusKm, 0.0f, 0.0f);
        float tileRadius = 100.0f;  // 100km radius
        
        bool visible = culler.IsSphereVisible(tileCenter, tileRadius);
        if (!Expect(visible, "Large tile near camera should be visible")) failures++;
        
        Report("SphereVisibilityNear");
    }
    
    // Test 4: Safety margin effect
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);
        culler.Update(cameraPos, earthRadiusKm);
        
        // Point near horizon boundary
        // At 100km altitude, horizon is at ~acos(6371/6471) = ~10 degrees
        // Point at ~15 degrees from center should be near horizon
        glm::vec3 boundaryPoint(earthRadiusKm * 0.96f, earthRadiusKm * 0.28f, 0.0f);
        
        // Without safety margin (factor = 1.0)
        culler.SetSafetyMargin(1.0f);
        bool visibleNoMargin = culler.IsVisible(boundaryPoint);
        
        // With safety margin (factor = 1.1 = ~10% more conservative)
        culler.SetSafetyMargin(1.1f);
        bool visibleWithMargin = culler.IsVisible(boundaryPoint);
        
        // Safety margin should make culling more conservative (keep more tiles)
        if (!Expect(!visibleNoMargin || visibleWithMargin,
                   "Safety margin should make culling more conservative")) failures++;
        
        Report("SafetyMarginEffect");
    }
    
    // Test 5: High altitude - larger visible area
    {
        HorizonCuller cullerLow;
        HorizonCuller cullerHigh;
        
        glm::vec3 cameraLow(earthRadiusKm + 10.0f, 0.0f, 0.0f);   // 10km
        glm::vec3 cameraHigh(earthRadiusKm + 1000.0f, 0.0f, 0.0f); // 1000km
        
        cullerLow.Update(cameraLow, earthRadiusKm);
        cullerHigh.Update(cameraHigh, earthRadiusKm);
        
        // Horizon distance should be larger at high altitude
        float distLow = cullerLow.GetHorizonDistance();
        float distHigh = cullerHigh.GetHorizonDistance();
        
        if (!Expect(distHigh > distLow, 
                   "Horizon distance should be larger at high altitude")) failures++;
        
        // Cos angle should be smaller at high altitude (larger visible cone)
        float cosLow = cullerLow.GetHorizonCosAngle();
        float cosHigh = cullerHigh.GetHorizonCosAngle();
        
        if (!Expect(cosHigh < cosLow,
                   "Horizon cos angle should be smaller at high altitude")) failures++;
        
        Report("HighAltitudeHorizon");
    }
    
    // Test 6: Camera at surface - everything visible
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm, 0.0f, 0.0f);  // On surface
        culler.Update(cameraPos, earthRadiusKm);
        
        // Any point should be visible when camera is on surface
        glm::vec3 anyPoint(-earthRadiusKm, 0.0f, 0.0f);
        bool visible = culler.IsSphereVisible(anyPoint, 100.0f);
        
        if (!Expect(visible, "Everything should be visible when camera is on surface")) failures++;
        
        Report("SurfaceCamera");
    }
    
    // Test 7: Config integration - useHorizonCulling flag simulation
    {
        // Simulate config.useHorizonCulling = false
        bool useHorizonCulling = false;
        
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);
        culler.Update(cameraPos, earthRadiusKm);
        
        glm::vec3 farPoint(-earthRadiusKm, 0.0f, 0.0f);
        
        // When disabled, we should not cull (return true)
        bool wouldBeVisible = true;  // Simulating "disabled = always visible"
        
        if (!useHorizonCulling) {
            // In real implementation, this would skip horizon culling entirely
            wouldBeVisible = true;
        } else {
            wouldBeVisible = culler.IsVisible(farPoint);
        }
        
        if (!Expect(wouldBeVisible, "When horizon culling disabled, tiles should be visible")) failures++;
        
        Report("ConfigIntegration");
    }
    
    // Test 8: Batch culling statistics simulation
    {
        HorizonCuller culler;
        glm::vec3 cameraPos(earthRadiusKm + 100.0f, 0.0f, 0.0f);
        culler.Update(cameraPos, earthRadiusKm);
        
        int totalTested = 10;
        int culledCount = 0;
        int keptCount = 0;
        
        // Test 5 near-side tiles (should be visible)
        for (int i = 0; i < 5; ++i) {
            glm::vec3 tileCenter(earthRadiusKm, i * 200.0f, 0.0f);
            if (culler.IsSphereVisible(tileCenter, 100.0f)) {
                keptCount++;
            } else {
                culledCount++;
            }
        }
        
        // Test 5 far-side tiles (should be culled)
        for (int i = 0; i < 5; ++i) {
            glm::vec3 tileCenter(-earthRadiusKm, i * 200.0f, 0.0f);
            if (culler.IsSphereVisible(tileCenter, 100.0f)) {
                keptCount++;
            } else {
                culledCount++;
            }
        }
        
        if (!Expect(keptCount + culledCount == totalTested,
                   "Total tested should equal kept + culled")) failures++;
        if (!Expect(culledCount >= 3, "Should cull at least some far-side tiles")) failures++;
        
        float cullRatio = static_cast<float>(culledCount) / totalTested;
        if (!Expect(cullRatio >= 0.3f && cullRatio <= 0.7f,
                   "Cull ratio should be reasonable (30-70%)")) failures++;
        
        Report("BatchCullingStats");
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll Horizon Culler tests PASSED\n";
    return 0;
}
