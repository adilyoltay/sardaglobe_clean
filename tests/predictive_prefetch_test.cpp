// Predictive Prefetch Test (P5.1)
// Verifies velocity-aware prefetch activation and threshold gating.

#include "../src/scheduling/lod_selector.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <unordered_set>
#include <vector>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

std::unordered_set<TileKey> ToSet(const std::vector<TileKey>& keys) {
    std::unordered_set<TileKey> set;
    set.reserve(keys.size());
    for (const TileKey& key : keys) {
        set.insert(key);
    }
    return set;
}

struct Scenario {
    glm::vec3 cameraPos;
    glm::vec3 velocity;
};

} // namespace

int main() {
    LodSelector selector;
    LodSelector::Settings settings;
    settings.minZoom = 0;
    settings.maxZoom = 8;
    settings.sseThreshold = 1.2f;
    settings.disableHorizonCull = true;  // Stabilize test against horizon edge conditions.

    auto isReady = [](const TileKey&) { return true; };

    const std::vector<Scenario> scenarios = {
        {{0.0f, -9000.0f, 1200.0f}, {0.0f, 0.8f, 0.0f}},
        {{9000.0f, 0.0f, 1600.0f}, {-0.8f, 0.0f, 0.0f}},
        {{4500.0f, -7800.0f, 2200.0f}, {-0.5f, 0.8f, 0.0f}},
    };

    bool foundPredictiveExpansion = false;
    int failed = 0;

    for (const Scenario& scenario : scenarios) {
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100000.0f);
        glm::mat4 view = glm::lookAt(scenario.cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 mvp = proj * view;

        LodSelection base = selector.Select(
            scenario.cameraPos,
            glm::vec3(0.0f),
            mvp,
            45.0f,
            10.0f,
            1920,
            1080,
            isReady,
            settings);

        LodSelection tinyVelocity = selector.Select(
            scenario.cameraPos,
            scenario.velocity * 0.01f,  // < 0.05 km/s threshold
            mvp,
            45.0f,
            10.0f,
            1920,
            1080,
            isReady,
            settings);

        std::unordered_set<TileKey> baseSet = ToSet(base.prefetch);
        std::unordered_set<TileKey> tinySet = ToSet(tinyVelocity.prefetch);
        failed += !Expect(baseSet == tinySet, "tiny velocity should not change prefetch set");

        LodSelection predictive = selector.Select(
            scenario.cameraPos,
            scenario.velocity,  // >= 0.05 km/s threshold
            mvp,
            45.0f,
            10.0f,
            1920,
            1080,
            isReady,
            settings);

        std::unordered_set<TileKey> predictiveSet = ToSet(predictive.prefetch);
        if (predictiveSet.size() > baseSet.size()) {
            foundPredictiveExpansion = true;
            continue;
        }
        for (const TileKey& key : predictiveSet) {
            if (baseSet.find(key) == baseSet.end()) {
                foundPredictiveExpansion = true;
                break;
            }
        }
    }

    failed += !Expect(foundPredictiveExpansion,
                      "predictive velocity should expand prefetch in at least one scenario");

    if (failed == 0) {
        std::cout << "PredictivePrefetchTest PASSED" << std::endl;
        return 0;
    }

    std::cerr << "PredictivePrefetchTest FAILED (" << failed << " checks failed)" << std::endl;
    return 1;
}
