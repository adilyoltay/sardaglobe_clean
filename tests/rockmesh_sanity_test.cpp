// RockMesh Sanity Tests - P0 Distance Check Regression Pack
// 
// ⚠️  IMPORTANT MAINTENANCE NOTE  ⚠️
// This test REPLICATES the distance check logic from BuildMesh() to avoid pulling
// in heavy dependencies (GL, GLFW, workers, network, disk cache, rate limiter, 
// octree index, HTTP client, etc.).
//
// If src/rendering/rockmesh_manager.cpp BuildMesh() distance check logic changes,
// this test MUST be updated to match.
//
// For actual BuildMesh() integration testing, BuildMeshForTest() API exists under
// NATIVE_GLOBE_TESTING, but requires linking the full RockMeshManager dependency
// tree which is impractical for a focused unit test.
//
// Target behavior validated:
// - P0 Fix: Distance from mesh origin (bbox center), NOT world origin (Earth center)
// - Threshold: rockMeshMaxVertexDistanceFromOriginKm (default: 300km from mesh origin)
// - Error message: Must contain "mesh origin"

#include <glm/glm.hpp>
#include <iostream>
#include <algorithm>
#include <string>

// Mirror of BuildMesh distance check logic
// UPDATE THIS if rockmesh_manager.cpp BuildMesh() changes!
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
    
    // P0 Fix: Compute mesh origin (center of bounding box)
    glm::dvec3 bboxMin(std::numeric_limits<double>::max());
    glm::dvec3 bboxMax(std::numeric_limits<double>::lowest());
    for (const auto& pos : worldPositions) {
        bboxMin = glm::min(bboxMin, pos);
        bboxMax = glm::max(bboxMax, pos);
    }
    glm::dvec3 originEcef = (bboxMin + bboxMax) * 0.5;
    
    // P0 Fix: Check vertex distance from mesh origin (NOT world origin)
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

// Create small mesh at specified Earth-surface location
std::vector<glm::dvec3> CreateSmallMeshAt(glm::dvec3 centerKm, double sizeKm = 0.05) {
    return {
        { centerKm.x - sizeKm, centerKm.y - sizeKm, centerKm.z },
        { centerKm.x + sizeKm, centerKm.y - sizeKm, centerKm.z },
        { centerKm.x + sizeKm, centerKm.y + sizeKm, centerKm.z },
        { centerKm.x - sizeKm, centerKm.y + sizeKm, centerKm.z }
    };
}

// Create mesh with local outlier
// Mesh origin at centerKm, outlier at centerKm + offsetKm
std::vector<glm::dvec3> CreateMeshWithOutlier(glm::dvec3 centerKm, glm::dvec3 offsetKm) {
    glm::dvec3 basePos = centerKm - offsetKm;
    std::vector<glm::dvec3> positions = { basePos, basePos, basePos, basePos };
    positions.push_back(centerKm + offsetKm);
    return positions;
}

// Test 1: Normal mesh at Earth surface passes with mesh-origin check
// Regression: World-origin check would FAIL (6378km > 300km)
bool Test_DistanceCheckUsesMeshOrigin() {
    std::cout << "[TEST] DistanceCheckUsesMeshOrigin..." << std::flush;
    
    auto positions = CreateSmallMeshAt({0, 0, 6378.0}, 0.05);
    auto result = TestDistanceCheck(positions, 300.0, true);
    
    if (!result.valid) {
        std::cout << " FAIL (rejected: " << result.error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 2: Local outlier rejected by mesh-origin check
bool Test_DistanceCheckRejectsLocalOutlier() {
    std::cout << "[TEST] DistanceCheckRejectsLocalOutlier..." << std::flush;
    
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {400.0, 0, 0});
    auto result = TestDistanceCheck(positions, 300.0, true);
    
    if (result.valid) {
        std::cout << " FAIL (should have been rejected)" << std::endl;
        return false;
    }
    
    if (result.error.find("mesh origin") == std::string::npos) {
        std::cout << " FAIL (error missing 'mesh origin': " << result.error << ")" << std::endl;
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
    
    auto positions = CreateMeshWithOutlier({0, 0, 6378.0}, {1000.0, 0, 0});
    auto result = TestDistanceCheck(positions, 300.0, false);
    
    if (!result.valid) {
        std::cout << " FAIL (rejected with sanity disabled)" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

// Test 5: Various Earth locations pass with mesh-origin check
bool Test_VariousEarthLocations() {
    std::cout << "[TEST] VariousEarthLocations..." << std::flush;
    
    std::vector<glm::dvec3> locations = {
        {0, 0, 6378.0},
        {6378.0, 0, 0},
        {0, 6378.0, 0},
        {-6378.0, 0, 0},
        {0, 0, -6378.0},
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

// Test 6: Error message format verification
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
    
    if (!hasMeshOrigin || !hasExceeds || !hasKm) {
        std::cout << " FAIL (incomplete: " << result.error << ")" << std::endl;
        return false;
    }
    
    std::cout << " PASS" << std::endl;
    return true;
}

int main() {
    std::cout << "=== RockMesh Sanity Tests (P0 Distance Check) ===" << std::endl;
    std::cout << "⚠️  NOTE: This test REPLICATES BuildMesh() logic." << std::endl;
    std::cout << "   If rockmesh_manager.cpp changes, update this test!" << std::endl;
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
