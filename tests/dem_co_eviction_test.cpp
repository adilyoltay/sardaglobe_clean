#include "../src/io/dem_manager.h"
#include "../src/debug/network_panel.h"

#include <iostream>

using namespace globe;

namespace {

bool Expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        return false;
    }
    return true;
}

DemGridData MakeGrid(int meshN, double value) {
    DemGridData data;
    data.meshN = meshN;
    data.minHeight = value;
    data.maxHeight = value;
    data.heights.assign(static_cast<size_t>(meshN * meshN), value);
    data.valid = true;
    return data;
}

} // namespace

namespace globe {

NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel panel;
    return panel;
}

void NetworkPanel::RecordStart(const TileKey&, RequestType, const std::string&) {}

void NetworkPanel::RecordComplete(const TileKey&, RequestType, bool, long, size_t,
                                  double, bool, const std::string&) {}

} // namespace globe

int main() {
    int failed = 0;

    DemManager::Config config;
    config.baseUrl = "http://invalid.local/dem";
    config.meshN = 5;
    config.cacheSize = 16;

    DemManager manager(config);

    const TileKey key(4, 7, 9);
    manager.PutGridData(key, MakeGrid(config.meshN, 120.0));
    failed += !Expect(manager.HasData(key), "put grid should create DEM cache entry");

    manager.SetPinnedTiles({key});
    manager.UnpinAndEvict(key);
    failed += !Expect(!manager.HasData(key), "co-eviction should remove DEM cache entry");
    failed += !Expect(!manager.HasDataOrAncestor(key), "co-eviction should remove ancestor availability");

    manager.PutGridData(key, MakeGrid(config.meshN, 130.0));
    failed += !Expect(manager.HasData(key), "explicit put should clear co-eviction block");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemCoEvictionTest PASSED\n";
        return 0;
    }

    std::cerr << "DemCoEvictionTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
