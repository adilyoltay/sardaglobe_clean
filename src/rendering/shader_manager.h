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
    Terrain     = 1 << 4,  // GPU terrain displacement
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
    int GetTexScaleOffsetMainLocation() const { return texScaleOffsetMainLoc_; }
    int GetTextureUnpopLocation() const { return texUnpopLoc_; }
    int GetUnpopBlendLocation() const { return unpopBlendLoc_; }
    int GetTexScaleOffsetUnpopLocation() const { return texScaleOffsetUnpopLoc_; }
    int GetRasterCrossfadeLocation() const { return rasterCrossfadeLoc_; }
    int GetCornerLodsLocation() const { return cornerLodsLoc_; }
    int GetUseLogDepthLocation() const { return useLogDepthLoc_; }
    int GetLogDepthFarLocation() const { return logDepthFarLoc_; }
    
    // Terrain uniforms
    int GetHeightmapLocation() const { return heightmapLoc_; }
    int GetHeightScaleLocation() const { return heightScaleLoc_; }
    int GetHeightMinLocation() const { return heightMinLoc_; }
    int GetHeightMaxLocation() const { return heightMaxLoc_; }
    int GetHasHeightmapLocation() const { return hasHeightmapLoc_; }
    int GetHeightmapUvTransformLocation() const { return heightmapUvTransformLoc_; }
    int GetTerrainMorphLocation() const { return terrainMorphLoc_; }
    
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
    int texScaleOffsetMainLoc_ = -1;
    int texUnpopLoc_ = -1;
    int unpopBlendLoc_ = -1;
    int texScaleOffsetUnpopLoc_ = -1;
    int rasterCrossfadeLoc_ = -1;
    int cornerLodsLoc_ = -1;
    int useLogDepthLoc_ = -1;
    int logDepthFarLoc_ = -1;
    
    // Terrain uniforms
    int heightmapLoc_ = -1;
    int heightScaleLoc_ = -1;
    int heightMinLoc_ = -1;
    int heightMaxLoc_ = -1;
    int hasHeightmapLoc_ = -1;
    int heightmapUvTransformLoc_ = -1;
    int terrainMorphLoc_ = -1;
    
    ShaderFlags activeFlags_ = ShaderFlags::None;
};

// Shader source code
namespace shaders {

const char* const TILE_VERTEX = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in float aHeightKm;

uniform mat4 uMVP;
uniform sampler2D uHeightmap;
uniform float uHeightScale;
uniform float uHeightMin;
uniform float uHeightMax;
uniform int uHasHeightmap;
uniform vec4 uHeightmapUvTransform;  // Heightmap UV: uv * scale.xy + offset.zw
uniform vec4 uCornerLods;  // NW, NE, SE, SW corner LODs for bilinear interpolation
uniform float uTerrainMorph;  // 0=flat, 1=full displacement
uniform int uUseLogDepth;
uniform float uLogDepthFar;

out vec2 vTexCoord;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 pos = aPos;
    vec3 normal = aNormal;
    vec3 radialDir = normalize(aPos);
    
    if (uHasHeightmap == 1) {
        // Bilinear LOD interpolation from tile corners.
        // Top row: NW->NE, bottom row: SW->SE (UV has V-up after mesh V flip).
        float lodTop = mix(uCornerLods.x, uCornerLods.y, aTexCoord.x);
        float lodBottom = mix(uCornerLods.w, uCornerLods.z, aTexCoord.x);
        float lodInterp = clamp(mix(lodBottom, lodTop, aTexCoord.y), 0.0, 6.0);

        vec2 hmUv = aTexCoord * uHeightmapUvTransform.xy + uHeightmapUvTransform.zw;
        // Sample heightmap (normalized [0,1])
        float heightNorm = textureLod(uHeightmap, hmUv, lodInterp).r;
        float heightKm = mix(uHeightMin, uHeightMax, heightNorm);
        
        // Displace vertex radially outward from Earth center
        pos = aPos + radialDir * (heightKm * uTerrainMorph);
        
        // Recalculate normal from heightmap gradient (finite difference)
        float lodScale = exp2(lodInterp);
        vec2 texelSize = lodScale / vec2(textureSize(uHeightmap, 0));
        float hL = textureLod(uHeightmap, hmUv - vec2(texelSize.x, 0), lodInterp).r;
        float hR = textureLod(uHeightmap, hmUv + vec2(texelSize.x, 0), lodInterp).r;
        float hD = textureLod(uHeightmap, hmUv - vec2(0, texelSize.y), lodInterp).r;
        float hU = textureLod(uHeightmap, hmUv + vec2(0, texelSize.y), lodInterp).r;
        
        // Gradient in tangent space
        float dHdx = (hR - hL) * (uHeightMax - uHeightMin) * 0.5;
        float dHdy = (hU - hD) * (uHeightMax - uHeightMin) * 0.5;
        
        // Perturb normal based on gradient
        // Scale factor for normal perturbation (larger = more visible slopes)
        float normalScale = 50.0;
        vec3 perturbation = vec3(-dHdx * normalScale * uTerrainMorph, -dHdy * normalScale * uTerrainMorph, 1.0);
        
        // Transform perturbation to world space (approximate - assumes radial up)
        vec3 tangentU = normalize(cross(vec3(0, 0, 1), radialDir));
        if (length(tangentU) < 0.1) tangentU = normalize(cross(vec3(1, 0, 0), radialDir));
        vec3 tangentV = cross(radialDir, tangentU);
        
        vec3 displacedNormal = normalize(tangentU * perturbation.x + tangentV * perturbation.y + radialDir * perturbation.z);
        normal = normalize(mix(radialDir, displacedNormal, uTerrainMorph));
    } else {
        // CPU mesh bake path: smoothly remove baked elevation during morph start.
        // aHeightKm is vertex-local DEM height above/below the ellipsoid surface.
        float morph = clamp(uTerrainMorph, 0.0, 1.0);
        if (abs(aHeightKm) > 1e-6 && morph < 1.0) {
            pos = aPos - radialDir * (aHeightKm * (1.0 - morph));
            normal = normalize(mix(radialDir, aNormal, morph));
        }
    }
    
    gl_Position = uMVP * vec4(pos, 1.0);
    // Vertex-shader log-depth: preserves early-Z optimization (no gl_FragDepth write).
    // Outerra/Brano Kemen technique: encode log2(w+1) into clip-space Z.
    if (uUseLogDepth == 1) {
        float Fcoef = 2.0 / log2(max(1.0, uLogDepthFar) + 1.0);
        gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * Fcoef - 1.0;
    }
    vTexCoord = aTexCoord;
    vNormal = normal;
    vWorldPos = pos;
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
