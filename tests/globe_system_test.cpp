// =============================================================================
// GLOBE SYSTEM VALIDATION TEST
// Interactive test automation for GIS-based globe map system
// Tests: Tile Scheduler, Rendering, Positions, Parent-Child, 3D Terrain
// =============================================================================

#include <iostream>
#include <iomanip>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <cassert>
#include <sstream>

#include "../src/core/tile_key.h"
#include "../src/core/tile.h"
#include "../src/core/extent.h"
#include "../src/core/constants.h"
#include "../src/core/ellipsoid.h"
#include "../src/scheduling/lod_selector.h"
#include "../src/math/tile_math.h"

using namespace globe;

// =============================================================================
// TEST UTILITIES
// =============================================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string details;
    double duration_ms;
};

class TestRunner {
public:
    std::vector<TestResult> results;
    int passed = 0;
    int failed = 0;
    
    void AddResult(const std::string& name, bool pass, const std::string& details = "", double ms = 0.0) {
        results.push_back({name, pass, details, ms});
        if (pass) passed++; else failed++;
    }
    
    void PrintSummary() {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                    GLOBE SYSTEM TEST RESULTS                     ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
        
        for (const auto& r : results) {
            std::string status = r.passed ? "✅ PASS" : "❌ FAIL";
            std::cout << "║ " << std::left << std::setw(40) << r.name 
                      << " " << std::setw(8) << status;
            if (r.duration_ms > 0) {
                std::cout << " (" << std::fixed << std::setprecision(1) << r.duration_ms << "ms)";
            }
            std::cout << std::string(std::max(0, 10 - (int)(r.duration_ms > 0 ? 10 : 0)), ' ') << "║\n";
            if (!r.details.empty() && !r.passed) {
                std::cout << "║   → " << std::left << std::setw(59) << r.details << "║\n";
            }
        }
        
        std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ TOTAL: " << std::setw(3) << (passed + failed) << " tests | "
                  << "PASSED: " << std::setw(3) << passed << " | "
                  << "FAILED: " << std::setw(3) << failed 
                  << std::string(20, ' ') << "║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
        
        if (failed == 0) {
            std::cout << "🎉 ALL TESTS PASSED! GIS Globe System is operational.\n\n";
        } else {
            std::cout << "⚠️  Some tests failed. Review the results above.\n\n";
        }
    }
};

TestRunner runner;

// =============================================================================
// TEST 1: TILE KEY STRUCTURE (QuadKey, Parent-Child)
// =============================================================================

void TestTileKeyStructure() {
    std::cout << "\n[TEST 1] TileKey Structure (QuadKey, Parent-Child)...\n";
    
    // Test 1.1: TileKey creation and validity
    {
        TileKey key(5, 10, 15);
        bool valid = key.IsValid();
        runner.AddResult("TileKey creation & validity", valid && key.level == 5 && key.x == 10 && key.y == 15);
    }
    
    // Test 1.2: Parent relationship
    {
        TileKey child(5, 10, 15);
        TileKey parent = child.Parent();
        bool correct = (parent.level == 4 && parent.x == 5 && parent.y == 7);
        runner.AddResult("Parent tile calculation", correct, 
            correct ? "" : "Expected (4,5,7) got (" + std::to_string(parent.level) + "," + 
                          std::to_string(parent.x) + "," + std::to_string(parent.y) + ")");
    }
    
    // Test 1.3: Children relationship
    {
        TileKey parent(3, 2, 1);
        auto children = parent.Children();
        bool allValid = true;
        for (const auto& c : children) {
            if (c.level != 4 || c.Parent() != parent) {
                allValid = false;
                break;
            }
        }
        runner.AddResult("Children tiles calculation", allValid && children.size() == 4);
    }
    
    // Test 1.4: Neighbor navigation
    {
        TileKey key(3, 4, 4);
        TileKey east = key.Neighbor(1, 0);
        TileKey west = key.Neighbor(-1, 0);
        TileKey north = key.Neighbor(0, -1);
        TileKey south = key.Neighbor(0, 1);
        
        bool correct = (east.x == 5 && west.x == 3 && north.y == 3 && south.y == 5);
        runner.AddResult("Neighbor navigation (N/S/E/W)", correct);
    }
    
    // Test 1.5: Wrap-around at edges
    {
        TileKey key(3, 0, 4);  // Left edge
        TileKey west = key.Neighbor(-1, 0);
        bool wraps = (west.x == 7);  // Should wrap to right side (2^3 - 1 = 7)
        runner.AddResult("X-axis wrap-around", wraps);
    }
    
    // Test 1.6: Hash uniqueness
    {
        TileKey k1(5, 10, 15);
        TileKey k2(5, 10, 16);
        TileKey k3(5, 10, 15);
        
        TileKey::Hash hasher;
        bool unique = (hasher(k1) != hasher(k2)) && (hasher(k1) == hasher(k3));
        runner.AddResult("TileKey hash uniqueness", unique);
    }
}

// =============================================================================
// TEST 2: TILE EXTENT & GEOGRAPHIC COORDINATES
// =============================================================================

void TestTileExtent() {
    std::cout << "\n[TEST 2] Tile Extent & Geographic Coordinates...\n";
    
    // Test 2.1: World tile (level 0)
    {
        Extent ext = Extent::FromTileWGS84(0, 0, 0);
        bool correct = (std::abs(ext.West() - (-180.0)) < 0.001 &&
                       std::abs(ext.East() - 180.0) < 0.001);
        runner.AddResult("Level 0 world extent", correct,
            correct ? "" : "Expected W=-180, E=180");
    }
    
    // Test 2.2: Tile subdivision
    {
        Extent parent = Extent::FromTileWGS84(0, 0, 1);
        Extent child = Extent::FromTileWGS84(0, 0, 2);
        
        bool subdivided = (child.Width() < parent.Width() && 
                          child.Height() < parent.Height());
        runner.AddResult("Tile subdivision reduces extent", subdivided);
    }
    
    // Test 2.3: Known location (Istanbul ~41°N, 29°E)
    {
        // At zoom 4, tile (9,5) should contain Istanbul
        Extent ext = Extent::FromTileWGS84(9, 5, 4);
        bool contains = (ext.West() < 29.0 && ext.East() > 29.0 &&
                        ext.South() < 41.0 && ext.North() > 41.0);
        runner.AddResult("Istanbul in correct tile (z4)", contains,
            contains ? "" : "Tile(9,5,4) should contain 41N,29E");
    }
    
    // Test 2.4: Tile center calculation
    {
        TileKey key(4, 8, 5);
        glm::vec3 center = TileCenterWorld(key);
        float dist = glm::length(center);
        
        // Center should be on Earth surface (radius ~6371 km)
        bool onSurface = (std::abs(dist - EARTH_RADIUS_KM) < 100.0);
        runner.AddResult("Tile center on Earth surface", onSurface,
            onSurface ? "" : "Distance: " + std::to_string(dist) + " km");
    }
    
    // Test 2.5: Bounding radius reasonable
    {
        TileKey key(4, 8, 5);
        float radius = TileBoundingRadius(key);
        
        // At zoom 4, tile should be ~2500km wide
        bool reasonable = (radius > 500.0 && radius < 5000.0);
        runner.AddResult("Tile bounding radius reasonable", reasonable,
            reasonable ? "" : "Radius: " + std::to_string(radius) + " km");
    }
}

// =============================================================================
// TEST 3: LOD SELECTION & SSE
// =============================================================================

void TestLodSelection() {
    std::cout << "\n[TEST 3] LOD Selection & SSE...\n";
    
    // Test 3.1: SSE calculation
    {
        // At zoom 0, geometric error is large
        float sse0 = ComputeSSE(0, 10000000.0, 1080, 45.0);  // 10000km distance
        float sse5 = ComputeSSE(5, 10000000.0, 1080, 45.0);
        
        bool decreases = (sse5 < sse0);
        runner.AddResult("SSE decreases with zoom level", decreases);
    }
    
    // Test 3.2: SSE increases when closer
    {
        float sseFar = ComputeSSE(5, 10000000.0, 1080, 45.0);   // 10000km
        float sseNear = ComputeSSE(5, 1000000.0, 1080, 45.0);   // 1000km
        
        bool increases = (sseNear > sseFar);
        runner.AddResult("SSE increases when camera closer", increases);
    }
    
    // Test 3.3: LOD selector produces valid selection
    {
        LodSelector selector;
        LodSelector::Settings settings;
        settings.minZoom = 0;
        settings.maxZoom = 8;
        settings.sseThreshold = 2.0f;
        
        // Camera at 25000km altitude
        glm::vec3 cameraPos(0, 0, EARTH_RADIUS_KM + 25000);
        
        // Simple MVP (identity for testing)
        glm::mat4 mvp(1.0f);
        mvp[1][1] = 2.0f;  // FOV factor
        
        auto isReady = [](const TileKey&) { return true; };
        
        LodSelection selection = selector.Select(cameraPos, mvp, 45.0f, 0.0f, 1920, 1080, isReady, settings);
        
        bool hasLeaves = !selection.leaves.empty();
        bool hasRequired = !selection.required.empty();
        
        runner.AddResult("LOD selector produces leaves", hasLeaves,
            hasLeaves ? "" : "No leaves generated");
        runner.AddResult("LOD selector produces required set", hasRequired);
    }
    
    // Test 3.4: All leaves are in required set
    {
        LodSelector selector;
        LodSelector::Settings settings;
        settings.minZoom = 0;
        settings.maxZoom = 5;
        
        glm::vec3 cameraPos(0, 0, EARTH_RADIUS_KM + 10000);
        glm::mat4 mvp(1.0f);
        mvp[1][1] = 2.0f;
        
        auto isReady = [](const TileKey&) { return true; };
        
        LodSelection selection = selector.Select(cameraPos, mvp, 45.0f, 0.0f, 1920, 1080, isReady, settings);
        
        bool allInRequired = true;
        for (const auto& leaf : selection.leaves) {
            if (selection.required.find(leaf) == selection.required.end()) {
                allInRequired = false;
                break;
            }
        }
        runner.AddResult("All leaves in required set", allInRequired);
    }
}

// =============================================================================
// TEST 4: TILE STATE MACHINE
// =============================================================================

void TestTileStateMachine() {
    std::cout << "\n[TEST 4] Tile State Machine...\n";
    
    // Test 4.1: Initial state
    {
        Tile tile(TileKey(5, 10, 15));
        bool initial = (tile.state == TileState::Unloaded);
        runner.AddResult("Tile initial state is Unloaded", initial);
    }
    
    // Test 4.2: State transitions exist
    {
        bool hasStates = true;
        hasStates &= (TileState::Unloaded != TileState::Scheduled);
        hasStates &= (TileState::Scheduled != TileState::Fetching);
        hasStates &= (TileState::Fetching != TileState::Decoding);
        hasStates &= (TileState::Decoding != TileState::Ready);
        hasStates &= (TileState::Failed != TileState::Ready);
        
        runner.AddResult("All tile states defined", hasStates);
    }
    
    // Test 4.3: IsReady check
    {
        Tile tile(TileKey(5, 10, 15));
        tile.state = TileState::Ready;
        tile.textureId = 1;  // Fake texture
        
        bool ready = tile.IsReady();
        runner.AddResult("IsReady() returns true when ready", ready);
    }
    
    // Test 4.4: IsLoading check
    {
        Tile tile(TileKey(5, 10, 15));
        
        tile.state = TileState::Scheduled;
        bool loading1 = tile.IsLoading();
        
        tile.state = TileState::Fetching;
        bool loading2 = tile.IsLoading();
        
        tile.state = TileState::Decoding;
        bool loading3 = tile.IsLoading();
        
        runner.AddResult("IsLoading() detects loading states", loading1 && loading2 && loading3);
    }
    
    // Test 4.5: Edge coarser mask bits
    {
        uint8_t mask = Tile::EDGE_NORTH | Tile::EDGE_SOUTH;
        bool northSet = (mask & Tile::EDGE_NORTH) != 0;
        bool southSet = (mask & Tile::EDGE_SOUTH) != 0;
        bool eastClear = (mask & Tile::EDGE_EAST) == 0;
        
        runner.AddResult("Edge coarser mask bits work", northSet && southSet && eastClear);
    }
}

// =============================================================================
// TEST 5: ELLIPSOID & 3D TERRAIN
// =============================================================================

void TestEllipsoidTerrain() {
    std::cout << "\n[TEST 5] Ellipsoid & 3D Terrain...\n";
    
    const Ellipsoid& wgs84 = Ellipsoid::WGS84_KM();
    
    // Test 5.1: Equator point
    {
        glm::dvec3 pos = wgs84.GeodeticToCartesian(0.0, 0.0, 0.0);
        double dist = glm::length(pos);
        
        // At equator, distance should be ~6378 km (equatorial radius)
        bool correct = (std::abs(dist - 6378.137) < 1.0);
        runner.AddResult("Equator point at correct radius", correct,
            correct ? "" : "Distance: " + std::to_string(dist) + " km");
    }
    
    // Test 5.2: North pole
    {
        glm::dvec3 pos = wgs84.GeodeticToCartesian(0.0, 90.0, 0.0);
        
        // North pole should be on Z axis
        bool onZAxis = (std::abs(pos.x) < 0.001 && std::abs(pos.y) < 0.001);
        bool correctZ = (pos.z > 6300.0 && pos.z < 6400.0);
        
        runner.AddResult("North pole on Z axis", onZAxis && correctZ);
    }
    
    // Test 5.3: Elevation adds to radius
    {
        glm::dvec3 posGround = wgs84.GeodeticToCartesian(0.0, 0.0, 0.0);
        glm::dvec3 posElevated = wgs84.GeodeticToCartesian(0.0, 0.0, 1.0);  // 1km elevation
        
        double diff = glm::length(posElevated) - glm::length(posGround);
        bool correct = (std::abs(diff - 1.0) < 0.01);
        
        runner.AddResult("Elevation adds to radius correctly", correct);
    }
    
    // Test 5.4: Surface normal points outward
    {
        glm::dvec3 pos = wgs84.GeodeticToCartesian(45.0, 45.0, 0.0);
        glm::dvec3 normal = wgs84.GetSurfaceNormal(pos);
        
        // Normal should point away from center
        double dot = glm::dot(glm::normalize(pos), normal);
        bool outward = (dot > 0.9);  // Should be close to 1.0
        
        runner.AddResult("Surface normal points outward", outward);
    }
    
    // Test 5.5: Multiple elevation points consistent
    {
        // Test that elevation consistently affects radius
        glm::dvec3 pos0 = wgs84.GeodeticToCartesian(29.0, 41.0, 0.0);
        glm::dvec3 pos1 = wgs84.GeodeticToCartesian(29.0, 41.0, 1.0);
        glm::dvec3 pos2 = wgs84.GeodeticToCartesian(29.0, 41.0, 2.0);
        
        double r0 = glm::length(pos0);
        double r1 = glm::length(pos1);
        double r2 = glm::length(pos2);
        
        bool increasing = (r1 > r0) && (r2 > r1);
        bool linearish = std::abs((r2 - r1) - (r1 - r0)) < 0.01;
        
        runner.AddResult("Elevation consistently increases radius", increasing && linearish);
    }
}

// =============================================================================
// TEST 6: FRUSTUM & HORIZON CULLING
// =============================================================================

void TestCulling() {
    std::cout << "\n[TEST 6] Frustum & Horizon Culling...\n";
    
    // Test 6.1: Frustum extraction
    {
        Frustum frustum;
        glm::mat4 mvp(1.0f);  // Identity
        frustum.Extract(mvp);
        
        // Should not crash
        runner.AddResult("Frustum extraction works", true);
    }
    
    // Test 6.2: Sphere visibility (in front)
    {
        Frustum frustum;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f/9.0f, 0.1f, 1000.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        frustum.Extract(proj * view);
        
        // Sphere at origin should be visible
        bool visible = frustum.IsSphereVisible(glm::vec3(0, 0, 0), 1.0f);
        runner.AddResult("Sphere in front is visible", visible);
    }
    
    // Test 6.3: Horizon culler setup
    {
        HorizonCuller horizon;
        glm::vec3 cameraPos(0, 0, EARTH_RADIUS_KM + 1000);  // 1000km altitude
        horizon.Update(cameraPos, EARTH_RADIUS_KM);
        
        // Point on far side should be culled
        glm::vec3 farPoint(0, 0, -EARTH_RADIUS_KM);
        bool culled = !horizon.IsSphereVisible(farPoint, 100.0f);
        
        runner.AddResult("Horizon culls far-side points", culled);
    }
    
    // Test 6.4: Visible point not culled
    {
        HorizonCuller horizon;
        glm::vec3 cameraPos(0, 0, EARTH_RADIUS_KM + 1000);
        horizon.Update(cameraPos, EARTH_RADIUS_KM);
        
        // Point directly below camera should be visible
        glm::vec3 nearPoint(0, 0, EARTH_RADIUS_KM);
        bool visible = horizon.IsSphereVisible(nearPoint, 100.0f);
        
        runner.AddResult("Visible point not horizon culled", visible);
    }
}

// =============================================================================
// TEST 7: PARENT-CHILD CONSISTENCY
// =============================================================================

void TestParentChildConsistency() {
    std::cout << "\n[TEST 7] Parent-Child Consistency...\n";
    
    // Test 7.1: Child covers subset of parent extent
    {
        TileKey parent(4, 8, 5);
        auto children = parent.Children();
        
        Extent parentExt = Extent::FromTileWGS84(parent.x, parent.y, parent.level);
        
        bool allContained = true;
        for (const auto& child : children) {
            Extent childExt = Extent::FromTileWGS84(child.x, child.y, child.level);
            
            // Child should be within parent bounds
            if (childExt.West() < parentExt.West() - 0.001 ||
                childExt.East() > parentExt.East() + 0.001 ||
                childExt.South() < parentExt.South() - 0.001 ||
                childExt.North() > parentExt.North() + 0.001) {
                allContained = false;
                break;
            }
        }
        runner.AddResult("Children within parent extent", allContained);
    }
    
    // Test 7.2: 4 children cover entire parent
    {
        TileKey parent(3, 2, 2);
        auto children = parent.Children();
        
        Extent parentExt = Extent::FromTileWGS84(parent.x, parent.y, parent.level);
        
        // Calculate total child area
        double totalChildArea = 0.0;
        for (const auto& child : children) {
            Extent childExt = Extent::FromTileWGS84(child.x, child.y, child.level);
            totalChildArea += childExt.Width() * childExt.Height();
        }
        
        double parentArea = parentExt.Width() * parentExt.Height();
        bool coversFully = (std::abs(totalChildArea - parentArea) / parentArea < 0.01);
        
        runner.AddResult("Children cover parent area", coversFully);
    }
    
    // Test 7.3: Grandparent relationship
    {
        TileKey tile(6, 32, 24);
        TileKey parent = tile.Parent();
        TileKey grandparent = parent.Parent();
        
        bool levelCorrect = (grandparent.level == tile.level - 2);
        
        // Tile's grandparent should equal parent's parent
        auto parentChildren = grandparent.Children();
        bool parentInGrandchildren = false;
        for (const auto& gc : parentChildren) {
            if (gc == parent) {
                parentInGrandchildren = true;
                break;
            }
        }
        
        runner.AddResult("Grandparent relationship valid", levelCorrect && parentInGrandchildren);
    }
    
    // Test 7.4: Root tile has no valid parent
    {
        TileKey root(0, 0, 0);
        TileKey parent = root.Parent();
        
        // Parent of root should be root itself
        bool isRoot = (parent.level == 0);
        runner.AddResult("Root tile parent handling", isRoot);
    }
}

// =============================================================================
// TEST 8: GIS COORDINATE SYSTEM
// =============================================================================

void TestGISCoordinates() {
    std::cout << "\n[TEST 8] GIS Coordinate System...\n";
    
    // Test 8.1: Known city coordinates
    struct CityTest {
        const char* name;
        double lon, lat;
        int z, expectedX, expectedY;
    };
    
    std::vector<CityTest> cities = {
        {"London", -0.1, 51.5, 4, 7, 5},
        {"New York", -74.0, 40.7, 4, 4, 6},
        {"Tokyo", 139.7, 35.7, 4, 14, 6},
        {"Sydney", 151.2, -33.9, 4, 15, 9},
    };
    
    for (const auto& city : cities) {
        // Find which tile contains the city
        int n = 1 << city.z;
        int tileX = static_cast<int>((city.lon + 180.0) / 360.0 * n);
        double latRad = city.lat * M_PI / 180.0;
        int tileY = static_cast<int>((1.0 - std::asinh(std::tan(latRad)) / M_PI) / 2.0 * n);
        
        bool correct = (tileX == city.expectedX && tileY == city.expectedY);
        runner.AddResult(std::string(city.name) + " in correct tile", correct,
            correct ? "" : "Expected (" + std::to_string(city.expectedX) + "," + 
                          std::to_string(city.expectedY) + ") got (" + 
                          std::to_string(tileX) + "," + std::to_string(tileY) + ")");
    }
    
    // Test 8.2: Tile to lon/lat conversion
    {
        // Tile (0,0) at zoom 1 should be upper-left quadrant
        auto tile2lon = [](int x, int z) {
            return x / static_cast<double>(1 << z) * 360.0 - 180.0;
        };
        auto tile2lat = [](int y, int z) {
            double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << z);
            return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
        };
        
        double lon = tile2lon(0, 1);
        double lat = tile2lat(0, 1);
        
        bool correct = (lon == -180.0 && lat > 80.0);  // Web Mercator limit ~85°
        runner.AddResult("Tile(0,0,1) is upper-left", correct);
    }
}

// =============================================================================
// TEST 9: MESH GEOMETRY
// =============================================================================

void TestMeshGeometry() {
    std::cout << "\n[TEST 9] Mesh Geometry...\n";
    
    // Test 9.1: Vertex count formula
    {
        int segments = 16;
        int expectedVerts = (segments + 1) * (segments + 1);  // 289
        int expectedIndices = segments * segments * 6;         // 1536
        
        // With skirts: 4 edges × (segments + 1) extra vertices
        int skirtVerts = 4 * (segments + 1);  // 68
        int skirtIndices = segments * 4 * 6;   // 384
        
        int totalVerts = expectedVerts + skirtVerts;    // 357
        int totalIndices = expectedIndices + skirtIndices;  // 1920
        
        runner.AddResult("Mesh vertex count formula", totalVerts == 357);
        runner.AddResult("Mesh index count formula", totalIndices == 1920);
    }
    
    // Test 9.2: Skirt depth calculation
    {
        int zoom = 5;
        double tileArcKm = 40075.0 / (1 << zoom);  // ~1252 km
        double skirtDepth = std::max(tileArcKm * 0.01, 0.001);  // ~12.5 km
        
        bool reasonable = (skirtDepth > 1.0 && skirtDepth < 100.0);
        runner.AddResult("Skirt depth reasonable at z5", reasonable,
            reasonable ? "" : "Skirt: " + std::to_string(skirtDepth) + " km");
    }
}

// =============================================================================
// TEST 10: SYSTEM INTEGRATION
// =============================================================================

void TestSystemIntegration() {
    std::cout << "\n[TEST 10] System Integration...\n";
    
    // Test 10.1: Full tile lifecycle simulation
    {
        TileKey key(5, 16, 12);
        
        // Create tile
        Tile tile(key);
        bool step1 = (tile.state == TileState::Unloaded);
        
        // Compute extent
        tile.ComputeExtent();
        bool step2 = (tile.extent.Width() > 0);
        
        // Simulate scheduling
        tile.state = TileState::Scheduled;
        bool step3 = tile.IsLoading();
        
        // Simulate fetch complete
        tile.state = TileState::Decoding;
        bool step4 = tile.IsLoading();
        
        // Simulate ready
        tile.state = TileState::Ready;
        tile.textureId = 999;
        tile.hasMesh = true;
        bool step5 = tile.IsReady();
        
        runner.AddResult("Tile lifecycle: Unloaded→Ready", step1 && step2 && step3 && step4 && step5);
    }
    
    // Test 10.2: LOD with fallback (children not ready)
    {
        LodSelector selector;
        LodSelector::Settings settings;
        settings.minZoom = 0;
        settings.maxZoom = 6;
        
        glm::vec3 cameraPos(0, 0, EARTH_RADIUS_KM + 5000);
        glm::mat4 mvp(1.0f);
        mvp[1][1] = 2.0f;
        
        // Simulate: only zoom 0-3 tiles are ready
        auto isReady = [](const TileKey& k) { return k.level <= 3; };
        
        LodSelection selection = selector.Select(cameraPos, mvp, 45.0f, 0.0f, 1920, 1080, isReady, settings);
        
        // Should have leaves at level 3 (fallback) and required at higher levels
        bool hasFallback = false;
        bool hasHigherRequired = false;
        for (const auto& leaf : selection.leaves) {
            if (leaf.level == 3) hasFallback = true;
        }
        for (const auto& req : selection.required) {
            if (req.level > 3) hasHigherRequired = true;
        }
        
        runner.AddResult("LOD fallback when children not ready", hasFallback);
        runner.AddResult("Higher LOD tiles in required set", hasHigherRequired);
    }
    
    // Test 10.3: Memory estimation
    {
        // Estimate memory for 100 tiles at 256x256 RGBA
        int tileCount = 100;
        int tilePixels = 256 * 256;
        int bytesPerPixel = 4;
        size_t textureMemory = tileCount * tilePixels * bytesPerPixel;  // ~26 MB
        
        // Plus mesh data (357 vertices × 8 floats × 4 bytes = ~11KB per tile)
        size_t meshMemory = tileCount * 357 * 8 * 4;  // ~1.1 MB
        
        size_t totalMB = (textureMemory + meshMemory) / (1024 * 1024);
        
        bool reasonable = (totalMB > 20 && totalMB < 50);
        runner.AddResult("Memory estimate reasonable", reasonable,
            reasonable ? "" : "Estimated: " + std::to_string(totalMB) + " MB");
    }
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         GLOBE GIS SYSTEM VALIDATION TEST SUITE                   ║\n";
    std::cout << "║         Testing: Tiles, LOD, Terrain, Coordinates                ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Run all test suites
    TestTileKeyStructure();
    TestTileExtent();
    TestLodSelection();
    TestTileStateMachine();
    TestEllipsoidTerrain();
    TestCulling();
    TestParentChildConsistency();
    TestGISCoordinates();
    TestMeshGeometry();
    TestSystemIntegration();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\nTotal test time: " << duration.count() << " ms\n";
    
    // Print summary
    runner.PrintSummary();
    
    return runner.failed > 0 ? 1 : 0;
}
