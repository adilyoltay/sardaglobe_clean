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

    Tile tile(TileKey(6, 12, 20));

    // No heightmap -> morph must be zero.
    failed += !Expect(Near(tile.UpdateTerrainMorph(1.0, false), 0.0f), "no heightmap should yield morph 0");

    // Heightmap appears -> morph starts at 0 and ramps to 1 in 200ms by default.
    float m0 = tile.UpdateTerrainMorph(2.0, true);
    float mMid = tile.UpdateTerrainMorph(2.10, true);
    float mEnd = tile.UpdateTerrainMorph(2.20, true);
    float mAfter = tile.UpdateTerrainMorph(2.40, true);

    failed += !Expect(Near(m0, 0.0f), "morph should start at 0 when heightmap first appears");
    failed += !Expect(mMid > 0.45f && mMid < 0.55f, "morph midpoint should be around 0.5 at 100ms");
    failed += !Expect(Near(mEnd, 1.0f, 1e-3f), "morph should complete at 200ms");
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
