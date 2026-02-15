// RockMesh Sanity Tests - P0 Distance Check Regression Pack
// Tests the mesh-origin based distance gate (not world origin)
// Minimal test - directly tests the distance check logic without full RockMeshManager

#include "../src/io/providers/rocktree_node_data_parser.h"
#include "../src/core/config.h"
#include <glm/glm.hpp>
#include <iostream>
#include <cassert>
#include <cmath>

using namespace globe;

// P0: Standalone distance check test (replicates BuildMesh logic)
// Returns: pair<isValid, errorMessage>
std::pair<bool, std::string> TestDistanceCheck(
    const std::vector<glm::dvec3>& worldPositions,
    double maxVertexDistanceFromMeshOriginKm,
    bool sanityEnabled
) {
    if (!sanityEnabled) {
        return {true, ""};  // Sanity disabled = always pass
    }
    
    // Compute mesh origin (center of bounding box)
    glm::dvec3 bboxMin(std::numeric_limits<double>::max());
    glm::dvec3 bboxMax(std::numeric_limits<double>::lowest());
    for (const auto& pos : worldPositions) {
        bboxMin = glm::min(bboxMin, pos);
        bboxMax = glm::max(bboxMax, pos);
    }
    glm::dvec3 originEcef = (bboxMin + bboxMax) * 0.5;
    
    // P0 Fix: Check vertex distance from mesh origin (not world origin)
    for (size_t i = 0; i < worldPositions.size(); ++i) {
        double distFromMeshOrigin = glm::length(worldPositions[i] - originEcef);
        if (distFromMeshOrigin > maxVertexDistanceFromMeshOriginKm) {
            std::string error = "Invalid mesh: vertex distance from mesh origin exceeds threshold (" +
                               std::to_string(static_cast<int>(distFromMeshOrigin)) + " km > " +
                               std::to_string(static_cast<int>(maxVertexDistanceFromMeshOriginKm)) + " km)";
            return {false, error};
        }
    }
    
    return {true, ""};
}

// Helper: Create a small mesh at Earth surface (~6378km from center)
std::vector<glm::dvec3> CreateEarthSurfaceMesh(double sizeMeters = 100.0) {
    // Earth radius ~6378 km
    const double earthRadiusKm = 6378.0;
    const double offsetKm = sizeMeters / 1000.0;  // Convert to km
    
    // Small square at Earth surface, centered at (0, 0, 6378)
    return {
        { -offsetKm, -offsetKm, earthRadiusKm },  // bottom-left
        {  offsetKm, -offsetKm, earthRadiusKm },  // bottom-right
        {  offsetKm,  offsetKm, earthRadiusKm },  // top-right
        { -offsetKm,  offsetKm, earthRadiusKm }   // top-left
    };
}

// Helper: Create mesh with local outlier (far from mesh origin but not Earth center)
// The mesh origin is at Earth surface (~6378km from world center)
// The outlier is outlierDistanceKm from the mesh origin (bbox center)
std::vector<glm::dvec3> CreateMeshWithLocalOutlier(double outlierDistanceKm = 400.0) {
    const double earthRadiusKm = 6378.0;
    
    // To get outlier at exactly outlierDistanceKm from mesh origin:
    // Place base mesh at one extreme, outlier at opposite extreme
    // Then origin is at midpoint, and outlier is outlierDistanceKm from origin
    double baseX = -outlierDistanceKm;  // Base mesh at -400km
    double outlierX = outlierDistanceKm; // Outlier at +400km
    
    std::vector<glm::dvec3> positions = {
        { baseX, 0.0, earthRadiusKm },  // Base mesh (tight cluster)
        { baseX, 0.0, earthRadiusKm },
        { baseX, 0.0, earthRadiusKm },
        { baseX, 0.0, earthRadiusKm }
    };
    
    // Add outlier at opposite extreme
    // Origin will be at (0, 0, 6378), outlier is at 400km from origin
    positions.push_back({ outlierX, 0.0, earthRadiusKm });
    
    return positions;
}

// Test 1: Normal mesh passes with default threshold (mesh-origin based)
bool Test_DistanceCheckUsesMeshOrigin_DefaultThreshold() {
    std::cout << "[TEST] DistanceCheckUsesMeshOrigin_DefaultThreshold..." << std::flush;
    
    auto positions = CreateEarthSurfaceMesh(100.0);  // 100m mesh
    auto [valid, error] = TestDistanceCheck(positions, 300.0, true);
    
    if (!valid) {
        std::cout << " FAIL (mesh rejected: " << error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 2: Local outlier rejected (mesh-origin based gate works)
bool Test_DistanceCheckRejectsLocalOutlier() {
    std::cout << "[TEST] DistanceCheckRejectsLocalOutlier..." << std::flush;
    
    // Outlier at 400km from mesh origin, threshold is 300km
    auto positions = CreateMeshWithLocalOutlier(400.0);
    auto [valid, error] = TestDistanceCheck(positions, 300.0, true);
    
    // Should fail - outlier exceeds 300km threshold from mesh origin
    if (valid) {
        std::cout << " FAIL (mesh should have been rejected)" << std::endl;
        return false;
    }
    
    // Check error message mentions mesh origin
    if (error.find("mesh origin") == std::string::npos) {
        std::cout << " FAIL (error message missing 'mesh origin': " << error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 3: Higher threshold allows previously-rejected mesh
bool Test_DistanceThresholdCustom() {
    std::cout << "[TEST] DistanceThresholdCustom..." << std::flush;
    
    // Same outlier at 400km, but now threshold is 500km
    auto positions = CreateMeshWithLocalOutlier(400.0);
    auto [valid, error] = TestDistanceCheck(positions, 500.0, true);
    
    // Should pass - 400km < 500km threshold
    if (!valid) {
        std::cout << " FAIL (mesh rejected with higher threshold: " << error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 4: Sanity disabled bypasses all checks
bool Test_SanityDisabledBypassesChecks() {
    std::cout << "[TEST] SanityDisabledBypassesChecks..." << std::flush;
    
    // Extreme outlier should pass when sanity is disabled
    auto positions = CreateMeshWithLocalOutlier(1000.0);  // 1000km outlier
    auto [valid, error] = TestDistanceCheck(positions, 300.0, false);  // Sanity disabled
    
    // Should pass - sanity checks disabled
    if (!valid) {
        std::cout << " FAIL (mesh rejected with sanity disabled: " << error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 5: Error message format verification
bool Test_ErrorMessageFormat() {
    std::cout << "[TEST] ErrorMessageFormat..." << std::flush;
    
    auto positions = CreateMeshWithLocalOutlier(400.0);
    auto [valid, error] = TestDistanceCheck(positions, 300.0, true);
    
    // Verify error message contains expected components
    bool hasMeshOrigin = error.find("mesh origin") != std::string::npos;
    bool hasExceeds = error.find("exceeds") != std::string::npos;
    bool hasKm = error.find("km") != std::string::npos;
    bool hasThreshold = error.find("300") != std::string::npos;
    
    if (!hasMeshOrigin || !hasExceeds || !hasKm || !hasThreshold) {
        std::cout << " FAIL (error message format incomplete: " << error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 6: Verify mesh at various Earth locations passes (regression for world-origin check)
bool Test_VariousEarthLocationsPass() {
    std::cout << "[TEST] VariousEarthLocationsPass..." << std::flush;
    
    const double earthRadiusKm = 6378.0;
    const double offsetKm = 0.05;  // 50 meters
    
    // Test meshes at different locations on Earth surface
    std::vector<std::vector<glm::dvec3>> testMeshes = {
        // North pole area
        { {-offsetKm, -offsetKm, earthRadiusKm}, {offsetKm, -offsetKm, earthRadiusKm},
          {offsetKm, offsetKm, earthRadiusKm}, {-offsetKm, offsetKm, earthRadiusKm} },
        // Equator, different longitudes
        { {earthRadiusKm, -offsetKm, -offsetKm}, {earthRadiusKm, offsetKm, -offsetKm},
          {earthRadiusKm, offsetKm, offsetKm}, {earthRadiusKm, -offsetKm, offsetKm} },
        // South pole area
        { {-offsetKm, -offsetKm, -earthRadiusKm}, {offsetKm, -offsetKm, -earthRadiusKm},
          {offsetKm, offsetKm, -earthRadiusKm}, {-offsetKm, offsetKm, -earthRadiusKm} },
    };
    
    for (size_t i = 0; i < testMeshes.size(); ++i) {
        auto [valid, error] = TestDistanceCheck(testMeshes[i], 300.0, true);
        if (!valid) {
            std::cout << " FAIL (mesh " << i << " rejected: " << error << ")" << std::endl;
            return false;
        }
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

int main() {
    std::cout << "=== RockMesh Sanity Tests (P0 Distance Check) ===" << std::endl;
    std::cout << "Testing mesh-origin based distance gate (not world origin)" << std::endl;
    std::cout << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    if (Test_DistanceCheckUsesMeshOrigin_DefaultThreshold()) passed++; else failed++;
    if (Test_DistanceCheckRejectsLocalOutlier()) passed++; else failed++;
    if (Test_DistanceThresholdCustom()) passed++; else failed++;
    if (Test_SanityDisabledBypassesChecks()) passed++; else failed++;
    if (Test_ErrorMessageFormat()) passed++; else failed++;
    if (Test_VariousEarthLocationsPass()) passed++; else failed++;
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    return failed > 0 ? 1 : 0;
}
