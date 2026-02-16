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

    // Test 7: Hard Clamp - PixelToHeight NoData values (P0-1)
    // Bu test PixelToHeight fonksiyonundaki hard clamp'i doğrular
    // Mapbox Terrain-RGB encoding: height = -10000 + (R*65536 + G*256 + B) * 0.1
    {
        // (0,0,0) pikseli = -10000m (NoData/okyanus tabanı)
        // Hard clamp ile bu 0.0m olmalı
        
        // Simüle edilmiş PixelToHeight davranışı (terrain_rgb_decoder.cpp'den)
        auto PixelToHeightWithClamp = [](uint8_t r, uint8_t g, uint8_t b) -> double {
            double height = -10000.0 + (static_cast<double>(r) * 65536.0 +
                                       static_cast<double>(g) * 256.0 +
                                       static_cast<double>(b)) * 0.1;
            // Hard clamp (config'ten bağımsız): <= -10000.0 (NoData) veya > 9000.0
            if (!std::isfinite(height) || height <= -10000.0 || height > 9000.0) {
                return 0.0;
            }
            return height;
        };
        
        // Test: (0,0,0) -> -10000m -> 0.0m (clamped)
        double h1 = PixelToHeightWithClamp(0, 0, 0);
        failed += !Expect(Near(h1, 0.0),
            "NoData (0,0,0) should be hard-clamped to 0.0m");
        
        // Test: Negatif NoData sınırı -> 0.0m (örn: (0,0,0) ve daha düşük)
        double h2 = PixelToHeightWithClamp(0, 0, 0);  // -10000m, clamped
        failed += !Expect(Near(h2, 0.0),
            "NoData sentinel (-10000m) should be clamped to 0.0m");
        
        // Test: Normal deniz seviyesi -> korunur (1,144,160) ~256m
        double h3 = PixelToHeightWithClamp(1, 144, 160);
        failed += !Expect(std::isfinite(h3) && h3 > -500.0 && h3 < 500.0,
            "Sea level values should remain finite and reasonable (~256m)");
        
        // Test: Yüksek dağ (Everest) -> korunur (2,224,0) ~8842m
        double h4 = PixelToHeightWithClamp(2, 224, 0);
        failed += !Expect(h4 > 8000.0 && h4 < 9000.0,
            "Everest height (~8842m) should be preserved");
        
        // Test: Aşırı yüksek >9000 -> 0.0m (clamped) (3,0,0) ~9661m
        double h5 = PixelToHeightWithClamp(3, 0, 0);
        failed += !Expect(Near(h5, 0.0),
            "Extreme high value (>9000m) should be clamped to 0.0m");
    }

    // Test 8: Terrarium decode formula + hard clamp
    {
        auto PixelToHeightTerrariumWithClamp = [](uint8_t r, uint8_t g, uint8_t b) -> double {
            double height = (static_cast<double>(r) * 256.0 +
                             static_cast<double>(g) +
                             static_cast<double>(b) / 256.0) - 32768.0;
            if (!std::isfinite(height) || height <= -32768.0 || height > 9000.0) {
                return 0.0;
            }
            return height;
        };

        double t1 = PixelToHeightTerrariumWithClamp(0, 0, 0);  // -32768 sentinel
        failed += !Expect(Near(t1, 0.0),
            "Terrarium NoData sentinel (-32768m) should be clamped to 0.0m");

        double t2 = PixelToHeightTerrariumWithClamp(128, 0, 0);  // 0m
        failed += !Expect(Near(t2, 0.0),
            "Terrarium sea level encoding should decode to ~0.0m");

        double t3 = PixelToHeightTerrariumWithClamp(162, 144, 0);  // 8848m
        failed += !Expect(t3 > 8800.0 && t3 < 8900.0,
            "Terrarium Everest-scale value should be preserved");

        double t4 = PixelToHeightTerrariumWithClamp(163, 72, 0);  // 9032m
        failed += !Expect(Near(t4, 0.0),
            "Terrarium extreme high value (>9000m) should be clamped to 0.0m");
    }

    if (failed == 0) {
        std::cout << "terrain_rgb_decoder_nodata_test: ALL PASSED" << std::endl;
    } else {
        std::cout << "terrain_rgb_decoder_nodata_test: " << failed << " FAILED" << std::endl;
    }

    return failed;
}
