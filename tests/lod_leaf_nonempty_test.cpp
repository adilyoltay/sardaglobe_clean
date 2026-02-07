// LOD leaf underflow regression test.
// Ensures required tiles never produce an empty leaf set.

#include "../src/scheduling/lod_selector.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool NeverReady(const TileKey&) {
    return false;
}

bool AlwaysReady(const TileKey&) {
    return true;
}

} // namespace

int main() {
    int failed = 0;

    LodSelector selector;
    LodSelector::Settings settings;
    settings.minZoom = 0;
    settings.maxZoom = 10;
    settings.sseThreshold = 1.2f;
    settings.disableFrustumCull = false;
    settings.disableHorizonCull = false;
    settings.enforceNeighborDelta = true;

    struct Case {
        glm::vec3 cameraPos;
        glm::vec3 target;
        float fov;
        float tilt;
    };

    std::vector<Case> cases = {
        {{0.0f, 0.0f, 9000.0f}, {0.0f, 0.0f, 0.0f}, 45.0f, 5.0f},
        {{6800.0f, 400.0f, 300.0f}, {0.0f, 0.0f, 0.0f}, 50.0f, 35.0f},
        {{6400.0f, 1200.0f, 200.0f}, {0.0f, 0.0f, 0.0f}, 55.0f, 60.0f},
    };

    for (const Case& c : cases) {
        glm::mat4 proj = glm::perspective(glm::radians(c.fov), 16.0f / 9.0f, 0.1f, 200000.0f);
        glm::mat4 view = glm::lookAt(c.cameraPos, c.target, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 mvp = proj * view;

        LodSelection neverReady = selector.Select(
            c.cameraPos, glm::vec3(0.0f), mvp, c.fov, c.tilt, 1920, 1080, NeverReady, settings);
        if (!neverReady.required.empty()) {
            failed += !Expect(!neverReady.leafSet.empty(),
                              "leafSet must not be empty when required is non-empty (never-ready path)");
        }

        LodSelection alwaysReady = selector.Select(
            c.cameraPos, glm::vec3(0.0f), mvp, c.fov, c.tilt, 1920, 1080, AlwaysReady, settings);
        if (!alwaysReady.required.empty()) {
            failed += !Expect(!alwaysReady.leafSet.empty(),
                              "leafSet must not be empty when required is non-empty (always-ready path)");
        }
    }

    if (failed == 0) {
        std::cout << "LodLeafNonEmptyTest PASSED\n";
        return 0;
    }

    std::cerr << "LodLeafNonEmptyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

