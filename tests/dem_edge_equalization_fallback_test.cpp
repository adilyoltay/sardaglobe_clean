// DEM edge equalization regression test.
// Ensures intentional border coarsening does not trigger full-tile parent fallback.

#include "../src/rendering/tile_mesh_builder.h"
#include "../src/core/extent.h"
#include "../src/io/dem_manager.h"
#include "../src/debug/network_panel.h"

#include <iostream>

using namespace globe;

namespace globe {

// Minimal stubs for linking dem_manager.cpp without UI dependencies.
NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel panel;
    return panel;
}
void NetworkPanel::RecordStart(const TileKey&, RequestType, const std::string&) {}
void NetworkPanel::RecordComplete(const TileKey&, RequestType, bool, long, size_t, double, bool, const std::string&) {}
void NetworkPanel::Render() {}
void NetworkPanel::Clear() {}
NetworkPanel::Stats NetworkPanel::GetStats() const { return Stats{}; }
NetworkRequest* NetworkPanel::FindPending(const TileKey&, RequestType) { return nullptr; }

} // namespace globe

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

DemGridData MakeConstantGrid(int meshN, double valueMeters) {
    DemGridData data;
    data.meshN = meshN;
    data.valid = true;
    data.minHeight = valueMeters;
    data.maxHeight = valueMeters;
    data.heights.assign(static_cast<size_t>(meshN * meshN), valueMeters);
    return data;
}

DemGridData MakeEdgeMatchedGrid(int meshN, double edgeValueMeters, double innerValueMeters) {
    DemGridData data;
    data.meshN = meshN;
    data.valid = true;
    data.minHeight = std::min(edgeValueMeters, innerValueMeters);
    data.maxHeight = std::max(edgeValueMeters, innerValueMeters);
    data.heights.resize(static_cast<size_t>(meshN * meshN), edgeValueMeters);
    for (int y = 0; y < meshN; ++y) {
        for (int x = 0; x < meshN; ++x) {
            const bool edge = (x == 0 || y == 0 || x == meshN - 1 || y == meshN - 1);
            data.heights[static_cast<size_t>(y * meshN + x)] = edge ? edgeValueMeters : innerValueMeters;
        }
    }
    return data;
}

} // namespace

int main() {
    int failed = 0;

    Config config;
    config.meshSegments = 8;
    config.demMeshN = 5;
    config.terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;
    config.demHeightScale = 1.0;

    DemManager::Config demConfig;
    demConfig.meshN = config.demMeshN;
    demConfig.cacheSize = 32;
    demConfig.maxBatchSize = 1;
    demConfig.baseUrl = "http://invalid.local/test";

    DemManager demManager(demConfig);

    const TileKey child(3, 2, 2);
    const Extent extent = Extent::FromTileWGS84(child.x, child.y, child.level);

    // Populate all level-2 parents so border sampling at exact tile boundaries
    // never falls through to deeper ancestors due coordinate quantization.
    for (int py = 0; py < (1 << 2); ++py) {
        for (int px = 0; px < (1 << 2); ++px) {
            demManager.PutGridData(TileKey(2, px, py), MakeConstantGrid(demConfig.meshN, 100.0));
        }
    }
    demManager.PutGridData(child, MakeEdgeMatchedGrid(demConfig.meshN, 100.0, 220.0));

    auto result = TileMeshBuilder::Build(
        child,
        extent,
        Tile::EDGE_EAST,  // Intentional border equalization against coarser edge.
        0,
        Tile::EDGE_EAST,
        child.level,
        &demManager,
        config,
        false
    );

    failed += !Expect(result.demUsed, "DEM should be used for child tile mesh build");
    failed += !Expect(result.demMissingSamples == 0, "No DEM samples should be missing");
    failed += !Expect(result.demSourceLevelMin == 2,
                      "Border equalization should sample parent level on the requested edge");
    failed += !Expect(result.demSourceLevelMax == child.level,
                      "Interior should remain on exact child DEM level");

    double heightRangeKm = result.maxHeightKm - result.minHeightKm;
    failed += !Expect(heightRangeKm > 0.05,
                      "Intentional mixed edge levels must not collapse tile to uniform parent DEM");

    demManager.Shutdown();

    if (failed == 0) {
        std::cout << "DemEdgeEqualizationFallbackTest PASSED\n";
        return 0;
    }

    std::cerr << "DemEdgeEqualizationFallbackTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
