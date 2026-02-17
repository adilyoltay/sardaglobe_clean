// Selective skirt mask regression test.

#include "../src/rendering/tile_mesh_builder.h"
#include "../src/rendering/mesh_template.h"
#include "../src/core/extent.h"
#include "../src/debug/network_panel.h"

#include <iostream>

using namespace globe;

namespace globe {

// Minimal stubs for linking dem_manager.cpp without UI backends.
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

    Config config;
    config.meshSegments = 8;
    config.selectiveSkirts = true;
    config.edgeStitching = true;
    config.terrainDisplacementMode = DisplacementMode::CPU_MESH_BAKE;
    config.demHeightScale = 1.0;

    TileKey key(5, 10, 12);
    Extent extent = Extent::FromTileWGS84(key.x, key.y, key.level);

    const uint8_t eastOnly = Tile::EDGE_EAST;
    auto eastResult = TileMeshBuilder::Build(
        key, extent,
        0,                 // stitchMask
        eastOnly,          // skirtMask
        key.level,         // dem target
        0,                 // demEdgeLevelPack
        nullptr,           // dem manager
        config,
        false              // shared ebo
    );

    failed += !Expect(eastResult.skirtMask == eastOnly, "result should preserve requested selective skirt mask");
    failed += !Expect(eastResult.mainIndexCount == MeshTemplate::GetMainIndexCount(eastResult.segments),
                      "main index count should match template");
    failed += !Expect(eastResult.skirtIndexCount == MeshTemplate::GetSkirtIndexCount(eastResult.segments, eastOnly),
                      "single-edge skirt count should match expected");
    failed += !Expect(eastResult.indexCount == eastResult.mainIndexCount + eastResult.skirtIndexCount,
                      "index count should be main + skirt");

    const uint8_t fullMask = static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_EAST |
                                                  Tile::EDGE_SOUTH | Tile::EDGE_WEST);
    auto fullResult = TileMeshBuilder::Build(
        key, extent,
        0,
        fullMask,
        key.level,
        0,
        nullptr,
        config,
        false
    );

    failed += !Expect(fullResult.skirtIndexCount > eastResult.skirtIndexCount,
                      "full skirt mask should produce more skirt indices than single-edge mask");
    failed += !Expect(fullResult.skirtIndexCount == MeshTemplate::GetSkirtIndexCount(fullResult.segments, fullMask),
                      "full skirt count should match expected");

    if (failed == 0) {
        std::cout << "SelectiveSkirtMaskTest PASSED\n";
        return 0;
    }

    std::cerr << "SelectiveSkirtMaskTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
