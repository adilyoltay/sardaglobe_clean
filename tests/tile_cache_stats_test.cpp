// Tile Cache Stats Test
// Verifies disk cache hit/miss/write telemetry counters.

#include "../src/io/tile_cache.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

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

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() /
                                    ("tile_cache_stats_test_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    TileCache cache(tempDir.string());
    TileKey key(4, 10, 7);
    const std::string urlTemplate = "https://example.com/{z}/{x}/{y}.png";
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6};

    std::vector<uint8_t> out;
    failed += !Expect(!cache.Read(key, urlTemplate, out), "first read should be a miss");

    auto stats = cache.GetStats();
    failed += !Expect(stats.readMisses == 1, "read miss counter should increment");
    failed += !Expect(stats.readHits == 0, "read hit counter should stay zero before write");

    failed += !Expect(cache.Write(key, urlTemplate, payload), "write should succeed");
    stats = cache.GetStats();
    failed += !Expect(stats.writeSuccess == 1, "write success counter should increment");
    failed += !Expect(stats.bytesWritten == payload.size(), "bytes written should match payload size");

    out.clear();
    failed += !Expect(cache.Read(key, urlTemplate, out), "second read should hit cache");
    failed += !Expect(out == payload, "cached payload must round-trip");

    stats = cache.GetStats();
    failed += !Expect(stats.readHits == 1, "read hit counter should increment");
    failed += !Expect(stats.bytesRead == payload.size(), "bytes read should match payload size");

    cache.ResetStats();
    stats = cache.GetStats();
    failed += !Expect(stats.readHits == 0 && stats.readMisses == 0, "read counters should reset");
    failed += !Expect(stats.writeSuccess == 0 && stats.writeFail == 0, "write counters should reset");
    failed += !Expect(stats.bytesRead == 0 && stats.bytesWritten == 0, "byte counters should reset");

    std::filesystem::remove_all(tempDir, ec);

    if (failed == 0) {
        std::cout << "TileCacheStatsTest PASSED\n";
        return 0;
    }

    std::cerr << "TileCacheStatsTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
