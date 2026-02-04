#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace globe {

// Manages shader compilation and programs
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();
    
    // Get or create tile shader program
    uint32_t GetTileProgram();
    
    // Uniform locations
    int GetMvpLocation() const { return mvpLoc_; }
    int GetTextureLocation() const { return texLoc_; }
    int GetFadeLocation() const { return fadeLoc_; }
    
    // Use tile shader
    void UseTileShader();

private:
    uint32_t CompileShader(uint32_t type, const char* source);
    uint32_t LinkProgram(uint32_t vertShader, uint32_t fragShader);
    
    uint32_t tileProgram_ = 0;
    int mvpLoc_ = -1;
    int texLoc_ = -1;
    int fadeLoc_ = -1;
};

// Shader source code
namespace shaders {

const char* const TILE_VERTEX = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;

out vec2 vTexCoord;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vTexCoord = aTexCoord;
    vNormal = aNormal;
    vWorldPos = aPos;
}
)";

const char* const TILE_FRAGMENT = R"(
#version 330 core
in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform float uFade;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    
    // Simple lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(vNormal);
    float diff = max(dot(normal, lightDir), 0.3);
    
    vec3 color = texColor.rgb * diff;
    fragColor = vec4(color, texColor.a * uFade);
}
)";

} // namespace shaders

} // namespace globe
