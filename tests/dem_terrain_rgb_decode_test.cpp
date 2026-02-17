#include "../src/io/terrain_rgb_decoder.h"
#include "../src/io/dem_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool NearlyEqual(double a, double b, double eps = 0.15) {
    return std::fabs(a - b) <= eps;
}

void EncodeMapbox(double meters, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int packed = std::max(0, static_cast<int>(std::llround((meters + 10000.0) * 10.0)));
    r = static_cast<uint8_t>((packed >> 16) & 0xff);
    g = static_cast<uint8_t>((packed >> 8) & 0xff);
    b = static_cast<uint8_t>(packed & 0xff);
}

void EncodeTerrarium(double meters, uint8_t& r, uint8_t& g, uint8_t& b) {
    const double shifted = meters + 32768.0;
    const int ri = std::clamp(static_cast<int>(std::floor(shifted / 256.0)), 0, 255);
    const double rem = shifted - static_cast<double>(ri) * 256.0;
    const int gi = std::clamp(static_cast<int>(std::floor(rem)), 0, 255);
    int bi = static_cast<int>(std::llround((rem - static_cast<double>(gi)) * 256.0));
    if (bi > 255) bi = 255;
    if (bi < 0) bi = 0;

    r = static_cast<uint8_t>(ri);
    g = static_cast<uint8_t>(gi);
    b = static_cast<uint8_t>(bi);
}

void SetPixel(std::vector<uint8_t>& rgba,
              int width,
              int x,
              int y,
              double meters,
              TerrainRGBEncoding encoding) {
    uint8_t r = 0, g = 0, b = 0;
    if (encoding == TerrainRGBEncoding::Terrarium) {
        EncodeTerrarium(meters, r, g, b);
    } else {
        EncodeMapbox(meters, r, g, b);
    }
    const size_t idx = static_cast<size_t>((y * width + x) * 4);
    rgba[idx + 0] = r;
    rgba[idx + 1] = g;
    rgba[idx + 2] = b;
    rgba[idx + 3] = 255;
}

bool TestMapboxRowOrder() {
    // Image rows are north->south. Output DEM rows must be south->north.
    std::vector<uint8_t> rgba(2 * 2 * 4, 255);
    SetPixel(rgba, 2, 0, 0, 1000.0, TerrainRGBEncoding::Mapbox); // NW
    SetPixel(rgba, 2, 1, 0, 2000.0, TerrainRGBEncoding::Mapbox); // NE
    SetPixel(rgba, 2, 0, 1, 3000.0, TerrainRGBEncoding::Mapbox); // SW
    SetPixel(rgba, 2, 1, 1, 4000.0, TerrainRGBEncoding::Mapbox); // SE

    DemGridData out;
    std::string err;
    const bool ok = DecodeTerrainRGBPixels(
        rgba.data(), 2, 2, 2, TerrainRGBEncoding::Mapbox, out, &err);

    bool pass = true;
    pass &= Expect(ok, "Mapbox decode should succeed");
    pass &= Expect(out.valid, "Mapbox output should be valid");
    pass &= Expect(out.heights.size() == 4, "Mapbox output mesh should be 2x2");
    if (out.heights.size() == 4) {
        pass &= Expect(NearlyEqual(out.heights[0], 3000.0), "row0 col0 must be south-west");
        pass &= Expect(NearlyEqual(out.heights[1], 4000.0), "row0 col1 must be south-east");
        pass &= Expect(NearlyEqual(out.heights[2], 1000.0), "row1 col0 must be north-west");
        pass &= Expect(NearlyEqual(out.heights[3], 2000.0), "row1 col1 must be north-east");
    }
    return pass;
}

bool TestTerrariumDecode() {
    std::vector<uint8_t> rgba(2 * 2 * 4, 255);
    SetPixel(rgba, 2, 0, 0, -50.5, TerrainRGBEncoding::Terrarium);
    SetPixel(rgba, 2, 1, 0, 125.25, TerrainRGBEncoding::Terrarium);
    SetPixel(rgba, 2, 0, 1, 300.75, TerrainRGBEncoding::Terrarium);
    SetPixel(rgba, 2, 1, 1, 512.0, TerrainRGBEncoding::Terrarium);

    DemGridData out;
    std::string err;
    const bool ok = DecodeTerrainRGBPixels(
        rgba.data(), 2, 2, 2, TerrainRGBEncoding::Terrarium, out, &err);

    bool pass = true;
    pass &= Expect(ok, "Terrarium decode should succeed");
    pass &= Expect(out.valid, "Terrarium output should be valid");
    if (out.heights.size() == 4) {
        pass &= Expect(NearlyEqual(out.heights[0], 300.75), "terrarium SW sample mismatch");
        pass &= Expect(NearlyEqual(out.heights[1], 512.0), "terrarium SE sample mismatch");
        pass &= Expect(NearlyEqual(out.heights[2], -50.5), "terrarium NW sample mismatch");
        pass &= Expect(NearlyEqual(out.heights[3], 125.25), "terrarium NE sample mismatch");
    }
    return pass;
}

bool TestBilinearCenter() {
    std::vector<uint8_t> rgba(2 * 2 * 4, 255);
    SetPixel(rgba, 2, 0, 0, 0.0, TerrainRGBEncoding::Mapbox);    // NW
    SetPixel(rgba, 2, 1, 0, 100.0, TerrainRGBEncoding::Mapbox);  // NE
    SetPixel(rgba, 2, 0, 1, 200.0, TerrainRGBEncoding::Mapbox);  // SW
    SetPixel(rgba, 2, 1, 1, 300.0, TerrainRGBEncoding::Mapbox);  // SE

    DemGridData out;
    std::string err;
    const bool ok = DecodeTerrainRGBPixels(
        rgba.data(), 2, 2, 3, TerrainRGBEncoding::Mapbox, out, &err);

    bool pass = true;
    pass &= Expect(ok, "Bilinear decode should succeed");
    pass &= Expect(out.valid, "Bilinear output should be valid");
    pass &= Expect(out.heights.size() == 9, "3x3 mesh should have 9 samples");
    if (out.heights.size() == 9) {
        // center sample (row=1, col=1) should average all corners.
        pass &= Expect(NearlyEqual(out.heights[4], 150.0), "Bilinear center mismatch");
    }
    return pass;
}

} // namespace

int main() {
    int failed = 0;
    failed += !TestMapboxRowOrder();
    failed += !TestTerrariumDecode();
    failed += !TestBilinearCenter();

    if (failed == 0) {
        std::cout << "DemTerrainRgbDecodeTest PASSED\n";
        return 0;
    }

    std::cerr << "DemTerrainRgbDecodeTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
