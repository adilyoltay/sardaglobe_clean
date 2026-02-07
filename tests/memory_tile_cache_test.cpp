// Memory Tile Cache Test
// Verifies LRU behavior, byte budget eviction, and telemetry counters.

#include "../src/io/memory_tile_cache.h"
#include <iostream>
#include <vector>

using namespace globe;

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

    MemoryTileCache cache(2, 20); // max 2 entries, 20 bytes
    const std::string source = "https://tiles.example/{z}/{x}/{y}.png";

    TileKey a(4, 1, 1);
    TileKey b(4, 1, 2);
    TileKey c(4, 1, 3);

    std::vector<uint8_t> out;
    failed += !Expect(!cache.Read(a, source, out), "initial read should miss");

    std::vector<uint8_t> vA(8, 1);
    std::vector<uint8_t> vB(8, 2);
    std::vector<uint8_t> vC(8, 3);
    std::vector<uint8_t> vHuge(64, 9);

    failed += !Expect(cache.Write(a, source, vA), "write A should succeed");
    failed += !Expect(cache.Write(b, source, vB), "write B should succeed");

    out.clear();
    failed += !Expect(cache.Read(a, source, out), "read A should hit");
    failed += !Expect(out.size() == vA.size() && out[0] == 1, "read A payload should match");

    // A was touched, B is now LRU. Writing C should evict B.
    failed += !Expect(cache.Write(c, source, vC), "write C should succeed");

    out.clear();
    failed += !Expect(!cache.Read(b, source, out), "B should be evicted by LRU");
    failed += !Expect(cache.Read(a, source, out), "A should remain after LRU eviction");
    failed += !Expect(cache.Read(c, source, out), "C should be present");

    failed += !Expect(!cache.Write(c, source, vHuge), "oversized write should be rejected");

    auto stats = cache.GetStats();
    failed += !Expect(stats.readHits >= 3, "read hit counter should include A/A/C hits");
    failed += !Expect(stats.readMisses >= 2, "read miss counter should include initial and B miss");
    failed += !Expect(stats.writeSuccess == 3, "write success should count A/B/C");
    failed += !Expect(stats.writeReject == 1, "write reject should count oversized write");
    failed += !Expect(stats.evictions >= 1, "eviction counter should increment");
    failed += !Expect(stats.entryCount == 2, "cache should contain 2 entries after eviction");
    failed += !Expect(stats.bytesUsed <= 20, "cache bytes should respect max byte budget");

    if (failed == 0) {
        std::cout << "MemoryTileCacheTest PASSED\n";
        return 0;
    }

    std::cerr << "MemoryTileCacheTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
