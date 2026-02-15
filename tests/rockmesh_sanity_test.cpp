// RockMesh Sanity Tests - P0 Distance Check Regression Pack
// 
// IMPORTANT: This test replicates the distance check logic from BuildMesh() to avoid
// pulling in heavy dependencies (GL, GLFW, workers, network). 
// If BuildMesh() implementation changes, this test MUST be updated to match.
//
// The test validates the P0 fix semantics:
// - OLD: distance from world origin (Earth center)
// - NEW: distance from mesh origin (bbox center)

#include <glm/glm.hpp>
#include <iostream>
#include <algorithm>

// Replicate BuildMesh distance check logic for testing
// NOTE: This must match src/rendering/rockmesh_manager.cpp BuildMesh() implementation
struct DistanceCheckResult {
    bool valid;
    std::string error;
    bool discarded;
};

DistanceCheckResult TestDistanceCheck(
    const std::vector<glm::dvec3>& worldPositions,
    double maxVertexDistanceFromMeshOriginKm,
    bool sanityEnabled
) {
    if (!sanityEnabled) {
        return {true, "", false};
    }
    
    // Compute mesh origin (center of bounding box) - P0 Fix
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
            return {false, error, true};
        }
    }
    
    return {true, "", false};
}

// Helper: Create a small mesh at specified location
// The mesh origin (bbox center) will be at 'centerKm'
std::vector<glm::dvec3> CreateSmallMeshAt(glm::dvec3 centerKm, double sizeKm = 0.1) {
    return {
        { centerKm.x - sizeKm, centerKm.y - sizeKm, centerKm.z },
        { centerKm.x + sizeKm, centerKm.y - sizeKm, centerKm.z },
        { centerKm.x + sizeKm, centerKm.y + sizeKm, centerKm.z },
        { centerKm.x - sizeKm, centerKm.y + sizeKm, centerKm.z }
    };
}

// Helper: Create mesh with outlier
// Base mesh at centerKm, outlier at centerKm + offsetKm
// With mesh-origin check: outlier is |offsetKm| from origin
std::vector<glm::dvec3> CreateMeshWithOutlier(glm::dvec3 centerKm, glm::dvec3 offsetKm) {
    // Create base mesh at one extreme
    double baseOffset = -glm::length(offsetKm);
    glm::dvec3 baseDir = glm::normalize(offsetKm);
    glm::dvec3 basePos = centerKm + baseDir * baseOffset;
    
    std::vector<glm::dvec3> positions = {
        basePos, basePos, basePos, basePos  // Degenerate base
    };
    
    // Add outlier at opposite extreme
    // Mesh origin will be at centerKm, outlier is |offsetKm| from origin
    positions.push_back(centerKm + offsetKm);
    
    return positions;
}

// Test 1: Normal Earth surface mesh passes (mesh-origin based, not world-origin)
bool Test_DistanceCheckUsesMeshOrigin() {
    std::cout << "[TEST] DistanceCheckUsesMeshOrigin..." << std::flush;
    
    // Mesh at Earth surface (6378km from world center), but small (100m)
    // With old world-origin check: distance = 6378km > 300km -> FAIL
    // With new mesh-origin check: distance from bbox center = 0.05km < 300km -> PASS
    auto positions = CreateSmallMeshAt({0, 0, 6378.0}, 0.05);
    auto result = TestDistanceCheck(positions, 300.0, true);
    
    if (!result.valid) {
        std::cout << " FAIL (rejected: " << result.error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 2: Local outlier rejected (mesh-origin based gate works)
bool Test_DistanceCheckRejectsLocalOutlier() {
    std::cout << "[TEST] DistanceCheckRejectsLocalOutlier..." << std::flush;
    
    // Mesh center at (0, 0, 6378), outlier at (400, 0, 6378)
    // Distance from mesh origin = 400km > 300km -> FAIL
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {400.0, 0, 0});
    auto result = TestDistanceCheck(positions, 300.0, true);
    
    if (result.valid) {
        std::cout << " FAIL (should have been rejected)" << std::endl;
        return false;
    }
    
    if (result.error.find("mesh origin") == std::string::npos) {
        std::cout << " FAIL (wrong error: " << result.error << ")" << std::endl;
        return false;
    }
    
    if (!result.discarded) {
        std::cout << " FAIL (discard flag not set)" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 3: Higher threshold allows previously-rejected mesh
bool Test_DistanceThresholdCustom() {
    std::cout << "[TEST] DistanceThresholdCustom..." << std::flush;
    
    // Same 400km outlier, but threshold is 500km -> PASS
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {400.0, 0, 0});
    auto result = TestDistanceCheck(positions, 500.0, true);
    
    if (!result.valid) {
        std::cout << " FAIL (rejected: " << result.error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 4: Sanity disabled bypasses all checks
bool Test_SanityDisabled() {
    std::cout << "[TEST] SanityDisabled..." << std::flush;
    
    // Extreme outlier should pass when sanity disabled
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {1000.0, 0, 0});
    auto result = TestDistanceCheck(positions, 300.0, false);
    
    if (!result.valid) {
        std::cout << " FAIL (rejected with sanity disabled)" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 5: Various Earth locations work with mesh-origin check
bool Test_VariousEarthLocations() {
    std::cout << "[TEST] VariousEarthLocations..." << std::flush;
    
    // Test multiple locations on Earth - all should pass with small meshes
    std::vector<glm::dvec3> locations = {
        {0, 0, 6378.0},        // North pole area
        {6378.0, 0, 0},        // Equator X
        {0, 6378.0, 0},        // Equator Y
        {-6378.0, 0, 0},       // Opposite side
        {0, 0, -6378.0},       // South pole area
    };
    
    for (size_t i = 0; i < locations.size(); ++i) {
        auto positions = CreateSmallMeshAt(locations[i], 0.05);
        auto result = TestDistanceCheck(positions, 300.0, true);
        
        if (!result.valid) {
            std::cout << " FAIL (location " << i << " rejected: " << result.error << ")" << std::endl;
            return false;
        }
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 6: Error message contains expected components
bool Test_ErrorMessageFormat() {
    std::cout << "[TEST] ErrorMessageFormat..." << std::flush;
    
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {400.0, 0, 0});
    auto result = TestDistanceCheck(positions, 300.0, true);
    
    if (result.valid) {
        std::cout << " FAIL (should have been rejected)" << std::endl;
        return false;
    }
    
    bool hasMeshOrigin = result.error.find("mesh origin") != std::string::npos;
    bool hasExceeds = result.error.find("exceeds") != std::string::npos;
    bool hasKm = result.error.find("km") != std::string::npos;
    bool hasThreshold = result.error.find("300") != std::string::npos;
    
    if (!hasMeshOrigin || !hasExceeds || !hasKm || !hasThreshold) {
        std::cout << " FAIL (incomplete: " << result.error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

int main() {
    std::cout << "=== RockMesh Sanity Tests (P0 Distance Check) ===" << std::endl;
    std::cout << "NOTE: This test replicates BuildMesh() distance check logic." << std::endl;
    std::cout << "If src/rendering/rockmesh_manager.cpp changes, update this test!" << std::endl;
    std::cout << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    if (Test_DistanceCheckUsesMeshOrigin()) passed++; else failed++;
    if (Test_DistanceCheckRejectsLocalOutlier()) passed++; else failed++;
    if (Test_DistanceThresholdCustom()) passed++; else failed++;
    if (Test_SanityDisabled()) passed++; else failed++;
    if (Test_VariousEarthLocations()) passed++; else failed++;
    if (Test_ErrorMessageFormat()) passed++; else failed++;
    
    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    return failed > 0 ? 1 : 0;
}
