// DEM batch URL order regression test.
// Ensures BuildBatchUrl emits C1..CN in input order, matching the service response ordering.

// Access private helpers for deterministic URL checks.
#define private public
#include "../src/io/dem_manager.h"
#undef private

#include "../src/debug/network_panel.h"

#include <iostream>
#include <string>

using namespace globe;

namespace globe {

// Minimal stubs so this unit test can link dem_manager.cpp without UI dependencies.
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

} // namespace

int main() {
    int failed = 0;

    DemManager::Config config;
    config.meshN = 5;
    config.cacheSize = 8;
    config.maxBatchSize = 2;
    config.baseUrl = "https://example.invalid/dem";

    DemManager manager(config);

    DemCell a;
    a.llx = 1.1;
    a.lly = 2.2;
    a.urx = 3.3;
    a.ury = 4.4;

    DemCell b;
    b.llx = 9.9;
    b.lly = 8.8;
    b.urx = 7.7;
    b.ury = 6.6;

    const std::string url = manager.BuildBatchUrl({a, b});

    failed += !Expect(url.find("&CN=2") != std::string::npos, "CN should be 2");
    failed += !Expect(url.find("C1LLX=1.100000000000") != std::string::npos, "C1 should map to first cell");
    failed += !Expect(url.find("C2LLX=9.900000000000") != std::string::npos, "C2 should map to second cell");

    // Ordering: C1 parameters must appear before C2 parameters.
    const std::size_t c1Pos = url.find("&C1LLX=");
    const std::size_t c2Pos = url.find("&C2LLX=");
    failed += !Expect(c1Pos != std::string::npos && c2Pos != std::string::npos && c1Pos < c2Pos,
                      "URL should emit C1 before C2");

    manager.Shutdown();

    if (failed == 0) {
        std::cout << "DemBatchUrlOrderTest PASSED\n";
        return 0;
    }

    std::cerr << "DemBatchUrlOrderTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

