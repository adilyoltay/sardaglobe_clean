#include "../src/rendering/shader_manager.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
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

    const char* vertexShader = shaders::TILE_VERTEX;
    const char* fragmentShader = shaders::TILE_FRAGMENT;
    const char* fragmentArrayShader = shaders::TILE_FRAGMENT_ARRAY;

    if (!Expect(std::strstr(vertexShader, "aHeightKm") != nullptr,
                "Vertex shader must keep CPU mesh height attribute (aHeightKm)")) {
        failures++;
    }
    if (!Expect(std::strstr(vertexShader, "uTerrainMorph") != nullptr,
                "Vertex shader must keep CPU terrain morph uniform")) {
        failures++;
    }

    if (!Expect(std::strstr(vertexShader, "uHeightmap") == nullptr,
                "Vertex shader must not contain uHeightmap uniform")) {
        failures++;
    }
    if (!Expect(std::strstr(vertexShader, "uHasHeightmap") == nullptr,
                "Vertex shader must not contain uHasHeightmap uniform")) {
        failures++;
    }
    if (!Expect(std::strstr(vertexShader, "uHeightMin") == nullptr &&
                std::strstr(vertexShader, "uHeightMax") == nullptr &&
                std::strstr(vertexShader, "uHeightmapUvTransform") == nullptr,
                "Vertex shader must not contain legacy heightmap transform uniforms")) {
        failures++;
    }

    {
        std::string normalized = NormalizeLowerNoSpace(vertexShader);
        const bool hasCpuMorphWorldPos =
            normalized.find("pos=worldpos-radialdir*(aheightkm*(1.0-morph))") != std::string::npos;
        const bool hasForbiddenAposMorph =
            normalized.find("pos=apos+radialdir") != std::string::npos ||
            normalized.find("pos=apos-radialdir") != std::string::npos;
        if (!Expect(hasCpuMorphWorldPos, "CPU morph must use worldPos-based displacement")) failures++;
        if (!Expect(!hasForbiddenAposMorph, "Shader must not use aPos-based radial displacement")) failures++;
    }

    if (!Expect(std::strstr(fragmentShader, "uHeightmap") == nullptr &&
                std::strstr(fragmentArrayShader, "uHeightmap") == nullptr,
                "Fragment shaders must not expose heightmap uniforms")) {
        failures++;
    }

    if (failures > 0) {
        std::cerr << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "CpuTerrainShaderContractTest PASSED\n";
    return 0;
}
