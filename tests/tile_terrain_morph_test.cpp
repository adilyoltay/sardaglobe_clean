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

    if (failed == 0) {
        std::cout << "TileTerrainMorphTest PASSED\n";
        return 0;
    }

    std::cerr << "TileTerrainMorphTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
