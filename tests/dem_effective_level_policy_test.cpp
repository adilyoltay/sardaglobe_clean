// DEM effective-level policy test.
// Verifies best-available level and common-ancestor resolution behavior.

#include "../src/io/dem_manager.h"
#include "../src/debug/network_panel.h"

#include <algorithm>
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
    config.cacheSize = 64;
    config.baseUrl = "http://invalid.local/test";

    DemManager manager(config);

    TileKey parent(5, 14, 10);
    TileKey childA(7, parent.x * 4, parent.y * 4);
    TileKey childB(7, parent.x * 4 + 1, parent.y * 4);
    TileKey unrelated(7, 1, 1);

    manager.PutGridData(parent, MakeGrid(config.meshN, 120.0));
    manager.PutGridData(childA, MakeGrid(config.meshN, 140.0));

    int levelA = -1;
    int levelB = -1;
    int unrelatedLevel = -1;
    bool gotA = manager.GetBestAvailableLevel(childA, levelA);
    bool gotB = manager.GetBestAvailableLevel(childB, levelB);
    bool gotUnrelated = manager.GetBestAvailableLevel(unrelated, unrelatedLevel);

    failed += !Expect(gotA && levelA == childA.level, "exact child DEM should resolve to child level");
    failed += !Expect(gotB && levelB == parent.level, "missing child should resolve to parent ancestor level");
    failed += !Expect(!gotUnrelated, "unrelated tile should not resolve when cache chain is empty");

    const int effectiveLevel = std::min(levelA, levelB);
    failed += !Expect(effectiveLevel == parent.level,
                      "neighbor mismatch should resolve to common ancestor effective level");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemEffectiveLevelPolicyTest PASSED\n";
        return 0;
    }

    std::cerr << "DemEffectiveLevelPolicyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

