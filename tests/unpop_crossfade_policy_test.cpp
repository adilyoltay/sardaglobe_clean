#include "../src/rendering/unpop_crossfade.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace globe;

namespace {

bool Near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool Expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        return false;
    }
    return true;
}

bool ExpectVec4(const glm::vec4& value, const glm::vec4& expected, const char* msg) {
    if (!Near(value.x, expected.x) ||
        !Near(value.y, expected.y) ||
        !Near(value.z, expected.z) ||
        !Near(value.w, expected.w)) {
        std::cerr << "FAILED: " << msg
                  << " got [" << value.x << ", " << value.y << ", " << value.z << ", " << value.w
                  << "] expected [" << expected.x << ", " << expected.y << ", "
                  << expected.z << ", " << expected.w << "]\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    // Duration policy
    failed += !Expect(Near(ComputeUnpopDurationSec(0.0f), Tile::FADE_DURATION),
                      "speed=0 should use default unpop duration");
    failed += !Expect(Near(ComputeUnpopDurationSec(kUnpopShortenStartKmPerSec), Tile::FADE_DURATION),
                      "shorten-start threshold should still use default duration");
    failed += !Expect(Near(ComputeUnpopDurationSec(kUnpopBypassKmPerSec), kUnpopMinDurationSec),
                      "bypass threshold should clamp to min duration");
    failed += !Expect(ComputeUnpopDurationSec(300.0f) < Tile::FADE_DURATION &&
                      ComputeUnpopDurationSec(300.0f) > kUnpopMinDurationSec,
                      "mid speed should linearly shorten duration");

    // Bypass policy
    failed += !Expect(!ShouldBypassUnpop(kUnpopBypassKmPerSec - 1.0f), "below bypass threshold");
    failed += !Expect(ShouldBypassUnpop(kUnpopBypassKmPerSec), "at bypass threshold");
    failed += !Expect(ShouldBypassUnpop(kUnpopBypassKmPerSec + 250.0f), "above bypass threshold");

    // UV transform: parent(2,1,1) -> child quadrants at level 3 (factor 2)
    const TileKey parent(2, 1, 1);
    failed += !ExpectVec4(ComputeUnpopUvTransform(TileKey(3, 2, 2), parent), glm::vec4(0.5f, 0.5f, 0.0f, 0.5f),
                          "child NW in parent UV");
    failed += !ExpectVec4(ComputeUnpopUvTransform(TileKey(3, 3, 2), parent), glm::vec4(0.5f, 0.5f, 0.5f, 0.5f),
                          "child NE in parent UV");
    failed += !ExpectVec4(ComputeUnpopUvTransform(TileKey(3, 2, 3), parent), glm::vec4(0.5f, 0.5f, 0.0f, 0.0f),
                          "child SW in parent UV");
    failed += !ExpectVec4(ComputeUnpopUvTransform(TileKey(3, 3, 3), parent), glm::vec4(0.5f, 0.5f, 0.5f, 0.0f),
                          "child SE in parent UV");

    // Compose with atlas transform: outer(0.25,0.25,0.5,0.25) and inner(0.5,0.5,0.0,0.5)
    failed += !ExpectVec4(ComposeUvTransform(glm::vec4(0.25f, 0.25f, 0.5f, 0.25f),
                                             glm::vec4(0.5f, 0.5f, 0.0f, 0.5f)),
                          glm::vec4(0.125f, 0.125f, 0.5f, 0.375f),
                          "compose atlas + unpop transforms");

    if (failed == 0) {
        std::cout << "UnpopCrossfadePolicyTest PASSED\n";
        return 0;
    }

    std::cerr << "UnpopCrossfadePolicyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
