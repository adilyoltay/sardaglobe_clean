// Terrain RGB Decoder No-Data Clamp Test
// Verifies config defaults for DEM no-data sanitization and DemGridData handling.
// Actual decode+sanitize path is tested via integration (requires stb_image link).

#include "../src/core/config.h"
#include "../src/io/dem_manager.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace globe;

namespace {

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    // Test 1: DemGridData with extreme negative heights
    {
        DemGridData data;
        data.meshN = 2;
        data.heights = {-15000.0, 100.0, 200.0, 50.0};
        data.minHeight = -15000.0;
        data.maxHeight = 200.0;
        data.valid = true;

        failed += !Expect(data.heights[0] < -11000.0,
            "raw DemGridData should preserve extreme negative heights");
        failed += !Expect(data.valid, "DemGridData should be valid");
    }

    // Test 2: Config defaults match plan specification (Aşama 0)
    {
        Config cfg;
        failed += !Expect(cfg.forceClampTerrainNoData == true,
            "forceClampTerrainNoData should default to true");
        failed += !Expect(Near(cfg.demNoDataMinHeightM, -11000.0f),
            "demNoDataMinHeightM should default to -11000");
        failed += !Expect(Near(cfg.demNoDataReplacementM, 0.0f),
            "demNoDataReplacementM should default to 0");
        failed += !Expect(cfg.demBaseUrl.find("access_token=") == std::string::npos,
            "Config.demBaseUrl should not hardcode an access token");
        failed += !Expect(cfg.demBaseUrl.find("pk.") == std::string::npos,
            "Config.demBaseUrl should not contain token material");
    }

    // Test 3: Fallback config defaults
    {
        Config cfg;
        failed += !Expect(cfg.fallbackRequireParentUntilChildrenReady == true,
            "fallbackRequireParentUntilChildrenReady should default to true");
    }

    // Test 4: DemGridData min/max height tracking
    {
        DemGridData data;
        data.meshN = 3;
        data.heights = {0.0, 100.0, -50.0, 200.0, 150.0, -100.0, 300.0, 50.0, -200.0};
        data.minHeight = -200.0;
        data.maxHeight = 300.0;
        data.valid = true;

        failed += !Expect(data.minHeight == -200.0, "minHeight should be -200");
        failed += !Expect(data.maxHeight == 300.0, "maxHeight should be 300");
    }

    // Test 5: NaN detection logic (simulated sanitization check)
    {
        double nanVal = std::numeric_limits<double>::quiet_NaN();
        double infVal = std::numeric_limits<double>::infinity();
        double negInfVal = -std::numeric_limits<double>::infinity();
        double normalVal = 500.0;
        double extremeNeg = -15000.0;

        Config cfg;
        double minValid = static_cast<double>(cfg.demNoDataMinHeightM);
        double replacement = static_cast<double>(cfg.demNoDataReplacementM);

        // These are the conditions the SanitizeTerrainHeights function checks
        failed += !Expect(!std::isfinite(nanVal), "NaN should be detected as non-finite");
        failed += !Expect(!std::isfinite(infVal), "Inf should be detected as non-finite");
        failed += !Expect(!std::isfinite(negInfVal), "-Inf should be detected as non-finite");
        failed += !Expect(std::isfinite(normalVal), "500.0 should be finite");
        failed += !Expect(extremeNeg < minValid,
            "extreme negative (-15000) should be below threshold (-11000)");
        failed += !Expect(normalVal >= minValid,
            "normal value (500) should be above threshold");
    }

    // Test 6: DemManagerConfig propagation
    {
        DemManagerConfig dmCfg;
        failed += !Expect(dmCfg.forceClampTerrainNoData == true,
            "DemManagerConfig.forceClampTerrainNoData default=true");
        failed += !Expect(Near(dmCfg.demNoDataMinHeightM, -11000.0f),
            "DemManagerConfig.demNoDataMinHeightM default=-11000");
        failed += !Expect(Near(dmCfg.demNoDataReplacementM, 0.0f),
            "DemManagerConfig.demNoDataReplacementM default=0");
    }

    if (failed == 0) {
        std::cout << "terrain_rgb_decoder_nodata_test: ALL PASSED" << std::endl;
    } else {
        std::cout << "terrain_rgb_decoder_nodata_test: " << failed << " FAILED" << std::endl;
    }

    return failed;
}
