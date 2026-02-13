// TilePyramid child-ready policy regression.
// Ensures child refinement depends on texture readiness, not mesh presence.

#include "../src/scheduling/tile_pyramid.h"
#include <glm/gtc/matrix_transform.hpp>

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

    TilePyramid pyramid;
    auto settings = pyramid.GetSettings();
    settings.minZoom = 0;
    settings.maxZoom = 1;
    settings.sseThreshold = 0.01f;
    settings.minLodPixels = 0.0f;  // Disable for forced-refine test scenarios  // Force subdivision at root.
    settings.disableFrustumCull = true;
    settings.disableHorizonCull = true;
    settings.enforceNeighborDelta = false;
    settings.lodChildQuorum = true;
    pyramid.SetSettings(settings);

    TilePyramid::TileMap tiles;
    for (const TileKey& child : TileKey(0, 0, 0).Children()) {
        Tile t(child);
        t.state = TileState::Ready;
        t.textureId = 1;
        t.hasMesh = false;  // Important: child mesh is intentionally absent.
        tiles.emplace(child, t);
    }

    glm::vec3 cameraPos(0.0f, 0.0f, 9000.0f);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 200000.0f);
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 mvp = proj * view;
    glm::vec3 viewDir = glm::normalize(glm::vec3(0.0f) - cameraPos);

    const LodSelection& selection = pyramid.Select(
        cameraPos,
        glm::vec3(0.0f),
        viewDir,
        mvp,
        45.0f,
        10.0f,
        1920,
        1080,
        tiles);

    bool hasLevel1Leaf = false;
    for (const TileKey& leaf : selection.leafSet) {
        if (leaf.level == 1) {
            hasLevel1Leaf = true;
            break;
        }
    }

    failed += !Expect(hasLevel1Leaf, "child tiles with ready textures should refine even without mesh");

    if (failed == 0) {
        std::cout << "TilePyramidChildReadyTest PASSED\n";
        return 0;
    }

    std::cerr << "TilePyramidChildReadyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

