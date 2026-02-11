// LOD refine-budget regression test.
// Ensures selection applies progressive refinement when per-frame budget is capped.

#include "../src/scheduling/lod_selector.h"
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

int MaxLeafLevel(const LodSelection& selection) {
    int maxLevel = 0;
    for (const TileKey& leaf : selection.leafSet) {
        maxLevel = std::max(maxLevel, leaf.level);
    }
    return maxLevel;
}

bool HasLeafLevel(const LodSelection& selection, int level) {
    for (const TileKey& leaf : selection.leafSet) {
        if (leaf.level == level) {
            return true;
        }
    }
    return false;
}

bool HasRequiredLevel(const LodSelection& selection, int level) {
    for (const TileKey& key : selection.required) {
        if (key.level == level) {
            return true;
        }
    }
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
    settings.maxZoom = 2;
    settings.sseThreshold = 0.05f;  // Aggressive refine for test determinism.
    settings.disableFrustumCull = true;
    settings.disableHorizonCull = true;
    settings.enforceNeighborDelta = false;
    settings.lodChildQuorum = true;

    glm::vec3 cameraPos(0.0f, 0.0f, 9000.0f);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 200000.0f);
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 mvp = proj * view;

    // Unlimited: should fully refine to maxZoom.
    settings.maxRefinementsPerFrame = 0;  // <=0 => unlimited
    LodSelection unlimited = selector.Select(
        cameraPos, glm::vec3(0.0f), mvp, 45.0f, 10.0f, 1920, 1080, AlwaysReady, settings);

    failed += !Expect(MaxLeafLevel(unlimited) == 2, "unlimited budget should refine to maxZoom");
    failed += !Expect(unlimited.refinedCount >= 5, "unlimited budget should apply multiple refinements");

    // Tight budget: refine root only this frame.
    settings.maxRefinementsPerFrame = 1;
    LodSelection budget1 = selector.Select(
        cameraPos, glm::vec3(0.0f), mvp, 45.0f, 10.0f, 1920, 1080, AlwaysReady, settings);

    failed += !Expect(budget1.refinedCount <= 1, "budget=1 must cap refinement count");
    failed += !Expect(MaxLeafLevel(budget1) == 1, "budget=1 should stop at level-1 leaves");
    failed += !Expect(HasRequiredLevel(budget1, 2), "budget-limited frame should still request deeper children");

    // Mid budget: should produce mixed-level leaves (progressive refinement).
    settings.maxRefinementsPerFrame = 3;
    LodSelection budget3 = selector.Select(
        cameraPos, glm::vec3(0.0f), mvp, 45.0f, 10.0f, 1920, 1080, AlwaysReady, settings);

    failed += !Expect(budget3.refinedCount <= 3, "budget=3 must cap refinement count");
    failed += !Expect(HasLeafLevel(budget3, 1) && HasLeafLevel(budget3, 2),
                      "budget=3 should yield mixed LOD levels (progressive)");

    if (failed == 0) {
        std::cout << "LodRefineBudgetTest PASSED\n";
        return 0;
    }

    std::cerr << "LodRefineBudgetTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

