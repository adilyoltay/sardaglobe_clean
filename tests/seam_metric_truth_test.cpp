// Seam metric truth test.
// Injects a deliberate edge height step and verifies it is detectable.

#include "../src/io/dem_manager.h"
#include "../src/core/extent.h"
#include "../src/debug/network_panel.h"

#include <cmath>
#include <iostream>

using namespace globe;

namespace globe {

// Minimal stubs for linking dem_manager.cpp without UI backends.
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

DemGridData MakeGrid(int meshN, double valueMeters) {
    DemGridData data;
    data.meshN = meshN;
    data.valid = true;
    data.minHeight = valueMeters;
    data.maxHeight = valueMeters;
    data.heights.assign(static_cast<size_t>(meshN * meshN), valueMeters);
    return data;
}

} // namespace

int main() {
    int failed = 0;

    DemManager::Config config;
    config.meshN = 5;
    config.cacheSize = 32;
    config.baseUrl = "http://invalid.local/test";
    config.maxBatchSize = 1;

    DemManager manager(config);

    // Two adjacent tiles at same level with intentionally different edge heights.
    const TileKey left(6, 20, 24);
    const TileKey right(6, 21, 24);
    manager.PutGridData(left, MakeGrid(config.meshN, 10.0));
    manager.PutGridData(right, MakeGrid(config.meshN, 80.0));

    Extent leftExtent = Extent::FromTileWGS84(left.x, left.y, left.level);
    const double lonBoundary = leftExtent.East();
    const double latMid = 0.5 * (leftExtent.North() + leftExtent.South());
    const double eps = 1e-9;

    DemSampleResult a;
    DemSampleResult b;
    bool okA = manager.SampleHeightDetailed(lonBoundary - eps, latMid, left.level, a);
    bool okB = manager.SampleHeightDetailed(lonBoundary + eps, latMid, right.level, b);
    failed += !Expect(okA && okB && a.ok && b.ok, "both edge samples should resolve");

    const double deltaM = std::fabs(a.heightMeters - b.heightMeters);
    failed += !Expect(deltaM > 4.0, "deliberate seam step should exceed seam warning threshold");
    failed += !Expect(deltaM > 15.0, "deliberate seam step should exceed cliff threshold");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "SeamMetricTruthTest PASSED\n";
        return 0;
    }

    std::cerr << "SeamMetricTruthTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

