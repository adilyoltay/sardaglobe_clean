// RTE/RTC Tile Regression Test
// Faz 1B: Validates jitter-free rendering with Relative-to-Center encoding

#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// Minimal standalone test - no GL/DEM dependencies

namespace {

bool Near(float a, float b, float eps = 1e-4f) {
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

// Simulate tile extent
struct Extent {
    double west, east, south, north;
    
    static Extent FromTileWGS84(int x, int y, int z) {
        double n = 1 << z;
        double lonLeft = x * 360.0 / n - 180.0;
        double lonRight = (x + 1) * 360.0 / n - 180.0;
        
        double latTopRad = std::atan(std::sinh(M_PI * (1 - 2 * y / n)));
        double latBotRad = std::atan(std::sinh(M_PI * (1 - 2 * (y + 1) / n)));
        
        return Extent{lonLeft, lonRight, 
                      latBotRad * 180.0 / M_PI, 
                      latTopRad * 180.0 / M_PI};
    }
};

// WGS84 ellipsoid constants
constexpr double WGS84_A_KM = 6378.137;

// Convert geodetic to ECEF
void GeodeticToCartesian(double lon, double lat, double alt, double& x, double& y, double& z) {
    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    
    double R = WGS84_A_KM + alt * 0.001;  // Convert m to km
    x = R * std::cos(latRad) * std::cos(lonRad);
    y = R * std::cos(latRad) * std::sin(lonRad);
    z = R * std::sin(latRad);
}

} // namespace

int main() {
    int failures = 0;
    
    // Test 1: Tile bbox and origin computation
    {
        // Tile z=5, x=16, y=16 (approx at equator)
        auto extent = Extent::FromTileWGS84(16, 16, 5);
        
        // Sample positions on tile grid
        std::vector<glm::dvec3> worldPositions;
        for (int iy = 0; iy <= 4; ++iy) {
            for (int ix = 0; ix <= 4; ++ix) {
                double u = ix / 4.0;
                double v = iy / 4.0;
                double lon = extent.west + (extent.east - extent.west) * u;
                double lat = extent.south + (extent.north - extent.south) * v;
                
                double x, y, z;
                GeodeticToCartesian(lon, lat, 0.0, x, y, z);
                worldPositions.push_back(glm::dvec3(x, y, z));
            }
        }
        
        // Compute bbox and origin (as TileMeshBuilder does)
        glm::dvec3 bboxMin(1e18), bboxMax(-1e18);
        for (const auto& pos : worldPositions) {
            bboxMin = glm::min(bboxMin, pos);
            bboxMax = glm::max(bboxMax, pos);
        }
        glm::dvec3 originEcef = (bboxMin + bboxMax) * 0.5;
        
        // Split origin
        glm::vec3 originHi = glm::vec3(originEcef);
        glm::vec3 originLo = glm::vec3(originEcef - glm::dvec3(originHi));
        
        float originLen = glm::length(originHi + originLo);
        if (!Expect(Near(originLen, 6378.0f, 100.0f), "Origin should be near Earth surface")) failures++;
        
        float hiLen = glm::length(originHi);
        float loLen = glm::length(originLo);
        
        if (!Expect(hiLen > 6000.0f, "Hi component should contain major magnitude")) failures++;
        if (!Expect(loLen < 100.0f, "Lo component should be small")) failures++;
        
        if (Near(originLen, 6378.0f, 100.0f) && hiLen > 6000.0f && loLen < 100.0f) {
            Report("TileOriginComputation");
        }
    }
    
    // Test 2: Vertices relative to origin
    {
        auto extent = Extent::FromTileWGS84(16, 16, 5);
        
        std::vector<glm::dvec3> worldPositions;
        for (int iy = 0; iy <= 4; ++iy) {
            for (int ix = 0; ix <= 4; ++ix) {
                double u = ix / 4.0;
                double v = iy / 4.0;
                double lon = extent.west + (extent.east - extent.west) * u;
                double lat = extent.south + (extent.north - extent.south) * v;
                
                double x, y, z;
                GeodeticToCartesian(lon, lat, 0.0, x, y, z);
                worldPositions.push_back(glm::dvec3(x, y, z));
            }
        }
        
        // Compute origin
        glm::dvec3 bboxMin(1e18), bboxMax(-1e18);
        for (const auto& pos : worldPositions) {
            bboxMin = glm::min(bboxMin, pos);
            bboxMax = glm::max(bboxMax, pos);
        }
        glm::dvec3 originEcef = (bboxMin + bboxMax) * 0.5;
        
        // Check relative positions
        bool allRelative = true;
        bool allReconstruct = true;
        
        for (const auto& world : worldPositions) {
            glm::vec3 relPos = glm::vec3(world - originEcef);
            float relLen = glm::length(relPos);
            
            // Relative positions should be small (tile size ~200-500km at level 5)
            if (relLen >= 1000.0f) {
                allRelative = false;
                break;
            }
            
            // Reconstruct and check
            glm::vec3 reconstructed = glm::vec3(originEcef) + relPos;
            float error = glm::length(reconstructed - glm::vec3(world));
            if (error > 1e-3f) {
                allReconstruct = false;
                break;
            }
        }
        
        if (!Expect(allRelative, "Vertices should be relative to origin")) failures++;
        if (!Expect(allReconstruct, "Reconstruction should be accurate")) failures++;
        
        if (allRelative && allReconstruct) Report("VerticesAreRelativeToOrigin");
    }
    
    // Test 3: High zoom tiles have smaller relative positions
    {
        // Level 15 tile (much smaller)
        auto extent15 = Extent::FromTileWGS84(16384, 16384, 15);
        
        std::vector<glm::dvec3> positions15;
        for (int iy = 0; iy <= 4; ++iy) {
            for (int ix = 0; ix <= 4; ++ix) {
                double u = ix / 4.0;
                double v = iy / 4.0;
                double lon = extent15.west + (extent15.east - extent15.west) * u;
                double lat = extent15.south + (extent15.north - extent15.south) * v;
                
                double x, y, z;
                GeodeticToCartesian(lon, lat, 0.0, x, y, z);
                positions15.push_back(glm::dvec3(x, y, z));
            }
        }
        
        // Compute origin and max relative
        glm::dvec3 bboxMin(1e18), bboxMax(-1e18);
        for (const auto& pos : positions15) {
            bboxMin = glm::min(bboxMin, pos);
            bboxMax = glm::max(bboxMax, pos);
        }
        glm::dvec3 origin15 = (bboxMin + bboxMax) * 0.5;
        
        float maxRelLen15 = 0.0f;
        for (const auto& world : positions15) {
            glm::vec3 relPos = glm::vec3(world - origin15);
            maxRelLen15 = std::max(maxRelLen15, glm::length(relPos));
        }
        
        // Level 5 tile for comparison
        auto extent5 = Extent::FromTileWGS84(16, 16, 5);
        std::vector<glm::dvec3> positions5;
        for (int iy = 0; iy <= 4; ++iy) {
            for (int ix = 0; ix <= 4; ++ix) {
                double u = ix / 4.0;
                double v = iy / 4.0;
                double lon = extent5.west + (extent5.east - extent5.west) * u;
                double lat = extent5.south + (extent5.north - extent5.south) * v;
                
                double x, y, z;
                GeodeticToCartesian(lon, lat, 0.0, x, y, z);
                positions5.push_back(glm::dvec3(x, y, z));
            }
        }
        
        bboxMin = glm::dvec3(1e18); bboxMax = glm::dvec3(-1e18);
        for (const auto& pos : positions5) {
            bboxMin = glm::min(bboxMin, pos);
            bboxMax = glm::max(bboxMax, pos);
        }
        glm::dvec3 origin5 = (bboxMin + bboxMax) * 0.5;
        
        float maxRelLen5 = 0.0f;
        for (const auto& world : positions5) {
            glm::vec3 relPos = glm::vec3(world - origin5);
            maxRelLen5 = std::max(maxRelLen5, glm::length(relPos));
        }
        
        // Level 15 should have much smaller relative positions
        if (!Expect(maxRelLen15 < maxRelLen5, "High zoom tiles should have smaller relative positions")) failures++;
        if (!Expect(maxRelLen15 < 200.0f, "Level 15 tile should be <200km in extent")) failures++;
        
        if (maxRelLen15 < maxRelLen5 && maxRelLen15 < 200.0f) {
            Report("HighZoomTilePrecision");
        }
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll RTE Tile tests PASSED\n";
    return 0;
}
