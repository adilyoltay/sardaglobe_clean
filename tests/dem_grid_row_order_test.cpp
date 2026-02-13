// DEM grid row-order regression test.
// The DEM service returns rows ordered south->north (bottom->top).
// This test ensures SampleHeightDetailed maps v accordingly (v=0 at latBottom).

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

DemGridData MakeRowCodedGrid(int meshN) {
    // Row y has value y*100 meters (row0=south, row(meshN-1)=north).
    DemGridData data;
    data.meshN = meshN;
    data.valid = true;
    data.minHeight = 0.0;
    data.maxHeight = (meshN - 1) * 100.0;
    data.heights.resize(static_cast<size_t>(meshN * meshN), 0.0);
    for (int y = 0; y < meshN; ++y) {
        for (int x = 0; x < meshN; ++x) {
            data.heights[static_cast<size_t>(y * meshN + x)] = static_cast<double>(y) * 100.0;
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
    config.baseUrl = "http://invalid.local/test";

    DemManager manager(config);

    const TileKey key(2, 1, 1);
    manager.PutGridData(key, MakeRowCodedGrid(config.meshN));

    const Extent ext = Extent::FromTileWGS84(key.x, key.y, key.level);
    const double lonMid = 0.5 * (ext.West() + ext.East());

    auto sampleAtFrac = [&](double fracFromSouth, double& outMeters) -> bool {
        // Linear in geodetic lat matches SampleHeightDetailed's v-mapping.
        const double lat = ext.South() + fracFromSouth * (ext.North() - ext.South());
        DemSampleResult s;
        if (!manager.SampleHeightDetailed(lonMid, lat, key.level, s) || !s.ok) {
            return false;
        }
        outMeters = s.heightMeters;
        return true;
    };

    double h25 = 0.0;
    double h75 = 0.0;
    bool ok25 = sampleAtFrac(0.25, h25);
    bool ok75 = sampleAtFrac(0.75, h75);
    failed += !Expect(ok25 && ok75, "samples should resolve inside tile");

    // With correct south->north mapping, 0.25 should be ~row1 (=100m) and 0.75 ~row3 (=300m).
    failed += !Expect(std::fabs(h25 - 100.0) < 1e-3, "south-side sample should map to lower rows");
    failed += !Expect(std::fabs(h75 - 300.0) < 1e-3, "north-side sample should map to higher rows");
    failed += !Expect(h25 < h75, "heights should increase with latitude for row-coded grid");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemGridRowOrderTest PASSED\n";
        return 0;
    }

    std::cerr << "DemGridRowOrderTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

