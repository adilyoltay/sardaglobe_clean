// Tile Fade Test
// Verifies monotonic fade progression with dynamic duration updates.

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
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    // Case 1: Default 300ms fade reaches 1.0 and completes.
    {
        Tile tile(TileKey(4, 3, 2));
        float a0 = tile.UpdateFade(10.0);
        float a1 = tile.UpdateFade(10.15);
        float a2 = tile.UpdateFade(10.30);

        failed += !Expect(Near(a0, 0.0f, 1e-3f), "default fade should start at alpha 0");
        failed += !Expect(a1 > 0.45f && a1 < 0.55f, "default fade midpoint should be around 0.5");
        failed += !Expect(Near(a2, 1.0f, 1e-3f), "default fade should complete at 300ms");
        failed += !Expect(tile.fadeComplete, "fadeComplete should be true when alpha reaches 1");
    }

    // Case 2: Changing duration frame-to-frame must not decrease alpha.
    {
        Tile tile(TileKey(5, 10, 12));
        float prev = tile.UpdateFade(20.0, 0.30f);
        float a1 = tile.UpdateFade(20.03, 0.08f);  // aggressive shorten
        float a2 = tile.UpdateFade(20.04, 0.30f);  // expand duration again
        float a3 = tile.UpdateFade(20.20, 0.30f);

        failed += !Expect(a1 >= prev, "alpha must be monotonic when fade duration shortens");
        failed += !Expect(a2 >= a1, "alpha must remain monotonic when fade duration increases back");
        failed += !Expect(a3 >= a2, "alpha must continue increasing over time");
    }

    // Case 3: Zero/negative duration should clamp and complete quickly.
    {
        Tile tile(TileKey(6, 20, 18));
        float a0 = tile.UpdateFade(30.0, 0.0f);
        float a1 = tile.UpdateFade(30.02, -1.0f);

        failed += !Expect(Near(a0, 0.0f, 1e-3f), "clamped duration fade should still start at alpha 0");
        failed += !Expect(Near(a1, 1.0f, 1e-3f), "clamped duration should complete after >=10ms");
    }

    if (failed == 0) {
        std::cout << "TileFadeTest PASSED" << std::endl;
        return 0;
    }

    std::cerr << "TileFadeTest FAILED (" << failed << " checks failed)" << std::endl;
    return 1;
}
