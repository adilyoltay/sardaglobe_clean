#include "../src/flight_controller.h"
#include "../src/earth_camera.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <GLFW/glfw3.h>

using namespace earth;

double g_mockTime = 100.0;

void TestDeadzone() {
    std::cout << "Testing Deadzone..." << std::endl;
    PerspectiveCamera cam;
    cam.SetLatLonAlt(41.0, 29.0, 10000.0);
    cam.SetAspectRatio(1.0);
    cam.SetFov(45.0);
    cam.SetTilt(45.0); // Avoid Zenith
    
    FlightController fc(cam);
    fc.OnWindowResize(1000, 1000);
    fc.SetNavigationSpeed(1.0);
    
    // Initial State
    double startHeading = cam.GetHeading();
    double startTilt = cam.GetTilt();
    
    // Mouse Down (Shift+Left = Orbit)
    fc.OnModifiers(true, false); // Shift Down
    fc.OnMouseDown(0, 500, 500, g_mockTime); // Center
    
    // Micro Move (0.1 px) - Should be ignored
    fc.OnMouseMove(500.1, 500.1, g_mockTime);
    
    // Check Camera - Should be EXACTLY same (Deadzone blocked it)
    if (std::abs(cam.GetHeading() - startHeading) > 0.0001) {
        throw std::runtime_error("Deadzone failed: Heading changed on micro move");
    }
    
    // Large Move (10 px)
    fc.OnMouseMove(510.0, 500.0, g_mockTime);
    
    // Check Camera - Should have moved
    if (std::abs(cam.GetHeading() - startHeading) < 0.1) {
        throw std::runtime_error("Movement failed: Heading did not change on large move");
    }
    
    std::cout << "  [PASS] Deadzone Verified" << std::endl;
}

void TestPivotMomentum() {
    std::cout << "Testing Pivot Momentum..." << std::endl;
    PerspectiveCamera cam;
    cam.SetLatLonAlt(41.0, 29.0, 10000.0);
    cam.SetTilt(45.0); // Avoid Zenith
    FlightController fc(cam);
    fc.OnWindowResize(1000, 1000);
    
    fc.OnModifiers(true, false);
    fc.OnMouseDown(0, 500, 500, g_mockTime); // Start Orbit
    
    g_mockTime += 0.01;
    fc.OnMouseMove(510.0, 500.0, g_mockTime); // Move 10px
    
    // Release
    g_mockTime += 0.01;
    fc.OnMouseUp(0, g_mockTime);
    
    // Check Momentum
    // Update should apply momentum
    double h1 = cam.GetHeading();
    fc.Update(0.016, g_mockTime);
    double h2 = cam.GetHeading();
    
    if (std::abs(h2 - h1) < 0.0001) {
        throw std::runtime_error("Momentum failed: No movement after release");
    }
    
    std::cout << "  [PASS] Momentum Verified" << std::endl;
}

void TestTiltLimits() {
    std::cout << "Testing Tilt Limits (5-175)..." << std::endl;
    PerspectiveCamera cam;
    cam.SetLatLonAlt(41.0, 29.0, 1000.0); // 1km altitude
    cam.SetTilt(0.0);
    FlightController fc(cam);
    fc.OnWindowResize(1000, 1000);
    
    fc.OnModifiers(true, false);
    fc.OnMouseDown(0, 500, 500, g_mockTime);
    
    // Drag Down (Increase Tilt) massively
    g_mockTime += 0.1;
    fc.OnMouseMove(500.0, 1000.0, g_mockTime); // 500px down
    
    double t = cam.GetTilt();
    std::cout << "  Tilt reached: " << t << std::endl;
    if (t <= 85.0) {
        throw std::runtime_error("Tilt Limit failed: Cannot exceed 85 degrees");
    }
    if (t > 175.001) {
         throw std::runtime_error("Tilt Limit failed: Exceeded 175 degrees");
    }
    
    std::cout << "  [PASS] Tilt Limits Verified" << std::endl;
}

void TestMomentumGap() {
    std::cout << "Testing Momentum Gap (Pause > 0.1s)..." << std::endl;
    PerspectiveCamera cam;
    cam.SetLatLonAlt(41.0, 29.0, 10000.0);
    cam.SetTilt(45.0);
    FlightController fc(cam);
    fc.OnWindowResize(1000, 1000);
    
    // Start Orbit
    fc.OnModifiers(true, false);
    fc.OnMouseDown(0, 500, 500, g_mockTime);
    
    // Move
    g_mockTime += 0.01;
    fc.OnMouseMove(510.0, 500.0, g_mockTime);
    
    // Pause
    g_mockTime += 0.2;
    fc.OnMouseUp(0, g_mockTime);
    
    // Check Momentum (Should be 0)
    double h1 = cam.GetHeading();
    fc.Update(0.016, g_mockTime);
    double h2 = cam.GetHeading();
    
    if (std::abs(h2 - h1) > 0.0001) {
        throw std::runtime_error("Gap failed: Momentum applied after pause");
    }
    std::cout << "  [PASS] Gap Verified" << std::endl;
}

void TestMidDragGap() {
    std::cout << "Testing Mid-Drag Momentum Gap..." << std::endl;
    PerspectiveCamera cam;
    cam.SetLatLonAlt(41.0, 29.0, 10000.0);
    cam.SetAspectRatio(1.0);
    cam.SetFov(45.0);
    cam.SetTilt(45.0); // Avoid Zenith
    
    FlightController fc(cam);
    fc.OnWindowResize(1000, 1000);
    
    // Start Orbit
    fc.OnModifiers(true, false);
    fc.OnMouseDown(0, 500, 500, g_mockTime);
    
    // Move
    g_mockTime += 0.01;
    fc.OnMouseMove(510.0, 500.0, g_mockTime);
    
    // Check velocity should be non-zero (internal state check via behavior)
    // We can't easily check internal m_orbitVelocity, but we can check if it persists after a gap
    
    // Pause mid-drag
    g_mockTime += 0.2;
    fc.OnMouseMove(511.0, 500.0, g_mockTime); // Move slightly after gap
    
    // The previous momentum should be cleared by isGap reset
    // Release immediately
    fc.OnMouseUp(0, g_mockTime);
    
    // Check Momentum (Should be very small or 0, definitely not the 10px/0.01s move from before)
    double h1 = cam.GetHeading();
    fc.Update(0.016, g_mockTime);
    double h2 = cam.GetHeading();
    
    // It shouldn't have the high momentum from the first 10px move.
    // The last move was only 1px over a "gap" or newly started.
    if (std::abs(h2 - h1) > 0.1) { 
        throw std::runtime_error("Mid-Drag Gap failed: Large stale momentum persisted");
    }
    std::cout << "  [PASS] Mid-Drag Gap Verified" << std::endl;
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return 1;
    }
    
    try {
        TestDeadzone();
        TestPivotMomentum();
        TestTiltLimits();
        TestMomentumGap();
        TestMidDragGap();
        std::cout << "\nAll Navigation tests PASSED." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED: " << e.what() << std::endl;
        return 1;
    }
    
    glfwTerminate();
    return 0;
}
