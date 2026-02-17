#include "../src/rendering/tile_mesh_builder.h"
#include "../src/io/dem_manager.h"
#include "../src/core/config.h"
#include "../src/core/extent.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace globe;

namespace {

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

DemGridData MakeInvalidGrid() {
    DemGridData grid;
    grid.valid = true;
    grid.meshN = 2;
    grid.heights = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        1.0e9
    };
    grid.minHeight = -1.0e9;
    grid.maxHeight = 1.0e9;
    return grid;
}

} // namespace

int main() {
    int failures = 0;

    DemManager::Config demCfg;
    demCfg.providerType = DemProviderType::TerrainRGB;
    demCfg.maxZoom = 15;
    demCfg.meshN = 2;
    demCfg.debug = false;

    DemManager demManager(demCfg);

    Config cfg;
    cfg.demProviderEffectiveMaxZoom = 15;
    cfg.demMeshN = 2;
    cfg.meshSegments = 8;
    cfg.terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;
    cfg.demHeightScaleBase = 1.0;
    cfg.demExaggerationFactor = 1.0;
    cfg.demHeightMinKm = -0.012f;
    cfg.demHeightMaxKm = 0.012f;
    cfg.demDebug = true;
    cfg.demDebugAbortOnInvalidVertex = false;

    TileKey key(4, 8, 6);
    demManager.PutGridData(key, MakeInvalidGrid());

    Extent extent = Extent::FromTileWGS84(key.x, key.y, key.level);
    auto result = TileMeshBuilder::Build(
        key,
        extent,
        0,
        0,
        key.level,
        0,
        &demManager,
        cfg,
        true);

    if (!Expect(!result.vertices.empty(), "Mesh builder should emit vertices")) {
        failures++;
    }

    constexpr std::size_t kStride = 9;
    for (std::size_t i = 0; i + kStride <= result.vertices.size(); i += kStride) {
        const float px = result.vertices[i + 0];
        const float py = result.vertices[i + 1];
        const float pz = result.vertices[i + 2];
        const float nx = result.vertices[i + 3];
        const float ny = result.vertices[i + 4];
        const float nz = result.vertices[i + 5];
        const float h = result.vertices[i + 8];

        if (!Expect(std::isfinite(px) && std::isfinite(py) && std::isfinite(pz), "Vertex position must be finite")) {
            failures++;
            break;
        }
        if (!Expect(std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz), "Vertex normal must be finite")) {
            failures++;
            break;
        }
        if (!Expect(std::isfinite(h), "Vertex height must be finite")) {
            failures++;
            break;
        }
        if (!Expect(h >= cfg.demHeightMinKm && h <= cfg.demHeightMaxKm,
                    "Vertex height must be clamped to demHeightMinKm/demHeightMaxKm")) {
            failures++;
            break;
        }
    }

    if (failures == 0) {
        Report("MeshBuilderHeightSanitize");
        std::cerr << "\nAll mesh builder sanitize tests PASSED\n";
        return 0;
    }

    std::cerr << "\n" << failures << " test(s) FAILED\n";
    return 1;
}
