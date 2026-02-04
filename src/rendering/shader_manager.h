#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace globe {

// Shader feature flags (GE-style variants)
enum class ShaderFlags : uint32_t {
    None        = 0,
    Wireframe   = 1 << 0,  // Render as wireframe
    DebugSeams  = 1 << 1,  // Highlight tile seams
    NoLighting  = 1 << 2,  // Disable lighting (flat shading)
    DebugLOD    = 1 << 3,  // Color-code by LOD level
};

inline ShaderFlags operator|(ShaderFlags a, ShaderFlags b) {
    return static_cast<ShaderFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ShaderFlags operator&(ShaderFlags a, ShaderFlags b) {
    return static_cast<ShaderFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(ShaderFlags flags, ShaderFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Manages shader compilation and programs
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();
    
    // Get or create tile shader program (default variant)
    uint32_t GetTileProgram();
    
    // Get or create tile shader with specific flags (GE-style variants)
    uint32_t GetTileProgram(ShaderFlags flags);
    
    // Uniform locations (for current active program)
    int GetMvpLocation() const { return mvpLoc_; }
    int GetTextureLocation() const { return texLoc_; }
    int GetFadeLocation() const { return fadeLoc_; }
    int GetLodLevelLocation() const { return lodLevelLoc_; }
    
    // Use tile shader (default or with flags)
    void UseTileShader();
    void UseTileShader(ShaderFlags flags);
    
    // Get current active flags
    ShaderFlags GetActiveFlags() const { return activeFlags_; }

private:
    uint32_t CompileShader(uint32_t type, const char* source);
    uint32_t LinkProgram(uint32_t vertShader, uint32_t fragShader);
    std::string BuildFragmentShader(ShaderFlags flags);
    void CacheUniformLocations(uint32_t program);
    
    // Default program (no flags)
    uint32_t tileProgram_ = 0;
    
    // Variant cache: flags -> program
    std::unordered_map<uint32_t, uint32_t> programCache_;
    
    // Current active program uniforms
    int mvpLoc_ = -1;
    int texLoc_ = -1;
    int fadeLoc_ = -1;
    int lodLevelLoc_ = -1;
    
    ShaderFlags activeFlags_ = ShaderFlags::None;
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
