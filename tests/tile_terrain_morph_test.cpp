// Tile Terrain Morph Test
// Verifies smooth flat->terrain morph timing and reset behavior.

#include "../src/core/tile.h"
#include <cmath>
#include <iostream>

using namespace globe;

namespace {

bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

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

    const TileKey key(6, 12, 20);
    Tile tile(key);

    // No heightmap -> morph must be zero.
    failed += !Expect(Near(tile.UpdateTerrainMorph(1.0, false), 0.0f), "no heightmap should yield morph 0");

    // Heightmap appears -> morph starts at 0 and ramps to 1 in 200ms, with deterministic
    // per-tile stagger offset.
    uint32_t h = static_cast<uint32_t>(key.level * 73856093u) ^
                 static_cast<uint32_t>(key.x * 19349663u) ^
                 static_cast<uint32_t>(key.y * 83492791u);
    float stagger = static_cast<float>(h % 1000u) / 1000.0f * Tile::TERRAIN_MORPH_MAX_STAGGER;
    auto expectedMorph = [&](double t) -> float {
        float elapsed = static_cast<float>(t - (2.0 + static_cast<double>(stagger)));
        return std::clamp(elapsed / Tile::TERRAIN_MORPH_DURATION, 0.0f, 1.0f);
    };

    float m0 = tile.UpdateTerrainMorph(2.0, true);
    float mMid = tile.UpdateTerrainMorph(2.10, true);
    float mEnd = tile.UpdateTerrainMorph(2.20, true);
    float mDone = tile.UpdateTerrainMorph(2.31, true);
    float mAfter = tile.UpdateTerrainMorph(2.40, true);

    failed += !Expect(Near(m0, 0.0f), "morph should start at 0 when heightmap first appears");
    failed += !Expect(Near(mMid, expectedMorph(2.10), 1e-3f), "morph at 100ms should match staggered expectation");
    failed += !Expect(Near(mEnd, expectedMorph(2.20), 1e-3f), "morph at 200ms should match staggered expectation");
    failed += !Expect(Near(mDone, 1.0f, 1e-3f), "morph should complete by 200ms + max stagger");
    failed += !Expect(Near(mAfter, 1.0f, 1e-3f), "morph should remain at 1 while heightmap persists");

    // Heightmap loss resets state; next appearance restarts morph.
    failed += !Expect(Near(tile.UpdateTerrainMorph(3.0, false), 0.0f), "losing heightmap should reset morph to 0");
    float restart = tile.UpdateTerrainMorph(4.0, true);
    failed += !Expect(Near(restart, 0.0f), "heightmap re-appearance should restart morph from 0");

    // P2: Distance-based morph tests
    {
        const TileKey key2(7, 14, 25);
        Tile tile2(key2);
        
        // Test 1: Spawn at distance = morph 0
        float spawnDist = 1000.0f;  // 1000km away
        float rangeKm = 0.2f;    // 200m morph band
        float mSpawn = tile2.UpdateTerrainMorph(0.0, true, spawnDist, true, rangeKm, 
                                                 Tile::TERRAIN_MORPH_DURATION, true);
        failed += !Expect(Near(mSpawn, 0.0f), "P2: spawn at distance should yield morph 0");
        
        // Test 2: Halfway through adaptive morph band (~50km) -> 0.5
        float halfApproachDist = spawnDist - (spawnDist * 0.05f * 0.5f);
        float mHalf = tile2.UpdateTerrainMorph(0.0, true, halfApproachDist, true, rangeKm,
                                                 Tile::TERRAIN_MORPH_DURATION, true);
        failed += !Expect(Near(mHalf, 0.5f, 1e-3f), "P2: halfway approach should yield morph 0.5");
        
        // Test 3: Full approach to band end -> morph 1
        float fullApproachDist = spawnDist * 0.95f; // spawn - 5%
        float mFull = tile2.UpdateTerrainMorph(0.0, true, fullApproachDist, true, rangeKm,
                                                 Tile::TERRAIN_MORPH_DURATION, true);
        failed += !Expect(Near(mFull, 1.0f, 1e-3f), "P2: full approach should yield morph 1");
        
        // Test 4: Monotonic - moving away shouldn't decrease morph
        float mAway = tile2.UpdateTerrainMorph(0.0, true, spawnDist * 2.0f, true, rangeKm,
                                                 Tile::TERRAIN_MORPH_DURATION, true);
        failed += !Expect(Near(mAway, 1.0f, 1e-3f), "P2: moving away should keep morph at 1 (monotonic)");
        
        // Test 5: Invalid distance falls back to time-based (when useDistanceBased=false)
        Tile tile3(key2);
        float mTimeFallback = tile3.UpdateTerrainMorph(0.0, true, -1.0f, false, rangeKm,
                                                        Tile::TERRAIN_MORPH_DURATION, true);
        failed += !Expect(Near(mTimeFallback, 0.0f), "P2: negative distance with useDistanceBased=false should use time-based (start at 0)");
        
        // Test 6: Zero rangeKm falls back (treated as invalid)
        Tile tile4(key2);
        float mZeroRange = tile4.UpdateTerrainMorph(0.0, true, 1.0f, true, 0.0f,
                                                      Tile::TERRAIN_MORPH_DURATION, true);
        // With zero range, falls through to time-based path, starts at 0
        failed += !Expect(Near(mZeroRange, 0.0f), "P2: zero rangeKm should fallback to time-based");
        
        // Test 7: Invalid distance with NO fallback (enableTimeFallback=false)
        // Note: First reset terrain data to set morph to 0, then call with invalid params
        Tile tile5(key2);
        tile5.UpdateTerrainMorph(0.0, false);  // Reset to get morph=0
        float mNoFallback = tile5.UpdateTerrainMorph(0.0, true, -1.0f, true, rangeKm,
                                                       Tile::TERRAIN_MORPH_DURATION, false);
        // With invalid params and no fallback, morph stays at current value (0)
        failed += !Expect(Near(mNoFallback, 0.0f), "P2: invalid distance with no fallback should stay at current value");
    }

    if (failed == 0) {
        std::cout << "TileTerrainMorphTest PASSED\n";
        return 0;
    }

    std::cerr << "TileTerrainMorphTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
