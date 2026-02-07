// Mesh template stitch/skirt variant regression test.

#include "../src/rendering/mesh_template.h"
#include "../src/core/tile.h"

#include <iostream>

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
    constexpr int segments = 8;

    const uint8_t fullSkirts = static_cast<uint8_t>(Tile::EDGE_NORTH |
                                                    Tile::EDGE_EAST |
                                                    Tile::EDGE_SOUTH |
                                                    Tile::EDGE_WEST);
    const uint8_t eastNorthSkirts = static_cast<uint8_t>(Tile::EDGE_EAST | Tile::EDGE_NORTH);

    const auto& full = MeshTemplate::GetIndices(segments, 0x00, fullSkirts);
    const auto& partial = MeshTemplate::GetIndices(segments, Tile::EDGE_NORTH, eastNorthSkirts);

    const uint32_t mainCountFull = MeshTemplate::GetMainIndexCount(segments, 0x00);
    const uint32_t mainCountPartial = MeshTemplate::GetMainIndexCount(segments, Tile::EDGE_NORTH);
    const uint32_t fullExpected = mainCountFull + MeshTemplate::GetSkirtIndexCount(segments, fullSkirts);
    const uint32_t partialExpected = mainCountPartial + MeshTemplate::GetSkirtIndexCount(segments, eastNorthSkirts);

    failed += !Expect(!full.empty(), "full template should not be empty");
    failed += !Expect(!partial.empty(), "partial template should not be empty");
    failed += !Expect(full.size() == fullExpected, "full template index count should match expected");
    failed += !Expect(partial.size() == partialExpected, "partial template index count should match expected");
    failed += !Expect(full.size() > partial.size(), "full skirts should produce more indices than partial skirts");
    failed += !Expect(mainCountPartial < mainCountFull, "stitched main index count should be reduced");

    failed += !Expect(MeshTemplate::Exists(segments, 0x00, fullSkirts), "full variant should exist after creation");
    failed += !Expect(MeshTemplate::Exists(segments, Tile::EDGE_NORTH, eastNorthSkirts),
                      "stitch/skirt variant should exist after creation");

    if (failed == 0) {
        std::cout << "MeshTemplateStitchMaskTest PASSED\n";
        return 0;
    }

    std::cerr << "MeshTemplateStitchMaskTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
