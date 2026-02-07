// Corner LOD Test
// Verifies edge-mask -> uCornerLods mapping for bilinear interpolation.

#include "../src/rendering/corner_lod.h"
#include <cmath>
#include <iostream>

using namespace globe;

namespace {

bool Near(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool ExpectVec4(const glm::vec4& value, const glm::vec4& expected, const char* name) {
    bool ok = Near(value.x, expected.x) &&
              Near(value.y, expected.y) &&
              Near(value.z, expected.z) &&
              Near(value.w, expected.w);
    if (!ok) {
        std::cerr << "FAILED: " << name
                  << " got [" << value.x << ", " << value.y << ", " << value.z << ", " << value.w
                  << "] expected [" << expected.x << ", " << expected.y << ", "
                  << expected.z << ", " << expected.w << "]\n";
    }
    return ok;
}

} // namespace

int main() {
    int failed = 0;

    failed += !ExpectVec4(CornerLodsFromEdgeMask(0), glm::vec4(0, 0, 0, 0), "no edges");
    failed += !ExpectVec4(CornerLodsFromEdgeMask(Tile::EDGE_NORTH), glm::vec4(1, 1, 0, 0), "north edge");
    failed += !ExpectVec4(CornerLodsFromEdgeMask(Tile::EDGE_EAST), glm::vec4(0, 1, 1, 0), "east edge");
    failed += !ExpectVec4(CornerLodsFromEdgeMask(Tile::EDGE_SOUTH), glm::vec4(0, 0, 1, 1), "south edge");
    failed += !ExpectVec4(CornerLodsFromEdgeMask(Tile::EDGE_WEST), glm::vec4(1, 0, 0, 1), "west edge");
    failed += !ExpectVec4(
        CornerLodsFromEdgeMask(Tile::EDGE_NORTH | Tile::EDGE_WEST),
        glm::vec4(1, 1, 0, 1),
        "north+west edges");
    failed += !ExpectVec4(
        CornerLodsFromEdgeMask(Tile::EDGE_EAST | Tile::EDGE_SOUTH),
        glm::vec4(0, 1, 1, 1),
        "east+south edges");
    failed += !ExpectVec4(
        CornerLodsFromEdgeMask(Tile::EDGE_NORTH | Tile::EDGE_EAST | Tile::EDGE_SOUTH | Tile::EDGE_WEST),
        glm::vec4(1, 1, 1, 1),
        "all edges");

    if (failed == 0) {
        std::cout << "CornerLodTest PASSED\n";
        return 0;
    }

    std::cerr << "CornerLodTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
