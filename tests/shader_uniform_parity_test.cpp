// Shader uniform parity test (CPU terrain authority).

#include "../src/rendering/shader_manager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
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

void Report(const char* test) {
    std::cerr << "PASSED: " << test << '\n';
}

std::string NormalizeLowerNoSpace(const char* src) {
    std::string out(src ? src : "");
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              out.end());
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

int main() {
    int failures = 0;

    {
        const char* vertexShader = shaders::TILE_VERTEX;
        bool hasOriginHi = std::strstr(vertexShader, "uTileOriginECEFHi") != nullptr;
        bool hasOriginLo = std::strstr(vertexShader, "uTileOriginECEFLo") != nullptr;
        bool hasUseRte = std::strstr(vertexShader, "uUseRTE") != nullptr;
        bool hasWorldPos = std::strstr(vertexShader, "worldPos") != nullptr;
        if (!Expect(hasOriginHi, "Vertex shader should contain uTileOriginECEFHi")) failures++;
        if (!Expect(hasOriginLo, "Vertex shader should contain uTileOriginECEFLo")) failures++;
        if (!Expect(hasUseRte, "Vertex shader should contain uUseRTE")) failures++;
        if (!Expect(hasWorldPos, "Vertex shader should compute worldPos")) failures++;
        if (hasOriginHi && hasOriginLo && hasUseRte && hasWorldPos) {
            Report("TileShaderHasRteUniforms");
        }
    }

    {
        const char* vertexShader = shaders::TILE_VERTEX;
        bool hasMvp = std::strstr(vertexShader, "uMVP") != nullptr;
        bool hasTerrainMorph = std::strstr(vertexShader, "uTerrainMorph") != nullptr;
        bool hasHeightmap = std::strstr(vertexShader, "uHeightmap") != nullptr;
        bool hasHasHeightmap = std::strstr(vertexShader, "uHasHeightmap") != nullptr;
        bool hasHeightMin = std::strstr(vertexShader, "uHeightMin") != nullptr;
        bool hasHeightMax = std::strstr(vertexShader, "uHeightMax") != nullptr;
        bool hasHeightScale = std::strstr(vertexShader, "uHeightScale") != nullptr;
        bool hasHeightUv = std::strstr(vertexShader, "uHeightmapUvTransform") != nullptr;
        if (!Expect(hasMvp, "Vertex shader should contain uMVP")) failures++;
        if (!Expect(hasTerrainMorph, "Vertex shader should contain uTerrainMorph")) failures++;
        if (!Expect(!hasHeightmap, "Vertex shader must not contain uHeightmap")) failures++;
        if (!Expect(!hasHasHeightmap, "Vertex shader must not contain uHasHeightmap")) failures++;
        if (!Expect(!hasHeightMin, "Vertex shader must not contain uHeightMin")) failures++;
        if (!Expect(!hasHeightMax, "Vertex shader must not contain uHeightMax")) failures++;
        if (!Expect(!hasHeightScale, "Vertex shader must not contain uHeightScale")) failures++;
        if (!Expect(!hasHeightUv, "Vertex shader must not contain uHeightmapUvTransform")) failures++;
        if (hasMvp && hasTerrainMorph && !hasHeightmap && !hasHasHeightmap &&
            !hasHeightMin && !hasHeightMax && !hasHeightScale && !hasHeightUv) {
            Report("CpuTerrainUniformContract");
        }
    }

    {
        const char* vertexShader = shaders::TILE_VERTEX;
        bool hasUseLogDepth = std::strstr(vertexShader, "uUseLogDepth") != nullptr;
        bool hasLogDepthFar = std::strstr(vertexShader, "uLogDepthFar") != nullptr;
        if (!Expect(hasUseLogDepth, "Vertex shader should contain uUseLogDepth")) failures++;
        if (!Expect(hasLogDepthFar, "Vertex shader should contain uLogDepthFar")) failures++;
        if (hasUseLogDepth && hasLogDepthFar) {
            Report("LogDepthUniformsPresent");
        }
    }

    {
        std::string shader = NormalizeLowerNoSpace(shaders::TILE_VERTEX);
        bool hasCpuWorldPattern =
            shader.find("pos=worldpos-radialdir*(aheightkm*(1.0-morph))") != std::string::npos;
        bool hasForbiddenApos =
            shader.find("pos=apos+radialdir") != std::string::npos ||
            shader.find("pos=apos-radialdir") != std::string::npos;
        if (!Expect(hasCpuWorldPattern, "CPU morph must adjust from worldPos")) failures++;
        if (!Expect(!hasForbiddenApos, "Shader must not contain aPos-based radial displacement")) failures++;
        if (hasCpuWorldPattern && !hasForbiddenApos) {
            Report("RteMorphUsesWorldPosForCpuBake");
        }
    }

    {
        const char* fragmentShader = shaders::TILE_FRAGMENT;
        bool hasTexture = std::strstr(fragmentShader, "uTexture") != nullptr;
        bool hasFade = std::strstr(fragmentShader, "uFade") != nullptr;
        if (!Expect(hasTexture, "Fragment shader should contain uTexture")) failures++;
        if (!Expect(hasFade, "Fragment shader should contain uFade")) failures++;
        if (hasTexture && hasFade) {
            Report("FragmentShaderHasBasicUniforms");
        }
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll shader uniform parity tests PASSED\n";
    return 0;
}
