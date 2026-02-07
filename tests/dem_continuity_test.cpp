// DEM continuity regression test.
// Verifies ancestor fallback and shared-edge continuity between exact/fallback neighbors.

#include "../src/io/dem_manager.h"
#include "../src/core/extent.h"
#include "../src/debug/network_panel.h"

#include <cmath>
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

    DemManager::Config config;
    config.meshN = 5;
    config.cacheSize = 32;
    config.maxBatchSize = 1;
    config.baseUrl = "http://invalid.local/test";

    DemManager manager(config);

    const TileKey parent(2, 1, 1);
    const TileKey childWest(3, 2, 2);
    const TileKey childEast(3, 3, 2); // east sibling (left missing, will fallback to parent)

    manager.PutGridData(parent, MakeConstantGrid(config.meshN, 100.0));
    manager.PutGridData(childWest, MakeEdgeMatchedGrid(config.meshN, 100.0, 150.0));

    const Extent westExtent = Extent::FromTileWGS84(childWest.x, childWest.y, childWest.level);
    const double lonBoundary = westExtent.East();
    const double latMid = 0.5 * (westExtent.North() + westExtent.South());
    const double eps = 1e-9;

    DemSampleResult westSample;
    DemSampleResult eastSample;
    bool westOk = manager.SampleHeightDetailed(lonBoundary - eps, latMid, childWest.level, westSample);
    bool eastOk = manager.SampleHeightDetailed(lonBoundary + eps, latMid, childEast.level, eastSample);
    failed += !Expect(westOk && westSample.ok, "west child sample should succeed");
    failed += !Expect(eastOk && eastSample.ok, "east sibling fallback sample should succeed");
    failed += !Expect(westSample.sourceLevel == childWest.level, "west sample should use exact child DEM level");
    failed += !Expect(eastSample.sourceLevel == parent.level && eastSample.usedAncestor,
                      "east sample should use parent fallback level");

    const double edgeDeltaM = std::fabs(westSample.heightMeters - eastSample.heightMeters);
    failed += !Expect(edgeDeltaM <= 5.0, "shared edge delta should stay below continuity threshold");

    DemSampleResult eastCenter;
    const Extent eastExtent = Extent::FromTileWGS84(childEast.x, childEast.y, childEast.level);
    LonLat eastCenterLonLat = eastExtent.GetCenter();
    bool eastCenterOk = manager.SampleHeightDetailed(
        eastCenterLonLat.lon, eastCenterLonLat.lat, childEast.level, eastCenter);
    failed += !Expect(eastCenterOk && eastCenter.ok, "east center fallback should succeed");
    failed += !Expect(eastCenter.sourceLevel == parent.level, "east center should use parent source level");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemContinuityTest PASSED\n";
        return 0;
    }

    std::cerr << "DemContinuityTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

