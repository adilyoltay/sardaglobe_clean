#include <array>
#include <iostream>

namespace {

enum class RenderBlockReason : unsigned char {
    None,
    NoTile,
    NoMesh,
    NoTexture,
    NoTerrain
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool ShouldCollapse(const std::array<RenderBlockReason, 4>& reasons) {
    int blockedCount = 0;
    int blockedTextureCount = 0;
    bool hasHardBlock = false;
    for (RenderBlockReason reason : reasons) {
        if (reason == RenderBlockReason::None) {
            continue;
        }
        ++blockedCount;
        if (reason == RenderBlockReason::NoTexture) {
            ++blockedTextureCount;
        } else {
            hasHardBlock = true;
        }
    }
    return (hasHardBlock && blockedCount >= 1) ||
           (!hasHardBlock && blockedTextureCount >= 2);
}

} // namespace

int main() {
    int failures = 0;

    if (!Expect(ShouldCollapse({RenderBlockReason::NoTerrain, RenderBlockReason::None,
                                RenderBlockReason::None, RenderBlockReason::None}),
                "Single NoTerrain child must collapse to parent")) {
        failures++;
    }

    if (!Expect(ShouldCollapse({RenderBlockReason::NoMesh, RenderBlockReason::None,
                                RenderBlockReason::None, RenderBlockReason::None}),
                "Single NoMesh child must collapse to parent")) {
        failures++;
    }

    if (!Expect(!ShouldCollapse({RenderBlockReason::NoTexture, RenderBlockReason::None,
                                 RenderBlockReason::None, RenderBlockReason::None}),
                "Single NoTexture child must not force collapse")) {
        failures++;
    }

    if (!Expect(ShouldCollapse({RenderBlockReason::NoTexture, RenderBlockReason::NoTexture,
                                RenderBlockReason::None, RenderBlockReason::None}),
                "Two NoTexture children must collapse to parent")) {
        failures++;
    }

    if (failures > 0) {
        std::cerr << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "RenderQuorumTerrainConsistencyTest PASSED\n";
    return 0;
}
