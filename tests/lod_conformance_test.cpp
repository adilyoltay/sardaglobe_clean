// LOD Conformance Test (FAZ 1.2)
// Tests that neighbor LOD delta is bounded by maxNeighborDelta

#include "../src/scheduling/lod_selector.h"
#include "../src/math/tile_math.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cassert>
#include <unordered_map>

using namespace globe;

// Mock isReady function - always returns true for testing conformance
bool AlwaysReady(const TileKey& key) {
    return true;
}

// Build maxDescLevel map from leafSet
std::unordered_map<TileKey, int> BuildMaxDescLevel(const std::unordered_set<TileKey>& leafSet) {
    std::unordered_map<TileKey, int> maxDescLevel;
    for (const TileKey& leaf : leafSet) {
        TileKey ancestor = leaf;
        while (ancestor.level >= 0) {
            auto it = maxDescLevel.find(ancestor);
            if (it == maxDescLevel.end()) {
                maxDescLevel[ancestor] = leaf.level;
            } else {
                it->second = std::max(it->second, leaf.level);
            }
            if (ancestor.level == 0) break;
            ancestor = ancestor.Parent();
        }
    }
    return maxDescLevel;
}

// Test that no leaf has a neighbor region with LOD delta > maxNeighborDelta
bool TestNeighborConformance(const LodSelection& selection, int maxNeighborDelta) {
    auto maxDescLevel = BuildMaxDescLevel(selection.leafSet);
    
    static const int dx[] = {0, 1, 0, -1};  // N, E, S, W
    static const int dy[] = {-1, 0, 1, 0};
    
    int violations = 0;
    
    for (const TileKey& leaf : selection.leafSet) {
        for (int dir = 0; dir < 4; ++dir) {
            TileKey neighborKey = leaf.Neighbor(dx[dir], dy[dir]);
            if (!neighborKey.IsValid()) continue;
            
            int neighborDeepest = neighborKey.level;
            auto it = maxDescLevel.find(neighborKey);
            if (it != maxDescLevel.end()) {
                neighborDeepest = it->second;
            }
            
            int delta = neighborDeepest - leaf.level;
            if (delta > maxNeighborDelta) {
                std::cerr << "VIOLATION: Leaf " << leaf.ToString() 
                          << " has neighbor region " << neighborKey.ToString()
                          << " with deepest level " << neighborDeepest
                          << " (delta=" << delta << " > " << maxNeighborDelta << ")"
                          << std::endl;
                violations++;
            }
        }
    }
    
    return violations == 0;
}

int main() {
    std::cout << "=== LOD Conformance Test ===" << std::endl;
    
    LodSelector selector;
    LodSelector::Settings settings;
    settings.minZoom = 0;
    settings.maxZoom = 10;
    settings.sseThreshold = 1.5f;
    settings.enforceNeighborDelta = true;
    settings.maxNeighborDelta = 1;
    settings.maxConformPasses = 6;
    
    // Test with camera at different positions
    struct TestCase {
        glm::vec3 cameraPos;
        float tiltDeg;
        const char* name;
    };
    
    TestCase testCases[] = {
        {{0, 0, 7000}, 0.0f, "Above origin (7000km)"},
        {{6371, 0, 0}, 0.0f, "At equator X (surface)"},
        {{0, 6371, 0}, 0.0f, "At equator Y (surface)"},
        {{4500, 4500, 0}, 0.0f, "Diagonal equator"},
        {{6400, 0, 200}, 30.0f, "Near-surface oblique (tilt 30)"},
        {{6200, 1200, 200}, 60.0f, "Near-surface oblique (tilt 60)"},
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& tc : testCases) {
        std::cout << "\nTest: " << tc.name << std::endl;
        
        // Simple MVP for testing
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100000.0f);
        glm::mat4 view = glm::lookAt(tc.cameraPos, glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
        glm::mat4 mvp = proj * view;
        
        LodSelection selection = selector.Select(
            tc.cameraPos, glm::vec3(0.0f), mvp,
            45.0f,  // FOV degrees
            tc.tiltDeg,
            1920, 1080,
            AlwaysReady,
            settings
        );
        
        std::cout << "  Leaves: " << selection.leaves.size() << std::endl;
        std::cout << "  Required: " << selection.required.size() << std::endl;
        std::cout << "  Refined: " << selection.refinedCount << std::endl;
        
        bool conformant = TestNeighborConformance(selection, settings.maxNeighborDelta);
        
        if (conformant) {
            std::cout << "  PASSED: Neighbor conformance satisfied" << std::endl;
            passed++;
        } else {
            std::cout << "  FAILED: Neighbor conformance violated" << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    return failed == 0 ? 0 : 1;
}
