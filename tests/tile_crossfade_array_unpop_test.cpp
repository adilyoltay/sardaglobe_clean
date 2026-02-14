// Tile crossfade + array-unpop binding parity tests

#include "../src/rendering/tile_renderer.h"
#include "../src/rendering/shader_manager.h"
#include <cstring>
#include <iostream>
#include <type_traits>

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
    int failures = 0;

    // Test 1: API must include unpop array layer + mode parameters.
    using CrossfadeSig = void (globe::TileRenderer::*)(
        const globe::Tile&,
        uint32_t,
        int,
        globe::TileRenderer::TextureTarget,
        const glm::vec4&,
        float,
        uint32_t,
        float,
        float,
        const glm::vec4&,
        float);
    if (!Expect(std::is_same_v<decltype(&globe::TileRenderer::RenderTileWithCrossfade), CrossfadeSig>,
                "RenderTileWithCrossfade must accept explicit unpop target enum")) {
        failures++;
    } else {
        std::cerr << "PASSED: TileRendererCrossfadeSignature\n";
    }

    // Test 2: Array fragment variant must include ancestor array sampling path.
    const char* arrayFragment = globe::shaders::TILE_FRAGMENT_ARRAY;
    bool hasUnpopArrayUniform = std::strstr(arrayFragment, "uPhotoTileTextureUnpopArray") != nullptr;
    bool hasUnpopUsesArray = std::strstr(arrayFragment, "uUnpopUsesArray") != nullptr;
    bool hasArrayBranch = std::strstr(arrayFragment, "if (uUnpopUsesArray == 1)") != nullptr;
    bool hasUnpopLayer = std::strstr(arrayFragment, "uUnpopTextureLayer") != nullptr;
    bool hasUseTexture2DFallback = std::strstr(arrayFragment, "uUseTexture2D") != nullptr;
    if (!Expect(hasUnpopArrayUniform && hasUnpopUsesArray && hasArrayBranch && hasUnpopLayer && hasUseTexture2DFallback,
                "Array shader variant should include unpop array uniforms and branch")) {
        failures++;
    } else {
        std::cerr << "PASSED: ArrayShaderContainsUnpopArrayPath\n";
    }

    // Test 3: Non-array shader path should still expose legacy unpop sampling.
    const char* legacyFragment = globe::shaders::TILE_FRAGMENT;
    bool hasLegacyUnpop = std::strstr(legacyFragment, "uPhotoTileTextureUnpop") != nullptr;
    if (!Expect(hasLegacyUnpop, "Legacy shader should keep uPhotoTileTextureUnpop uniform")) {
        failures++;
    } else {
        std::cerr << "PASSED: LegacyShaderUnpopUniformPresent\n";
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll tile crossfade array-unpop tests PASSED\n";
    return 0;
}
