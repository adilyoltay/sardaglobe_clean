#include "shader_manager.h"
#include <glad/glad.h>
#include <cstring>
#include <iostream>
#include <sstream>

namespace globe {

ShaderManager::ShaderManager() {}

ShaderManager::~ShaderManager() {
    if (tileProgram_ != 0) {
        glDeleteProgram(tileProgram_);
    }
    // Delete cached variant programs
    for (auto& [flags, program] : programCache_) {
        if (program != 0) {
            glDeleteProgram(program);
        }
    }
}

uint32_t ShaderManager::CompileShader(uint32_t type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t ShaderManager::LinkProgram(uint32_t vertShader, uint32_t fragShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program link error: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return program;
}

uint32_t ShaderManager::GetTileProgram() {
    return GetTileProgram(ShaderFlags::None);
}

uint32_t ShaderManager::GetTileProgram(ShaderFlags flags) {
    uint32_t flagsKey = static_cast<uint32_t>(flags);
    
    // Check cache first
    auto it = programCache_.find(flagsKey);
    if (it != programCache_.end()) {
        return it->second;
    }

    // P0 CRITICAL: Fail-fast guard for TILE_VERTEX morph patterns.
    // RTE path must use worldPos (world-space) for morph, NOT aPos (tile-local).
    // Using aPos directly causes km-level artifacts (spikes/walls) at tile boundaries.
    // 
    // Required patterns (safe):
    //   - Heightmap: pos = worldPos + radialDir * (heightKm * uTerrainMorph);
    //   - CPU bake:  pos = worldPos - radialDir * (aHeightKm * (1.0 - morph));
    //
    // Forbidden patterns (unsafe):
    //   - pos = aPos + radialDir * ...
    //   - pos = aPos - radialDir * ...
    //   - Any direct aPos-based radial displacement
    
    // Normalize shader source: remove all whitespace for reliable pattern matching
    std::string normalizedVert(shaders::TILE_VERTEX);
    normalizedVert.erase(
        std::remove_if(normalizedVert.begin(), normalizedVert.end(), 
            [](unsigned char c) { return std::isspace(c); }),
        normalizedVert.end());
    
    // Check for forbidden patterns (case-insensitive by converting to lower)
    std::string lowerVert = normalizedVert;
    std::transform(lowerVert.begin(), lowerVert.end(), lowerVert.begin(), ::tolower);
    
    // Forbidden: pos=apos+radialdir or pos=apos-radialdir (any spacing variant)
    if (lowerVert.find("pos=apos+radialdir") != std::string::npos ||
        lowerVert.find("pos=apos-radialdir") != std::string::npos) {
        std::cerr << "[ShaderManager] P0 SAFETY VIOLATION: TILE_VERTEX contains forbidden "
                     "aPos-based morph pattern (pos = aPos +/- radialDir).\n"
                     "This causes km-level terrain artifacts. Aborting compilation.\n";
        programCache_[flagsKey] = 0;
        if (flags == ShaderFlags::None) {
            tileProgram_ = 0;
        }
        return 0;
    }
    
    // Verify required patterns exist (worldPos-based morph)
    if (lowerVert.find("pos=worldpos+radialdir") == std::string::npos &&
        lowerVert.find("pos=worldpos-radialdir") == std::string::npos) {
        std::cerr << "[ShaderManager] P0 SAFETY VIOLATION: TILE_VERTEX missing required "
                     "worldPos-based morph pattern.\n"
                     "Expected: pos = worldPos +/- radialDir * ...\n";
        programCache_[flagsKey] = 0;
        if (flags == ShaderFlags::None) {
            tileProgram_ = 0;
        }
        return 0;
    }
    
    // Build and compile new variant
    GLuint vert = CompileShader(GL_VERTEX_SHADER, shaders::TILE_VERTEX);
    std::string fragSource = BuildFragmentShader(flags);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
    
    uint32_t program = 0;
    if (vert && frag) {
        program = LinkProgram(vert, frag);
    }
    
    // Cache the program
    programCache_[flagsKey] = program;
    
    // Also set as default if no flags
    if (flags == ShaderFlags::None) {
        tileProgram_ = program;
    }
    
    return program;
}

std::string ShaderManager::BuildFragmentShader(ShaderFlags flags) {
    std::ostringstream ss;
    
    ss << "#version 330 core\n";
    
    // Faz 2B: Texture array support
    bool useArray = HasFlag(flags, ShaderFlags::UseTextureArray);
    
    ss << "in vec2 vTexCoord;\n";
    ss << "in vec3 vNormal;\n";
    ss << "in vec3 vWorldPos;\n";
    ss << "\n";
    
    if (useArray) {
        ss << "uniform sampler2DArray uTextureArray;\n";
        ss << "uniform int uTextureLayer;\n";
        // Crossfade support for array mode with optional ancestor array texture
        ss << "uniform sampler2D uPhotoTileTextureUnpop;\n";
        ss << "uniform sampler2DArray uPhotoTileTextureUnpopArray;\n";
        ss << "uniform int uUnpopTextureLayer;\n";
        ss << "uniform int uUnpopUsesArray;\n";
        ss << "uniform float uUnpopBlend;\n";
        ss << "uniform int uRasterCrossfade;\n";       // 0=single texture, 1=crossfade
        ss << "uniform vec4 uTexScaleOffsetUnpop;\n"; // xy=scale, zw=offset for unpop
        // Placeholder fallback (2D texture for loading/placeholder tiles)
        ss << "uniform sampler2D uTexture;\n";
        ss << "uniform int uUseTexture2D;\n";          // 0=array, 1=2D placeholder
    } else {
        ss << "uniform sampler2D uTexture;\n";
        ss << "uniform sampler2D uPhotoTileTextureUnpop;\n";
        ss << "uniform float uUnpopBlend;\n";
        ss << "uniform vec4 uTexScaleOffsetMain;\n";   // xy=scale, zw=offset
        ss << "uniform vec4 uTexScaleOffsetUnpop;\n";  // xy=scale, zw=offset
        ss << "uniform int uRasterCrossfade;\n";       // 0=single texture, 1=crossfade
    }
    ss << "uniform float uFade;\n";
    
    if (HasFlag(flags, ShaderFlags::DebugLOD)) {
        ss << "uniform int uLodLevel;\n";
    }
    
    ss << "\n";
    ss << "out vec4 fragColor;\n";
    ss << "\n";
    
    // Faz 2B Fix: Bayer matrix for stochastic dithering during crossfade
    // 8x8 Bayer matrix for smooth LOD transitions (GE-style)
    ss << "const float bayer8x8[64] = float[](\n";
    ss << "    0.0/64.0, 48.0/64.0, 12.0/64.0, 60.0/64.0, 3.0/64.0, 51.0/64.0, 15.0/64.0, 63.0/64.0,\n";
    ss << "    32.0/64.0, 16.0/64.0, 44.0/64.0, 28.0/64.0, 35.0/64.0, 19.0/64.0, 47.0/64.0, 31.0/64.0,\n";
    ss << "    8.0/64.0, 56.0/64.0, 4.0/64.0, 52.0/64.0, 11.0/64.0, 59.0/64.0, 7.0/64.0, 55.0/64.0,\n";
    ss << "    40.0/64.0, 24.0/64.0, 36.0/64.0, 20.0/64.0, 43.0/64.0, 27.0/64.0, 39.0/64.0, 23.0/64.0,\n";
    ss << "    2.0/64.0, 50.0/64.0, 14.0/64.0, 62.0/64.0, 1.0/64.0, 49.0/64.0, 13.0/64.0, 61.0/64.0,\n";
    ss << "    34.0/64.0, 18.0/64.0, 46.0/64.0, 30.0/64.0, 33.0/64.0, 17.0/64.0, 45.0/64.0, 29.0/64.0,\n";
    ss << "    10.0/64.0, 58.0/64.0, 6.0/64.0, 54.0/64.0, 9.0/64.0, 57.0/64.0, 5.0/64.0, 53.0/64.0,\n";
    ss << "    42.0/64.0, 26.0/64.0, 38.0/64.0, 22.0/64.0, 41.0/64.0, 25.0/64.0, 37.0/64.0, 21.0/64.0\n";
    ss << ");\n";
    ss << "\n";
    ss << "float GetBayerValue(vec2 screenPos) {\n";
    ss << "    ivec2 pos = ivec2(mod(screenPos, 8.0));\n";
    ss << "    return bayer8x8[pos.y * 8 + pos.x];\n";
    ss << "}\n";
    ss << "\n";
    
    ss << "void main() {\n";
    
    if (useArray) {
        // Texture array path - supports both array and 2D placeholder
        ss << "    vec4 texColor;\n";
        ss << "    if (uUseTexture2D == 1) {\n";
        ss << "        texColor = texture(uTexture, vTexCoord);\n";
        ss << "    } else {\n";
        ss << "        texColor = texture(uTextureArray, vec3(vTexCoord, float(uTextureLayer)));\n";
        ss << "    }\n";
        ss << "    if (uRasterCrossfade == 1) {\n";
        ss << "        vec2 uvUnpop = vTexCoord * uTexScaleOffsetUnpop.xy + uTexScaleOffsetUnpop.zw;\n";
        ss << "        vec4 unpopColor;\n";
        ss << "        if (uUnpopUsesArray == 1) {\n";
        ss << "            unpopColor = texture(uPhotoTileTextureUnpopArray, vec3(uvUnpop, float(uUnpopTextureLayer)));\n";
        ss << "        } else {\n";
        ss << "            unpopColor = texture(uPhotoTileTextureUnpop, uvUnpop);\n";
        ss << "        }\n";
        ss << "        float blend = clamp(uUnpopBlend, 0.0, 1.0);\n";
        // Bayer dithering: stochastic crossfade for smoother transitions
        ss << "        float bayer = GetBayerValue(gl_FragCoord.xy);\n";
        ss << "        float ditheredBlend = blend + (bayer - 0.5) * 0.25;\n";
        ss << "        float useChild = step(ditheredBlend, 0.5);\n";
        ss << "        texColor = mix(unpopColor, texColor, useChild);\n";
        ss << "    }\n";
    } else {
        // Standard atlas/individual texture path
        ss << "    vec2 uvMain = vTexCoord * uTexScaleOffsetMain.xy + uTexScaleOffsetMain.zw;\n";
        ss << "    vec4 texColor = texture(uTexture, uvMain);\n";
        ss << "    if (uRasterCrossfade == 1) {\n";
        ss << "        vec2 uvUnpop = vTexCoord * uTexScaleOffsetUnpop.xy + uTexScaleOffsetUnpop.zw;\n";
        ss << "        vec4 unpopColor = texture(uPhotoTileTextureUnpop, uvUnpop);\n";
        ss << "        float blend = clamp(uUnpopBlend, 0.0, 1.0);\n";
        // Bayer dithering for non-array path too
        ss << "        float bayer = GetBayerValue(gl_FragCoord.xy);\n";
        ss << "        float ditheredBlend = blend + (bayer - 0.5) * 0.25;\n";
        ss << "        float useChild = step(ditheredBlend, 0.5);\n";
        ss << "        texColor = mix(unpopColor, texColor, useChild);\n";
        ss << "    }\n";
    }
    
    if (HasFlag(flags, ShaderFlags::DebugSeams)) {
        // Highlight tile edges
        ss << "    float edgeDist = min(min(vTexCoord.x, 1.0 - vTexCoord.x), min(vTexCoord.y, 1.0 - vTexCoord.y));\n";
        ss << "    if (edgeDist < 0.02) { texColor.rgb = vec3(1.0, 0.0, 0.0); }\n";
    }
    
    if (HasFlag(flags, ShaderFlags::DebugLOD)) {
        // Color-code by LOD level
        ss << "    vec3 lodColors[10] = vec3[](";
        ss << "vec3(1,0,0), vec3(1,0.5,0), vec3(1,1,0), vec3(0.5,1,0), vec3(0,1,0),";
        ss << "vec3(0,1,0.5), vec3(0,1,1), vec3(0,0.5,1), vec3(0,0,1), vec3(0.5,0,1));\n";
        ss << "    int idx = clamp(uLodLevel, 0, 9);\n";
        ss << "    texColor.rgb = mix(texColor.rgb, lodColors[idx], 0.4);\n";
    }
    
    if (HasFlag(flags, ShaderFlags::NoLighting)) {
        ss << "    vec3 color = texColor.rgb;\n";
    } else {
        // Standard lighting
        ss << "    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));\n";
        ss << "    vec3 normal = normalize(vNormal);\n";
        ss << "    float diff = max(dot(normal, lightDir), 0.3);\n";
    ss << "    vec3 color = texColor.rgb * diff;\n";
    }
    
    ss << "    fragColor = vec4(color, texColor.a * uFade);\n";
    ss << "}\n";
    
    return ss.str();
}

void ShaderManager::CacheUniformLocations(uint32_t program) {
    mvpLoc_ = glGetUniformLocation(program, "uMVP");
    texLoc_ = glGetUniformLocation(program, "uTexture");
    fadeLoc_ = glGetUniformLocation(program, "uFade");
    lodLevelLoc_ = glGetUniformLocation(program, "uLodLevel");
    texScaleOffsetMainLoc_ = glGetUniformLocation(program, "uTexScaleOffsetMain");
    texUnpopLoc_ = glGetUniformLocation(program, "uPhotoTileTextureUnpop");
    unpopBlendLoc_ = glGetUniformLocation(program, "uUnpopBlend");
    texScaleOffsetUnpopLoc_ = glGetUniformLocation(program, "uTexScaleOffsetUnpop");
    rasterCrossfadeLoc_ = glGetUniformLocation(program, "uRasterCrossfade");
    cornerLodsLoc_ = glGetUniformLocation(program, "uCornerLods");
    useLogDepthLoc_ = glGetUniformLocation(program, "uUseLogDepth");
    logDepthFarLoc_ = glGetUniformLocation(program, "uLogDepthFar");
    
    // Terrain uniforms
    heightmapLoc_ = glGetUniformLocation(program, "uHeightmap");
    heightScaleLoc_ = glGetUniformLocation(program, "uHeightScale");
    heightMinLoc_ = glGetUniformLocation(program, "uHeightMin");
    heightMaxLoc_ = glGetUniformLocation(program, "uHeightMax");
    hasHeightmapLoc_ = glGetUniformLocation(program, "uHasHeightmap");
    heightmapUvTransformLoc_ = glGetUniformLocation(program, "uHeightmapUvTransform");
    terrainMorphLoc_ = glGetUniformLocation(program, "uTerrainMorph");
    
    // RTE uniforms
    tileOriginHiLoc_ = glGetUniformLocation(program, "uTileOriginECEFHi");
    tileOriginLoLoc_ = glGetUniformLocation(program, "uTileOriginECEFLo");
    useRteLoc_ = glGetUniformLocation(program, "uUseRTE");
    
    // Faz 2B: Texture array uniforms
    texArrayLoc_ = glGetUniformLocation(program, "uTextureArray");
    texLayerLoc_ = glGetUniformLocation(program, "uTextureLayer");
    useTexture2DLoc_ = glGetUniformLocation(program, "uUseTexture2D");
    photoTileTextureUnpopArrayLoc_ = glGetUniformLocation(program, "uPhotoTileTextureUnpopArray");
    unpopTextureLayerLoc_ = glGetUniformLocation(program, "uUnpopTextureLayer");
    unpopUsesArrayLoc_ = glGetUniformLocation(program, "uUnpopUsesArray");
}

void ShaderManager::UseTileShader() {
    UseTileShader(ShaderFlags::None);
}

void ShaderManager::UseTileShader(ShaderFlags flags) {
    uint32_t program = GetTileProgram(flags);
    if (program) {
        glUseProgram(program);
        CacheUniformLocations(program);
        activeFlags_ = flags;
    }
}

} // namespace globe
