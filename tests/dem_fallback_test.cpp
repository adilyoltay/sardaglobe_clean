// DEM Parent Fallback Test (P5.3)
// Verifies that SampleHeight falls back to ancestor DEM and prefers exact child DEM when available.

#include "../src/io/dem_manager.h"

#include "../src/core/extent.h"
#include "../src/debug/network_panel.h"
#include <cmath>
#include <iostream>

using namespace globe;

namespace globe {

// Minimal stubs so this unit test can link dem_manager.cpp without ImGui/GLFW dependencies.
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

bool Near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
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

    DemManager::Config config;
    config.meshN = 5;
    config.cacheSize = 32;
    config.maxBatchSize = 1;
    config.baseUrl = "http://invalid.local/test";

    DemManager manager(config);

    const TileKey parent(2, 1, 1);
    const TileKey child(3, 2, 2);  // Parent of this child is (2,1,1)
    const Extent childExtent = Extent::FromTileWGS84(child.x, child.y, child.level);
    const LonLat childCenter = childExtent.GetCenter();

    manager.PutGridData(parent, MakeConstantGrid(config.meshN, 100.0));

    double sampledHeight = 0.0;
    bool fallbackOk = manager.SampleHeight(childCenter.lon, childCenter.lat, child.level, sampledHeight);
    failed += !Expect(fallbackOk, "SampleHeight should succeed via parent DEM fallback");
    failed += !Expect(Near(sampledHeight, 100.0), "fallback sample should use parent DEM value");
    failed += !Expect(!manager.HasData(child), "child DEM should still be missing");
    failed += !Expect(manager.HasDataOrAncestor(child), "HasDataOrAncestor should detect parent fallback availability");

    manager.PutGridData(child, MakeConstantGrid(config.meshN, 220.0));

    double exactHeight = 0.0;
    bool exactOk = manager.SampleHeight(childCenter.lon, childCenter.lat, child.level, exactHeight);
    failed += !Expect(exactOk, "SampleHeight should succeed for exact child DEM");
    failed += !Expect(Near(exactHeight, 220.0), "exact child DEM should be preferred over parent fallback");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemFallbackTest PASSED" << std::endl;
        return 0;
    }

    std::cerr << "DemFallbackTest FAILED (" << failed << " checks failed)" << std::endl;
    return 1;
}
