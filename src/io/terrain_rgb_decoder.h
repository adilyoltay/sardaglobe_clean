#pragma once

#include "dem_manager.h"
#include <cstdint>
#include <string>
#include <vector>

namespace globe {

struct Config;  // Forward declaration

// Terrain tile encoding formats.
enum class TerrainRGBEncoding {
    Mapbox,    // height = -10000 + (R*256*256 + G*256 + B) * 0.1
    Terrarium  // height = (R*256 + G + B/256) - 32768
};

// Decode a PNG/JPEG Terrain-RGB tile into a DemGridData grid.
// The image is decoded, pixel RGB values are converted to meters using the
// selected encoding, and the result is bilinearly resampled to meshN×meshN.
// When config is provided and forceClampTerrainNoData is true, heights below
// demNoDataMinHeightM are replaced with demNoDataReplacementM.
// Returns true on success.  On failure, *error (if non-null) receives a
// human-readable message.
bool DecodeTerrainRGBFromImage(
    const std::vector<uint8_t>& imageData,
    int meshN,
    TerrainRGBEncoding encoding,
    DemGridData& outData,
    std::string* error = nullptr,
    const Config* config = nullptr);

} // namespace globe
