// Google Earth DEM Grid Order Test
// Phase 4: Validates that FetchDemTile returns heights in correct row-major order
//
// Grid ordering expectation:
// - gy=0 (row 0) = south edge of tile
// - gy=meshN-1 (last row) = north edge of tile
// - gx=0 (col 0) = west edge of tile
// - gx=meshN-1 (last col) = east edge of tile
//
// This matches DemGridData expectations and is consistent with Google Earth's
// coordinate system where lat increases northward.

#include "../src/io/providers/google_earth_dem_provider.h"
#include "../src/io/providers/i_elevation_provider.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

using namespace globe;

// Stub elevation provider that returns predictable elevations based on coordinates
class GridOrderTestElevationProvider : public IElevationProvider {
public:
    // Elevation formula: elevation = latitude * 1000 + longitude
    // This creates unique, predictable values for each coordinate pair
    // that can be verified in the output grid
    
    ElevationBatchResult BatchQuery(const std::vector<GeoPoint>& points,
                                    const ElevationOptions& opt) override {
        (void)opt; // Unused
        
        ElevationBatchResult result;
        result.heights.reserve(points.size());
        
        for (const auto& point : points) {
            // Unique elevation for each lat/lon: lat * 1000 + lon
            // Example: (0, 0) -> 0, (0, 1) -> 1, (1, 0) -> 1000, (1, 1) -> 1001
            double elevation = point.latDeg * 1000.0 + point.lonDeg;
            result.heights.push_back(elevation);
        }
        
        result.ok = true;
        return result;
    }
    
    bool SupportsBatchQuery() const override { return true; }
    const char* GetProviderName() const override { return "grid-order-test"; }
};

// Helper: Calculate expected elevation at a grid point
// Based on the linear interpolation formula in GenerateTileGrid
double CalculateExpectedElevation(const TileKey& key, int meshN, int gy, int gx) {
    // Tile bounds calculation (same as GoogleEarthDemProvider)
    double lonWest = (key.x / static_cast<double>(1 << key.level)) * 360.0 - 180.0;
    double lonEast = ((key.x + 1) / static_cast<double>(1 << key.level)) * 360.0 - 180.0;
    
    // Mercator Y to latitude
    auto TileYToLat = [](int y, int z) -> double {
        double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << z);
        return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    };
    
    double latNorth = TileYToLat(key.y, key.level);
    double latSouth = TileYToLat(key.y + 1, key.level);
    
    // Interpolation factors (same as provider)
    double v = (meshN == 1) ? 0.0 : static_cast<double>(gy) / (meshN - 1);
    double u = (meshN == 1) ? 0.0 : static_cast<double>(gx) / (meshN - 1);
    
    double lat = latSouth + (latNorth - latSouth) * v;
    double lon = lonWest + (lonEast - lonWest) * u;
    
    // Same formula as stub provider
    return lat * 1000.0 + lon;
}

int main() {
    std::cout << "=== Google Earth DEM Grid Order Test ===" << std::endl;
    std::cout << "Grid ordering: gy=0=south, gy=meshN-1=north, gx=0=west, gx=meshN-1=east" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // Test parameters
    const int meshN = 5;  // 5x5 grid for clear testing
    const int testLevel = 10;
    const int testX = 512;  // Middle of zoom 10
    const int testY = 512;
    
    TileKey testKey(testLevel, testX, testY);
    
    // Create provider with stub elevation provider
    auto elevationProvider = std::make_unique<GridOrderTestElevationProvider>();
    GoogleEarthDemProvider demProvider(std::move(elevationProvider), meshN);
    
    // Fetch tile
    DemGridData gridData;
    DemFetchResult fetchResult;
    bool success = demProvider.FetchDemTile(testKey, gridData, fetchResult);
    
    if (!success || !gridData.valid) {
        std::cout << "FAIL: FetchDemTile failed: " << fetchResult.errorMessage << std::endl;
        return 1;
    }
    
    // Verify grid dimensions
    if (gridData.meshN != meshN) {
        std::cout << "FAIL: meshN mismatch: expected " << meshN << ", got " << gridData.meshN << std::endl;
        failed++;
    } else {
        std::cout << "PASS: meshN = " << gridData.meshN << std::endl;
        passed++;
    }
    
    if (gridData.heights.size() != static_cast<size_t>(meshN * meshN)) {
        std::cout << "FAIL: heights size mismatch: expected " << (meshN * meshN) 
                  << ", got " << gridData.heights.size() << std::endl;
        failed++;
    } else {
        std::cout << "PASS: heights.size() = " << gridData.heights.size() << std::endl;
        passed++;
    }
    
    // Verify grid order by checking each point
    std::cout << "\nVerifying grid order (each point)..." << std::endl;
    
    bool allPointsCorrect = true;
    double maxError = 0.0;
    
    for (int gy = 0; gy < meshN; ++gy) {
        for (int gx = 0; gx < meshN; ++gx) {
            int index = gy * meshN + gx;
            double actualElevation = gridData.heights[index];
            double expectedElevation = CalculateExpectedElevation(testKey, meshN, gy, gx);
            
            double error = std::abs(actualElevation - expectedElevation);
            maxError = std::max(maxError, error);
            
            if (error > 0.001) {
                allPointsCorrect = false;
                std::cout << "  MISMATCH at gy=" << gy << ", gx=" << gx 
                          << ": expected " << expectedElevation 
                          << ", got " << actualElevation << std::endl;
            }
        }
    }
    
    if (allPointsCorrect) {
        std::cout << "PASS: All " << (meshN * meshN) << " grid points match expected values"
                  << " (max error: " << maxError << ")" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Grid order validation failed (see mismatches above)" << std::endl;
        failed++;
    }
    
    // Verify south-to-north progression (latitudes should increase with gy)
    std::cout << "\nVerifying south-to-north progression..." << std::endl;
    
    bool southToNorthCorrect = true;
    for (int gx = 0; gx < meshN; ++gx) {
        // Compare center columns for cleaner verification
        // South (gy=0) should have lower elevations than North (gy=meshN-1)
        // because elevation formula is lat * 1000 + lon
        double southElev = gridData.heights[0 * meshN + gx];  // gy=0 (south)
        double northElev = gridData.heights[(meshN - 1) * meshN + gx];  // gy=meshN-1 (north)
        
        if (southElev >= northElev) {
            std::cout << "  ERROR at gx=" << gx << ": south (" << southElev 
                      << ") >= north (" << northElev << ")" << std::endl;
            southToNorthCorrect = false;
        }
    }
    
    if (southToNorthCorrect) {
        std::cout << "PASS: South-to-north progression verified (elevation increases northward)" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: South-to-north progression incorrect" << std::endl;
        failed++;
    }
    
    // Verify west-to-east progression (longitudes should increase with gx)
    std::cout << "\nVerifying west-to-east progression..." << std::endl;
    
    bool westToEastCorrect = true;
    for (int gy = 0; gy < meshN; ++gy) {
        // West (gx=0) should have lower elevations than East (gx=meshN-1)
        // because elevation formula is lat * 1000 + lon
        double westElev = gridData.heights[gy * meshN + 0];  // gx=0 (west)
        double eastElev = gridData.heights[gy * meshN + (meshN - 1)];  // gx=meshN-1 (east)
        
        if (westElev >= eastElev) {
            std::cout << "  ERROR at gy=" << gy << ": west (" << westElev 
                      << ") >= east (" << eastElev << ")" << std::endl;
            westToEastCorrect = false;
        }
    }
    
    if (westToEastCorrect) {
        std::cout << "PASS: West-to-east progression verified (elevation increases eastward)" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: West-to-east progression incorrect" << std::endl;
        failed++;
    }
    
    // Verify min/max elevation consistency
    std::cout << "\nVerifying min/max elevation..." << std::endl;
    
    double actualMin = *std::min_element(gridData.heights.begin(), gridData.heights.end());
    double actualMax = *std::max_element(gridData.heights.begin(), gridData.heights.end());
    
    if (std::abs(actualMin - gridData.minHeight) < 0.001 &&
        std::abs(actualMax - gridData.maxHeight) < 0.001) {
        std::cout << "PASS: minHeight=" << gridData.minHeight << ", maxHeight=" << gridData.maxHeight << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: min/max mismatch: calculated min=" << actualMin 
                  << " vs stored min=" << gridData.minHeight
                  << ", calculated max=" << actualMax 
                  << " vs stored max=" << gridData.maxHeight << std::endl;
        failed++;
    }
    
    // Print grid visualization
    std::cout << "\nGrid visualization (elevation values):" << std::endl;
    std::cout << "Row 0 (SOUTH) to Row " << (meshN-1) << " (NORTH):" << std::endl;
    for (int gy = 0; gy < meshN; ++gy) {
        std::cout << "  gy=" << gy << ": ";
        for (int gx = 0; gx < meshN; ++gx) {
            int index = gy * meshN + gx;
            std::cout << std::fixed << std::setprecision(1) << gridData.heights[index];
            if (gx < meshN - 1) std::cout << " ";
        }
        if (gy == 0) std::cout << "  <- SOUTH EDGE";
        if (gy == meshN - 1) std::cout << "  <- NORTH EDGE";
        std::cout << std::endl;
    }
    std::cout << "         ^ WEST        ^ EAST" << std::endl;
    
    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
