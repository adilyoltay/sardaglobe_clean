#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace globe {

// Shader feature flags (GE-style variants)
enum class ShaderFlags : uint32_t {
    None            = 0,
    Wireframe       = 1 << 0,  // Render as wireframe
    DebugSeams      = 1 << 1,  // Highlight tile seams
    NoLighting      = 1 << 2,  // Disable lighting (flat shading)
    DebugLOD        = 1 << 3,  // Color-code by LOD level
    UseTextureArray = 1 << 4,  // Faz 2B: Use GL_TEXTURE_2D_ARRAY instead of atlas/individual textures
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
    
    // Faz 2B: Texture array uniforms
    int GetTextureArrayLocation() const { return texArrayLoc_; }
    int GetTextureLayerLocation() const { return texLayerLoc_; }
    int GetPhotoTileTextureUnpopArrayLocation() const { return photoTileTextureUnpopArrayLoc_; }
    int GetUnpopTextureLayerLocation() const { return unpopTextureLayerLoc_; }
    int GetUnpopUsesArrayLocation() const { return unpopUsesArrayLoc_; }
    int GetRasterCrossfadeLocation() const { return rasterCrossfadeLoc_; }
    int GetCornerLodsLocation() const { return cornerLodsLoc_; }
    int GetUseLogDepthLocation() const { return useLogDepthLoc_; }
    int GetLogDepthFarLocation() const { return logDepthFarLoc_; }
    
    int GetUseTexture2DLocation() const { return useTexture2DLoc_; }
    
    int GetTerrainMorphLocation() const { return terrainMorphLoc_; }
    
    // P1-5: Distance-based terrain morph uniforms
    int GetCameraPosLocation() const { return cameraPosLoc_; }
    int GetUseDistanceBasedMorphLocation() const { return useDistanceBasedMorphLoc_; }
    int GetMorphDistanceRangeLocation() const { return morphDistanceRangeLoc_; }
    
    // RTE uniforms
    int GetTileOriginHiLocation() const { return tileOriginHiLoc_; }
    int GetTileOriginLoLocation() const { return tileOriginLoLoc_; }
    int GetUseRteLocation() const { return useRteLoc_; }
    
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
    
    int terrainMorphLoc_ = -1;
    
    // P1-5: Distance-based terrain morph uniforms
    int cameraPosLoc_ = -1;
    int useDistanceBasedMorphLoc_ = -1;
    int morphDistanceRangeLoc_ = -1;
    
    // RTE uniforms
    int tileOriginHiLoc_ = -1;
    int tileOriginLoLoc_ = -1;
    int useRteLoc_ = -1;
    
    // Faz 2B: Texture array uniforms
    int texArrayLoc_ = -1;
    int texLayerLoc_ = -1;
    int useTexture2DLoc_ = -1;  // 0=array, 1=2D placeholder
    int photoTileTextureUnpopArrayLoc_ = -1;
    int unpopTextureLayerLoc_ = -1;
    int unpopUsesArrayLoc_ = -1;
    
    ShaderFlags activeFlags_ = ShaderFlags::None;
};

// Shader source code
namespace shaders {

const char* const TILE_VERTEX = R"(
#version 330 core
layout(location = 0) in vec3 aPos;          // RTE: position relative to tile origin
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in float aHeightKm;

uniform mat4 uMVP;
uniform vec4 uCornerLods;  // NW, NE, SE, SW corner LODs for bilinear interpolation
uniform float uTerrainMorph;  // Base morph value (0=flat, 1=full displacement)
uniform int uUseLogDepth;
uniform float uLogDepthFar;

// P1-5: Distance-based terrain morph uniforms
uniform vec3 uCameraPos;           // Camera position in km (ECEF)
uniform int uUseDistanceBasedMorph; // 0=use uTerrainMorph directly, 1=calculate based on distance
uniform float uMorphDistanceRangeKm; // Distance over which morph occurs (km)

// RTE (Relative-to-Center) uniforms for jitter-free rendering
uniform vec3 uTileOriginECEFHi;  // High 16 bits of tile origin
uniform vec3 uTileOriginECEFLo;  // Low 16 bits of tile origin
uniform int uUseRTE;             // 0=absolute positions, 1=RTE relative

out vec2 vTexCoord;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    // RTE: Reconstruct absolute world position from relative input
    vec3 worldPos;
    if (uUseRTE == 1) {
        vec3 tileOrigin = uTileOriginECEFHi + uTileOriginECEFLo;
        worldPos = tileOrigin + aPos;
    } else {
        worldPos = aPos;  // Legacy: absolute positions in vertex buffer
    }
    
    vec3 pos = worldPos;
    vec3 normal = normalize(aNormal);
    vec3 radialDir = normalize(worldPos);

    // P1-3: Corner LOD bilinear interpolation for seam smoothing
    // uCornerLods: NW(x), NE(y), SE(z), SW(w)
    // Calculate weighted LOD blend based on UV position
    float lodNW = uCornerLods.x * (1.0 - aTexCoord.x) * aTexCoord.y;
    float lodNE = uCornerLods.y * aTexCoord.x * aTexCoord.y;
    float lodSE = uCornerLods.z * aTexCoord.x * (1.0 - aTexCoord.y);
    float lodSW = uCornerLods.w * (1.0 - aTexCoord.x) * (1.0 - aTexCoord.y);
    float edgeLodBlend = lodNW + lodNE + lodSE + lodSW;
    
    // P1-3: Clamp edgeLodBlend to valid range (0-2 based on max possible corner LOD sum)
    edgeLodBlend = clamp(edgeLodBlend, 0.0, 2.0);
    
    // Corner LOD blend reduces height displacement at edges where neighbor is coarser
    // Clamp to ensure valid morph range [0.0, 1.0]
    float cornerLodMorph = clamp(1.0 - edgeLodBlend * 0.5, 0.0, 1.0);

    // P1-5: Distance-based terrain morph calculation
    // Calculate morph based on vertex distance from camera
    float distanceMorph;
    if (uUseDistanceBasedMorph == 1) {
        // CPU tracks morph state in uTerrainMorph and applies
        // distance/time-based policy with stable thresholds.
        // Keep shader-path in sync with CPU state to avoid unit/semantic drift.
        // Use CPU output directly; uMorphDistanceRangeKm remains for telemetry/debug,
        // but we keep this branch for command compatibility.
        distanceMorph = clamp(uTerrainMorph, 0.0, 1.0);
    } else {
        // Use uniform morph value directly
        distanceMorph = uTerrainMorph;
    }
    
    // CPU mesh bake path: smoothly remove baked elevation during morph start.
    // aHeightKm is vertex-local DEM height above/below the ellipsoid surface.
    float morph = clamp(distanceMorph, 0.0, 1.0);
    
    // P1-3: Apply corner LOD blend to height morph
    // Coarser edges (cornerLodMorph -> 0) should reduce displacement
    // Smooth edges (cornerLodMorph -> 1) use normal morph
    float adjustedMorph = clamp(morph * cornerLodMorph, 0.0, 1.0);
    
    if (abs(aHeightKm) > 1e-6 && adjustedMorph < 1.0) {
        pos = worldPos - radialDir * (aHeightKm * (1.0 - adjustedMorph));
        normal = normalize(mix(radialDir, aNormal, adjustedMorph));
    } else if (adjustedMorph < 1.0) {
        // P1-3: Use adjustedMorph for normal consistency
        normal = normalize(mix(radialDir, aNormal, adjustedMorph));
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
    vWorldPos = worldPos;
}
)";

const char* const TILE_FRAGMENT = R"(
#version 330 core
in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2D uTexture;
uniform sampler2D uPhotoTileTextureUnpop;
uniform float uUnpopBlend;
uniform vec4 uTexScaleOffsetMain;
uniform vec4 uTexScaleOffsetUnpop;
uniform int uRasterCrossfade;
uniform float uFade;

out vec4 fragColor;

void main() {
    vec2 uvMain = vTexCoord * uTexScaleOffsetMain.xy + uTexScaleOffsetMain.zw;
    vec4 texColor = texture(uTexture, uvMain);
    
    if (uRasterCrossfade == 1) {
        vec2 uvUnpop = vTexCoord * uTexScaleOffsetUnpop.xy + uTexScaleOffsetUnpop.zw;
        vec4 unpopColor = texture(uPhotoTileTextureUnpop, uvUnpop);
        float blend = clamp(uUnpopBlend, 0.0, 1.0);
        texColor = mix(unpopColor, texColor, blend);
    }
    
    // Simple lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(vNormal);
    float diff = max(dot(normal, lightDir), 0.3);
    
    vec3 color = texColor.rgb * diff;
    fragColor = vec4(color, texColor.a * uFade);
}
)";

// Faz 2B: Texture2DArray variant (prevents bleeding)
const char* const TILE_FRAGMENT_ARRAY = R"(
#version 330 core
in vec2 vTexCoord;
in vec3 vNormal;
in vec3 vWorldPos;

uniform sampler2DArray uTextureArray;
uniform int uTextureLayer;           // Layer index in the array
uniform sampler2D uPhotoTileTextureUnpop;
uniform sampler2DArray uPhotoTileTextureUnpopArray;
uniform int uUnpopTextureLayer;      // Ancestor array layer index
uniform int uUnpopUsesArray;         // 1=sample unpop from array, 0=2D
uniform float uUnpopBlend;
uniform int uRasterCrossfade;
uniform vec4 uTexScaleOffsetUnpop;   // xy=scale, zw=offset for unpop
uniform sampler2D uTexture;
uniform int uUseTexture2D;           // 0=array mode, 1=2D fallback
uniform float uFade;
uniform vec4 uCornerLods;
uniform float uTerrainMorph;
uniform int uUseLogDepth;
uniform float uLogDepthFar;

out vec4 fragColor;

void main() {
    vec4 texColor;
    if (uUseTexture2D == 1) {
        texColor = texture(uTexture, vTexCoord);
    } else {
        texColor = texture(uTextureArray, vec3(vTexCoord, float(uTextureLayer)));
    }
    
    if (uRasterCrossfade == 1) {
        vec2 uvUnpop = vTexCoord * uTexScaleOffsetUnpop.xy + uTexScaleOffsetUnpop.zw;
        vec4 unpopColor;
        if (uUnpopUsesArray == 1) {
            unpopColor = texture(uPhotoTileTextureUnpopArray, vec3(uvUnpop, float(uUnpopTextureLayer)));
        } else {
            unpopColor = texture(uPhotoTileTextureUnpop, uvUnpop);
        }
        float blend = clamp(uUnpopBlend, 0.0, 1.0);
        texColor = mix(unpopColor, texColor, blend);
    }
    
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
