// LOD child quorum regression test.

#include "../src/scheduling/lod_selector.h"
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <unordered_set>

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

    LodSelector selector;
    LodSelector::Settings settings;
    settings.minZoom = 0;
    settings.maxZoom = 1;
    settings.sseThreshold = 0.05f;  // Force refine at root.
    settings.disableFrustumCull = true;
    settings.disableHorizonCull = true;
    settings.enforceNeighborDelta = false;

    glm::vec3 cameraPos(0.0f, 0.0f, 9000.0f);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 200000.0f);
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 mvp = proj * view;

    std::unordered_set<TileKey> readyChildren = {
        TileKey(1, 0, 0),
        TileKey(1, 1, 0),
        TileKey(1, 0, 1),
        // TileKey(1, 1, 1) intentionally missing
    };

    auto readyThreeChildren = [&readyChildren](const TileKey& key) {
        return readyChildren.count(key) > 0;
    };

    settings.lodChildQuorum = true;
    LodSelection strict = selector.Select(
        cameraPos,
        glm::vec3(0.0f),
        mvp,
        45.0f,
        10.0f,
        1920,
        1080,
        readyThreeChildren,
        settings);

    const TileKey root(0, 0, 0);
    failed += !Expect(strict.leafSet.count(root) > 0,
                      "strict quorum should keep parent leaf when a child is missing");

    settings.lodChildQuorum = false;
    LodSelection relaxed = selector.Select(
        cameraPos,
        glm::vec3(0.0f),
        mvp,
        45.0f,
        10.0f,
        1920,
        1080,
        readyThreeChildren,
        settings);

    bool hasLevel1Leaf = false;
    for (const TileKey& leaf : relaxed.leafSet) {
        if (leaf.level == 1) {
            hasLevel1Leaf = true;
            break;
        }
    }
    failed += !Expect(hasLevel1Leaf, "relaxed quorum should allow refinement with partial child readiness");

    if (failed == 0) {
        std::cout << "LodChildQuorumTest PASSED\n";
        return 0;
    }

    std::cerr << "LodChildQuorumTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

