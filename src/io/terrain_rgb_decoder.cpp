#include "terrain_rgb_decoder.h"
#include "../../third_party/stb_image.h"
#include <algorithm>
#include <cmath>

namespace globe {

static double PixelToHeight(uint8_t r, uint8_t g, uint8_t b) {
    // Mapbox Terrain-RGB: height = -10000 + (R*65536 + G*256 + B) * 0.1
    return -10000.0 + (static_cast<double>(r) * 65536.0 +
                        static_cast<double>(g) * 256.0 +
                        static_cast<double>(b)) * 0.1;
}

bool DecodeTerrainRGBFromImage(
    const std::vector<uint8_t>& imageData,
    int meshN,
    TerrainRGBEncoding encoding,
    DemGridData& outData,
    std::string* error)
{
    outData = DemGridData{};

    if (imageData.empty()) {
        if (error) *error = "empty image data";
        return false;
    }
    if (meshN < 2) {
        if (error) *error = "meshN must be >= 2";
        return false;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        imageData.data(), static_cast<int>(imageData.size()),
        &w, &h, &channels, 3);   // force 3-channel RGB

    if (!pixels) {
        if (error) *error = std::string("stbi decode failed: ") + stbi_failure_reason();
        return false;
    }

    if (w < 2 || h < 2) {
        stbi_image_free(pixels);
        if (error) *error = "decoded image too small";
        return false;
    }

    // Build full-resolution height grid from decoded image.
    // Row 0 of stbi output = top of image = north (high latitude).
    std::vector<double> fullGrid(static_cast<size_t>(w) * h);
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const int idx = (row * w + col) * 3;
            fullGrid[static_cast<size_t>(row) * w + col] =
                PixelToHeight(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
        }
    }
    stbi_image_free(pixels);

    // Bilinear resample to meshN × meshN.
    outData.meshN = meshN;
    outData.heights.resize(static_cast<size_t>(meshN) * meshN);
    outData.minHeight =  1e30;
    outData.maxHeight = -1e30;

    for (int gy = 0; gy < meshN; ++gy) {
        // gy=0 → south (bottom of image → row h-1), gy=meshN-1 → north (row 0)
        const double srcRow = (1.0 - static_cast<double>(gy) / (meshN - 1)) * (h - 1);
        const int r0 = std::clamp(static_cast<int>(std::floor(srcRow)), 0, h - 1);
        const int r1 = std::clamp(r0 + 1, 0, h - 1);
        const double fy = srcRow - r0;

        for (int gx = 0; gx < meshN; ++gx) {
            const double srcCol = static_cast<double>(gx) / (meshN - 1) * (w - 1);
            const int c0 = std::clamp(static_cast<int>(std::floor(srcCol)), 0, w - 1);
            const int c1 = std::clamp(c0 + 1, 0, w - 1);
            const double fx = srcCol - c0;

            const double v00 = fullGrid[static_cast<size_t>(r0) * w + c0];
            const double v10 = fullGrid[static_cast<size_t>(r0) * w + c1];
            const double v01 = fullGrid[static_cast<size_t>(r1) * w + c0];
            const double v11 = fullGrid[static_cast<size_t>(r1) * w + c1];

            const double height = v00 * (1 - fx) * (1 - fy) +
                                  v10 * fx * (1 - fy) +
                                  v01 * (1 - fx) * fy +
                                  v11 * fx * fy;

            outData.heights[static_cast<size_t>(gy) * meshN + gx] = height;
            outData.minHeight = std::min(outData.minHeight, height);
            outData.maxHeight = std::max(outData.maxHeight, height);
        }
    }

    outData.valid = true;
    return true;
}

} // namespace globe
