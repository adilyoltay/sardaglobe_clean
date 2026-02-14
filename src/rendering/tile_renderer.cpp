#include "tile_renderer.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <cassert>

namespace globe {

namespace {

inline void ApplyPerTileUniforms(ShaderManager& shaderManager, const Tile& tile, const glm::vec4& texScaleOffsetMain, bool useRte) {
    glUniform4f(shaderManager.GetCornerLodsLocation(),
                tile.cornerLods.x,
                tile.cornerLods.y,
                tile.cornerLods.z,
                tile.cornerLods.w);
    glUniform4f(shaderManager.GetTexScaleOffsetMainLocation(),
                texScaleOffsetMain.x,
                texScaleOffsetMain.y,
                texScaleOffsetMain.z,
                texScaleOffsetMain.w);
    
    // RTE (Relative-to-Center) uniforms for jitter-free rendering
    if (useRte) {
        glUniform3f(shaderManager.GetTileOriginHiLocation(),
                    tile.originEcefHi.x,
                    tile.originEcefHi.y,
                    tile.originEcefHi.z);
        glUniform3f(shaderManager.GetTileOriginLoLocation(),
                    tile.originEcefLo.x,
                    tile.originEcefLo.y,
                    tile.originEcefLo.z);
        glUniform1i(shaderManager.GetUseRteLocation(), 1);
    } else {
        glUniform1i(shaderManager.GetUseRteLocation(), 0);
    }
}

inline void ResetCrossfadeState(ShaderManager& shaderManager, bool useTextureArrayBatch) {
    glUniform1i(shaderManager.GetRasterCrossfadeLocation(), 0);
    glUniform1f(shaderManager.GetUnpopBlendLocation(), 1.0f);
    glUniform4f(shaderManager.GetTexScaleOffsetUnpopLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    if (useTextureArrayBatch) {
        glUniform1i(shaderManager.GetUnpopUsesArrayLocation(), 0);
        glUniform1i(shaderManager.GetUnpopTextureLayerLocation(), 0);
        glUniform1i(shaderManager.GetUseTexture2DLocation(), 0);
    }
}

std::size_t gUnpopArrayTo2DFallbacks = 0;

struct DrawCallBreakdown {
    int drawCalls = 0;
    int triangles = 0;
};

DrawCallBreakdown DrawTileGeometry(const Tile& tile) {
    DrawCallBreakdown out;
    const uint32_t mainCount = (tile.mainIndexCount > 0 || tile.skirtIndexCount > 0)
        ? tile.mainIndexCount
        : tile.indexCount;
    const uint32_t skirtCount = tile.skirtIndexCount;

    if (mainCount > 0) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mainCount), GL_UNSIGNED_INT, nullptr);
        ++out.drawCalls;
        out.triangles += static_cast<int>(mainCount / 3);
    }

    if (skirtCount > 0) {
        GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
        if (cullEnabled) {
            glDisable(GL_CULL_FACE);
        }
        GLboolean polyOffsetEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_POLYGON_OFFSET_FILL);
        // Use a stronger offset for skirts, then restore the batch's default
        // offset so subsequent main-tile draws don't inherit skirt bias (causes dark grids).
        // FIX 4: Matched to increased ancestor offset (2.0, 4.0) → skirt (3.0, 6.0).
        glPolygonOffset(3.0f, 6.0f);
        const std::size_t offsetBytes = static_cast<std::size_t>(mainCount) * sizeof(unsigned int);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(skirtCount),
                       GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offsetBytes));
        ++out.drawCalls;
        out.triangles += static_cast<int>(skirtCount / 3);
        glPolygonOffset(2.0f, 4.0f);
        if (!polyOffsetEnabled) {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (cullEnabled) {
            glEnable(GL_CULL_FACE);
        }
    }

    return out;
}

constexpr char kInstancedFlatTileVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 aGridUv;
layout(location = 1) in vec4 aExtentDeg;       // west, east, south, north
layout(location = 2) in vec4 aTexScaleOffset;  // xy scale, zw offset
layout(location = 3) in vec4 aInstanceData;    // x: fade

uniform mat4 uMVP;
uniform int uUseLogDepth;
uniform float uLogDepthFar;

out vec2 vTexCoord;
out vec3 vNormal;
out float vFade;

const float PI = 3.14159265358979323846;
const float WGS84_A_KM = 6378.137;
const float WGS84_E2 = 0.00669437999014;

void main() {
    float north = radians(aExtentDeg.w);
    float south = radians(aExtentDeg.z);
    float west = radians(aExtentDeg.x);
    float east = radians(aExtentDeg.y);

    float mercTop = log(tan(PI * 0.25 + north * 0.5));
    float mercBottom = log(tan(PI * 0.25 + south * 0.5));
    float merc = mix(mercBottom, mercTop, aGridUv.y);
    float lat = 2.0 * atan(exp(merc)) - PI * 0.5;
    float lon = mix(west, east, aGridUv.x);

    float sinLat = sin(lat);
    float cosLat = cos(lat);
    float sinLon = sin(lon);
    float cosLon = cos(lon);

    float N = WGS84_A_KM / sqrt(max(1e-8, 1.0 - WGS84_E2 * sinLat * sinLat));
    vec3 pos = vec3(
        N * cosLat * cosLon,
        N * cosLat * sinLon,
        (N * (1.0 - WGS84_E2)) * sinLat
    );

    gl_Position = uMVP * vec4(pos, 1.0);
    if (uUseLogDepth == 1) {
        float Fcoef = 2.0 / log2(max(1.0, uLogDepthFar) + 1.0);
        gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * Fcoef - 1.0;
    }
    vNormal = normalize(pos);
    vTexCoord = aGridUv * aTexScaleOffset.xy + aTexScaleOffset.zw;
    vFade = clamp(aInstanceData.x, 0.0, 1.0);
}
)";

constexpr char kInstancedFlatTileFragmentShader[] = R"(
#version 330 core
in vec2 vTexCoord;
in vec3 vNormal;
in float vFade;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(vNormal), lightDir), 0.3);
    vec3 color = texColor.rgb * diff;
    fragColor = vec4(color, texColor.a * vFade);
}
)";

// Faz 2B: Texture Array variant for instanced rendering
constexpr char kInstancedArrayVertexShader[] = R"(
#version 330 core
layout(location = 0) in vec2 aGridUv;
layout(location = 1) in vec4 aExtentDeg;       // west, east, south, north
layout(location = 2) in vec4 aTexScaleOffset;  // xy scale, zw offset
layout(location = 3) in vec4 aInstanceData;    // x: fade, y: layer index

uniform mat4 uMVP;
uniform int uUseLogDepth;
uniform float uLogDepthFar;

out vec2 vTexCoord;
out vec3 vNormal;
out float vFade;
out float vLayer;

const float PI = 3.14159265358979323846;
const float WGS84_A_KM = 6378.137;
const float WGS84_E2 = 0.00669437999014;

void main() {
    float north = radians(aExtentDeg.w);
    float south = radians(aExtentDeg.z);
    float west = radians(aExtentDeg.x);
    float east = radians(aExtentDeg.y);

    float mercTop = log(tan(PI * 0.25 + north * 0.5));
    float mercBottom = log(tan(PI * 0.25 + south * 0.5));
    float merc = mix(mercBottom, mercTop, aGridUv.y);
    float lat = 2.0 * atan(exp(merc)) - PI * 0.5;
    float lon = mix(west, east, aGridUv.x);

    float sinLat = sin(lat);
    float cosLat = cos(lat);
    float sinLon = sin(lon);
    float cosLon = cos(lon);

    float N = WGS84_A_KM / sqrt(max(1e-8, 1.0 - WGS84_E2 * sinLat * sinLat));
    vec3 pos = vec3(
        N * cosLat * cosLon,
        N * cosLat * sinLon,
        (N * (1.0 - WGS84_E2)) * sinLat
    );

    gl_Position = uMVP * vec4(pos, 1.0);
    if (uUseLogDepth == 1) {
        float Fcoef = 2.0 / log2(max(1.0, uLogDepthFar) + 1.0);
        gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * Fcoef - 1.0;
    }
    vNormal = normalize(pos);
    vTexCoord = aGridUv * aTexScaleOffset.xy + aTexScaleOffset.zw;
    vFade = clamp(aInstanceData.x, 0.0, 1.0);
    vLayer = aInstanceData.y;  // Pass layer index to fragment shader
}
)";

constexpr char kInstancedArrayFragmentShader[] = R"(
#version 330 core
in vec2 vTexCoord;
in vec3 vNormal;
in float vFade;
in float vLayer;

uniform sampler2DArray uTextureArray;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTextureArray, vec3(vTexCoord, vLayer));
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(vNormal), lightDir), 0.3);
    vec3 color = texColor.rgb * diff;
    fragColor = vec4(color, texColor.a * vFade);
}
)";

} // namespace

TileRenderer::TileRenderer(ShaderManager& shaderManager)
    : shaderManager_(shaderManager) {
    InitInstancedResources();
}

TileRenderer::~TileRenderer() {
    DestroyInstancedResources();
}

uint32_t TileRenderer::CompileShader(uint32_t type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Instanced shader compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t TileRenderer::LinkProgram(uint32_t vertexShader, uint32_t fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Instanced program link error: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool TileRenderer::InitInstancedResources() {
    // Standard 2D instanced shader
    uint32_t vertexShader = CompileShader(GL_VERTEX_SHADER, kInstancedFlatTileVertexShader);
    uint32_t fragmentShader = CompileShader(GL_FRAGMENT_SHADER, kInstancedFlatTileFragmentShader);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return false;
    }

    instancedProgram_ = LinkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (instancedProgram_ == 0) {
        return false;
    }

    instancedMvpLoc_ = glGetUniformLocation(instancedProgram_, "uMVP");
    instancedTextureLoc_ = glGetUniformLocation(instancedProgram_, "uTexture");
    instancedUseLogDepthLoc_ = glGetUniformLocation(instancedProgram_, "uUseLogDepth");
    instancedLogDepthFarLoc_ = glGetUniformLocation(instancedProgram_, "uLogDepthFar");

    glGenVertexArrays(1, &instancedVao_);
    glGenBuffers(1, &instancedGridVbo_);
    glGenBuffers(1, &instancedGridEbo_);
    glGenBuffers(1, &instancedInstanceVbo_);
    
    // Faz 2B: Texture array instanced shader
    uint32_t arrayVertShader = CompileShader(GL_VERTEX_SHADER, kInstancedArrayVertexShader);
    uint32_t arrayFragShader = CompileShader(GL_FRAGMENT_SHADER, kInstancedArrayFragmentShader);
    if (arrayVertShader != 0 && arrayFragShader != 0) {
        instancedArrayProgram_ = LinkProgram(arrayVertShader, arrayFragShader);
        glDeleteShader(arrayVertShader);
        glDeleteShader(arrayFragShader);
        
        if (instancedArrayProgram_ != 0) {
            instancedArrayMvpLoc_ = glGetUniformLocation(instancedArrayProgram_, "uMVP");
            instancedArrayTextureLoc_ = glGetUniformLocation(instancedArrayProgram_, "uTextureArray");
            instancedArrayUseLogDepthLoc_ = glGetUniformLocation(instancedArrayProgram_, "uUseLogDepth");
            instancedArrayLogDepthFarLoc_ = glGetUniformLocation(instancedArrayProgram_, "uLogDepthFar");
            instancedArrayTextureLayerLoc_ = glGetUniformLocation(instancedArrayProgram_, "uTextureLayer");
            
            glGenVertexArrays(1, &instancedArrayVao_);
            glGenBuffers(1, &instancedArrayInstanceVbo_);
        }
    } else {
        if (arrayVertShader != 0) glDeleteShader(arrayVertShader);
        if (arrayFragShader != 0) glDeleteShader(arrayFragShader);
    }
    
    return true;
}

void TileRenderer::DestroyInstancedResources() {
    if (instancedInstanceVbo_ != 0) {
        glDeleteBuffers(1, &instancedInstanceVbo_);
        instancedInstanceVbo_ = 0;
    }
    if (instancedGridEbo_ != 0) {
        glDeleteBuffers(1, &instancedGridEbo_);
        instancedGridEbo_ = 0;
    }
    if (instancedGridVbo_ != 0) {
        glDeleteBuffers(1, &instancedGridVbo_);
        instancedGridVbo_ = 0;
    }
    if (instancedVao_ != 0) {
        glDeleteVertexArrays(1, &instancedVao_);
        instancedVao_ = 0;
    }
    if (instancedProgram_ != 0) {
        glDeleteProgram(instancedProgram_);
        instancedProgram_ = 0;
    }
    
    // Faz 2B: Cleanup array instancing resources
    if (instancedArrayInstanceVbo_ != 0) {
        glDeleteBuffers(1, &instancedArrayInstanceVbo_);
        instancedArrayInstanceVbo_ = 0;
    }
    if (instancedArrayVao_ != 0) {
        glDeleteVertexArrays(1, &instancedArrayVao_);
        instancedArrayVao_ = 0;
    }
    if (instancedArrayProgram_ != 0) {
        glDeleteProgram(instancedArrayProgram_);
        instancedArrayProgram_ = 0;
    }
    instancedArrayMvpLoc_ = -1;
    instancedArrayTextureLoc_ = -1;
    instancedArrayUseLogDepthLoc_ = -1;
    instancedArrayLogDepthFarLoc_ = -1;
    instancedArrayTextureLayerLoc_ = -1;
    
    instancedSegments_ = -1;
    instancedIndexCount_ = 0;
    instancedMvpLoc_ = -1;
    instancedTextureLoc_ = -1;
    instancedUseLogDepthLoc_ = -1;
    instancedLogDepthFarLoc_ = -1;
}

bool TileRenderer::EnsureInstancedGrid(int segments) {
    if (instancedProgram_ == 0 || instancedVao_ == 0 || segments <= 0) {
        return false;
    }
    if (instancedSegments_ == segments && instancedIndexCount_ > 0) {
        return true;
    }

    std::vector<float> gridVertices;
    std::vector<unsigned int> gridIndices;
    gridVertices.reserve(static_cast<size_t>((segments + 1) * (segments + 1) * 2));
    gridIndices.reserve(static_cast<size_t>(segments * segments * 6));

    for (int iy = 0; iy <= segments; ++iy) {
        float v = static_cast<float>(iy) / static_cast<float>(segments);
        for (int ix = 0; ix <= segments; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(segments);
            gridVertices.push_back(u);
            gridVertices.push_back(1.0f - v);  // Match tile UV orientation.
        }
    }

    for (int iy = 0; iy < segments; ++iy) {
        for (int ix = 0; ix < segments; ++ix) {
            unsigned int tl = static_cast<unsigned int>(iy * (segments + 1) + ix);
            unsigned int tr = tl + 1;
            unsigned int bl = tl + static_cast<unsigned int>(segments + 1);
            unsigned int br = bl + 1;
            gridIndices.push_back(tl);
            gridIndices.push_back(bl);
            gridIndices.push_back(tr);
            gridIndices.push_back(tr);
            gridIndices.push_back(bl);
            gridIndices.push_back(br);
        }
    }

    glBindVertexArray(instancedVao_);

    glBindBuffer(GL_ARRAY_BUFFER, instancedGridVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(gridVertices.size() * sizeof(float)),
                 gridVertices.data(),
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, instancedGridEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(gridIndices.size() * sizeof(unsigned int)),
                 gridIndices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, instancedInstanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(12 * sizeof(float));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);

    instancedSegments_ = segments;
    instancedIndexCount_ = static_cast<uint32_t>(gridIndices.size());
    return true;
}

void TileRenderer::BeginBatch(const glm::mat4& mvp, bool wireframe,
                              bool useLogDepth, float logDepthFarKm, bool useRte,
                              bool useTextureArray) {
    stats_ = RenderStats{};
    batchActive_ = true;
    wireframeMode_ = wireframe;
    currentMvp_ = mvp;
    useLogDepthBatch_ = useLogDepth;
    logDepthFarBatch_ = std::max(1.0f, logDepthFarKm);
    useRteBatch_ = useRte;
    useTextureArrayBatch_ = useTextureArray;
    
    // Setup shader
    // Faz 2B: Use texture array shader variant if enabled
    if (useTextureArray) {
        shaderManager_.UseTileShader(ShaderFlags::UseTextureArray);
    } else {
        shaderManager_.UseTileShader();
    }
    
    glUniformMatrix4fv(shaderManager_.GetMvpLocation(), 1, GL_FALSE, glm::value_ptr(mvp));
    
    // Faz 2B: Texture array uses different sampler
    if (useTextureArray) {
        glUniform1i(shaderManager_.GetTextureArrayLocation(), 0);
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);  // Default to array mode
        glUniform1i(shaderManager_.GetPhotoTileTextureUnpopArrayLocation(), 2);
        glUniform1i(shaderManager_.GetUnpopUsesArrayLocation(), 0);
        glUniform1i(shaderManager_.GetUnpopTextureLayerLocation(), 0);
        // uTextureLayer will be set per-tile in RenderTile
    } else {
        glUniform1i(shaderManager_.GetTextureLocation(), 0);
    }
    
    glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
    glUniform1i(shaderManager_.GetTextureUnpopLocation(), 2);  // Unpop texture on unit 2
    glUniform1f(shaderManager_.GetUnpopBlendLocation(), 1.0f);
    glUniform4f(shaderManager_.GetTexScaleOffsetMainLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    glUniform4f(shaderManager_.GetTexScaleOffsetUnpopLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    glUniform1i(shaderManager_.GetRasterCrossfadeLocation(), 0);
    glUniform4f(shaderManager_.GetCornerLodsLocation(), 0.0f, 0.0f, 0.0f, 0.0f);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), 1.0f);
    glUniform1i(shaderManager_.GetUseLogDepthLocation(), useLogDepthBatch_ ? 1 : 0);
    glUniform1f(shaderManager_.GetLogDepthFarLocation(), logDepthFarBatch_);
    
    // Default: no heightmap (terrain displacement disabled)
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
    glUniform1i(shaderManager_.GetHeightmapLocation(), 1);  // Heightmap on texture unit 1
    glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    
    // Wireframe mode
    if (wireframeMode_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
}

void TileRenderer::RenderTile(const Tile& tile, float terrainMorph) {
    if (!batchActive_) return;
    // Renderable = hasMesh && textureId != 0 (not IsReady!)
    if (!tile.hasMesh || tile.textureId == 0 || tile.vao == 0) return;
    
    // Faz 2B: Bind texture (array or 2D)
    glActiveTexture(GL_TEXTURE0);
    const bool useArrayTexture =
        useTextureArrayBatch_ && tile.usesTextureArray && tile.textureArrayLayer >= 0;
    if (useArrayTexture) {
        // Texture array path - bind array and set layer
        glBindTexture(GL_TEXTURE_2D_ARRAY, tile.textureId);
        glUniform1i(shaderManager_.GetTextureLayerLocation(), tile.textureArrayLayer);
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    } else {
        // Legacy 2D texture path
        glBindTexture(GL_TEXTURE_2D, tile.textureId);
        if (useTextureArrayBatch_) {
            glUniform1i(shaderManager_.GetUseTexture2DLocation(), 1);
        }
    }
    
    ApplyPerTileUniforms(shaderManager_, tile, tile.texScaleOffset, useRteBatch_);
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    
    // Draw tile mesh
    glBindVertexArray(tile.vao);
    DrawCallBreakdown breakdown = DrawTileGeometry(tile);
    glBindVertexArray(0);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), 1.0f);
    
    // Faz 2B: Reset to array mode for subsequent tiles (if in array batch)
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    }
    
    // Update stats
    stats_.tilesRendered++;
    stats_.drawCalls += breakdown.drawCalls;
    stats_.trianglesRendered += breakdown.triangles;
}

void TileRenderer::RenderTileWithTexture(const Tile& tile, uint32_t textureId, float terrainMorph) {
    if (!batchActive_) return;
    if (!tile.hasMesh || tile.vao == 0 || textureId == 0) return;
    
    // Faz 2B: Placeholder uses GL_TEXTURE_2D even in array mode.
    // Tell shader to use 2D sampler path for this draw.
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 1);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    ApplyPerTileUniforms(shaderManager_, tile, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f), useRteBatch_);
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    
    // Draw tile mesh
    glBindVertexArray(tile.vao);
    DrawCallBreakdown breakdown = DrawTileGeometry(tile);
    glBindVertexArray(0);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), 1.0f);
    
    // Faz 2B: Reset to array mode after 2D placeholder draw
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    }
    
    // Update stats
    stats_.tilesRendered++;
    stats_.drawCalls += breakdown.drawCalls;
    stats_.trianglesRendered += breakdown.triangles;
}

void TileRenderer::RenderTileWithHeightmap(const Tile& tile, uint32_t heightmapId,
                                            float heightMin, float heightMax,
                                            const glm::vec4& heightmapUvTransform,
                                            float terrainMorph) {
    if (!batchActive_) return;
    if (!tile.hasMesh || tile.textureId == 0 || tile.vao == 0) return;
    
    // Faz 2B: Ensure array mode for this draw (in case previous was 2D placeholder)
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    }
    
    // Faz 2B: Bind color texture (array or 2D)
    const bool useArrayTexture =
        useTextureArrayBatch_ && tile.usesTextureArray && tile.textureArrayLayer >= 0;
    glActiveTexture(GL_TEXTURE0);
    if (useArrayTexture) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, tile.textureId);
        glUniform1i(shaderManager_.GetTextureLayerLocation(), tile.textureArrayLayer);
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    } else {
        glBindTexture(GL_TEXTURE_2D, tile.textureId);
        if (useTextureArrayBatch_) {
            glUniform1i(shaderManager_.GetUseTexture2DLocation(), 1);
        }
    }
    ApplyPerTileUniforms(shaderManager_, tile, tile.texScaleOffset, useRteBatch_);
    
    // Bind heightmap texture on unit 1 and enable displacement
    if (heightmapId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, heightmapId);
        glUniform1i(shaderManager_.GetHasHeightmapLocation(), 1);
        glUniform1f(shaderManager_.GetHeightMinLocation(), heightMin);
        glUniform1f(shaderManager_.GetHeightMaxLocation(), heightMax);
        glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(),
                    heightmapUvTransform.x,
                    heightmapUvTransform.y,
                    heightmapUvTransform.z,
                    heightmapUvTransform.w);
        glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    } else {
        glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
        glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
        glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    }
    
    // Draw tile mesh
    glBindVertexArray(tile.vao);
    DrawCallBreakdown breakdown = DrawTileGeometry(tile);
    glBindVertexArray(0);
    
    // Reset heightmap state for next tile
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
    glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), 1.0f);
    glActiveTexture(GL_TEXTURE0);  // Reset to default texture unit
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    }
    
    // Update stats
    stats_.tilesRendered++;
    stats_.drawCalls += breakdown.drawCalls;
    stats_.trianglesRendered += breakdown.triangles;
}

void TileRenderer::RenderTileWithCrossfade(const Tile& tile,
                                           uint32_t unpopTextureId,
                                           int unpopTextureLayer,
                                           TextureTarget unpopTarget,
                                           const glm::vec4& texScaleOffsetUnpop,
                                           float unpopBlend,
                                           uint32_t heightmapId,
                                           float heightMin,
                                           float heightMax,
                                           const glm::vec4& heightmapUvTransform,
                                           float terrainMorph) {
    if (!batchActive_) return;
    if (!useTextureArrayBatch_) {
        unpopTarget = TextureTarget::k2D;
    }
    if (useTextureArrayBatch_ &&
        unpopTarget == TextureTarget::kArray &&
        unpopTextureLayer < 0) {
        assert(false && "RenderTileWithCrossfade received array target without valid array layer");
        ++gUnpopArrayTo2DFallbacks;
        unpopTarget = TextureTarget::k2D;
    }
    if (!tile.hasMesh || tile.textureId == 0 || tile.vao == 0) {
        if (useTextureArrayBatch_) {
            ResetCrossfadeState(shaderManager_, useTextureArrayBatch_);
        }
        return;
    }
    if (unpopTextureId == 0) {
        ResetCrossfadeState(shaderManager_, useTextureArrayBatch_);
        if (heightmapId != 0) {
            RenderTileWithHeightmap(tile, heightmapId, heightMin, heightMax, heightmapUvTransform, terrainMorph);
        } else {
            RenderTile(tile, terrainMorph);
        }
        return;
    }

    // Safety: if caller marks ancestor as array-backed but omits valid layer index,
    // avoid binding array id to GL_TEXTURE_2D sampler (undefined behavior).
    // Degrade gracefully to non-crossfade tile render in this edge case.
    if (useTextureArrayBatch_ &&
        unpopTarget == TextureTarget::kArray &&
        unpopTextureLayer < 0) {
        ResetCrossfadeState(shaderManager_, useTextureArrayBatch_);
        if (heightmapId != 0) {
            RenderTileWithHeightmap(tile, heightmapId, heightMin, heightMax, heightmapUvTransform, terrainMorph);
        } else {
            RenderTile(tile, terrainMorph);
        }
        return;
    }

    assert(unpopTextureId != 0);

    // Faz 2B: Ensure array mode for this draw (in case previous was 2D placeholder)
    if (useTextureArrayBatch_) {
        glUniform1i(shaderManager_.GetUseTexture2DLocation(), 0);
    }

    // Faz 2B: Bind child texture (array or 2D)
    glActiveTexture(GL_TEXTURE0);
    const bool useMainTextureArray =
        useTextureArrayBatch_ && tile.usesTextureArray && tile.textureArrayLayer >= 0;
    if (useMainTextureArray) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, tile.textureId);
        glUniform1i(shaderManager_.GetTextureLayerLocation(), tile.textureArrayLayer);
    } else {
        if (useTextureArrayBatch_) {
            glUniform1i(shaderManager_.GetUseTexture2DLocation(), 1);
        }
        glBindTexture(GL_TEXTURE_2D, tile.textureId);
    }
    ApplyPerTileUniforms(shaderManager_, tile, tile.texScaleOffset, useRteBatch_);

    // Bind unpop (ancestor) texture on unit 2 (array or 2D)
    glActiveTexture(GL_TEXTURE2);
    const bool useUnpopArray = useTextureArrayBatch_ &&
                               unpopTarget == TextureTarget::kArray &&
                               unpopTextureLayer >= 0 &&
                               unpopTextureId != 0;
    if (useUnpopArray) {
        glUniform1i(shaderManager_.GetUnpopUsesArrayLocation(), 1);
        glUniform1i(shaderManager_.GetUnpopTextureLayerLocation(), unpopTextureLayer);
        glBindTexture(GL_TEXTURE_2D_ARRAY, unpopTextureId);
    } else {
        glUniform1i(shaderManager_.GetUnpopUsesArrayLocation(), 0);
        glUniform1i(shaderManager_.GetUnpopTextureLayerLocation(), 0);
        glBindTexture(GL_TEXTURE_2D, unpopTextureId);
    }

    glUniform1i(shaderManager_.GetRasterCrossfadeLocation(), 1);
    glUniform1f(shaderManager_.GetUnpopBlendLocation(), std::clamp(unpopBlend, 0.0f, 1.0f));
    glUniform4f(shaderManager_.GetTexScaleOffsetUnpopLocation(),
                texScaleOffsetUnpop.x,
                texScaleOffsetUnpop.y,
                texScaleOffsetUnpop.z,
                texScaleOffsetUnpop.w);

    // Optional heightmap displacement for child geometry
    if (heightmapId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, heightmapId);
        glUniform1i(shaderManager_.GetHasHeightmapLocation(), 1);
        glUniform1f(shaderManager_.GetHeightMinLocation(), heightMin);
        glUniform1f(shaderManager_.GetHeightMaxLocation(), heightMax);
        glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(),
                    heightmapUvTransform.x,
                    heightmapUvTransform.y,
                    heightmapUvTransform.z,
                    heightmapUvTransform.w);
        glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    } else {
        glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
        glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
        glUniform1f(shaderManager_.GetTerrainMorphLocation(), std::clamp(terrainMorph, 0.0f, 1.0f));
    }

    glBindVertexArray(tile.vao);
    DrawCallBreakdown breakdown = DrawTileGeometry(tile);
    glBindVertexArray(0);

    // Restore defaults for subsequent single-texture draws
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 0);
    glUniform4f(shaderManager_.GetHeightmapUvTransformLocation(), 1.0f, 1.0f, 0.0f, 0.0f);
    glUniform1f(shaderManager_.GetTerrainMorphLocation(), 1.0f);
    ResetCrossfadeState(shaderManager_, useTextureArrayBatch_);
    glActiveTexture(GL_TEXTURE0);

    stats_.tilesRendered++;
    stats_.drawCalls += breakdown.drawCalls;
    stats_.trianglesRendered += breakdown.triangles;
}

void TileRenderer::RenderFlatTilesInstanced(uint32_t textureId,
                                            int segments,
                                            const std::vector<FlatTileInstance>& instances) {
    if (!batchActive_ || instances.empty() || textureId == 0) {
        return;
    }
    // Faz 2B: When texture array batch is active, use the array instanced path
    if (useTextureArrayBatch_ && instancedArrayProgram_ != 0) {
        RenderFlatTilesInstancedArray(textureId, segments, instances);
        return;
    }
    if (!EnsureInstancedGrid(segments) || instancedProgram_ == 0 || instancedIndexCount_ == 0) {
        return;
    }

    // extent(4) + texScaleOffset(4) + data(4: fade + padding)
    instancedInstanceData_.clear();
    instancedInstanceData_.reserve(instances.size() * 12);

    int renderedTiles = 0;
    for (const FlatTileInstance& instance : instances) {
        if (instance.tile == nullptr || !instance.tile->hasMesh || instance.tile->textureId == 0) {
            continue;
        }
        Extent extent = instance.tile->extent;
        if (extent.Width() == 0.0 || extent.Height() == 0.0) {
            extent = Extent::FromTileWGS84(instance.tile->key.x, instance.tile->key.y, instance.tile->key.level);
        }
        instancedInstanceData_.push_back(static_cast<float>(extent.West()));
        instancedInstanceData_.push_back(static_cast<float>(extent.East()));
        instancedInstanceData_.push_back(static_cast<float>(extent.South()));
        instancedInstanceData_.push_back(static_cast<float>(extent.North()));
        instancedInstanceData_.push_back(instance.tile->texScaleOffset.x);
        instancedInstanceData_.push_back(instance.tile->texScaleOffset.y);
        instancedInstanceData_.push_back(instance.tile->texScaleOffset.z);
        instancedInstanceData_.push_back(instance.tile->texScaleOffset.w);
        instancedInstanceData_.push_back(std::clamp(instance.fade, 0.0f, 1.0f));
        instancedInstanceData_.push_back(0.0f);
        instancedInstanceData_.push_back(0.0f);
        instancedInstanceData_.push_back(0.0f);
        ++renderedTiles;
    }

    if (renderedTiles == 0) {
        return;
    }

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevActiveTex = 0;
    GLint prevTex2D = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);

    glUseProgram(instancedProgram_);
    glUniformMatrix4fv(instancedMvpLoc_, 1, GL_FALSE, glm::value_ptr(currentMvp_));
    glUniform1i(instancedTextureLoc_, 0);
    glUniform1i(instancedUseLogDepthLoc_, useLogDepthBatch_ ? 1 : 0);
    glUniform1f(instancedLogDepthFarLoc_, logDepthFarBatch_);

    glBindTexture(GL_TEXTURE_2D, textureId);
    glBindVertexArray(instancedVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instancedInstanceVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(instancedInstanceData_.size() * sizeof(float)),
                 instancedInstanceData_.data(),
                 GL_DYNAMIC_DRAW);
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(instancedIndexCount_),
                            GL_UNSIGNED_INT,
                            nullptr,
                            renderedTiles);
    glBindVertexArray(static_cast<GLuint>(prevVao));

    glUseProgram(static_cast<GLuint>(prevProgram));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex2D));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));

    stats_.tilesRendered += renderedTiles;
    stats_.drawCalls += 1;
    stats_.trianglesRendered += static_cast<int>((instancedIndexCount_ / 3) * renderedTiles);
    stats_.instancedBatches += 1;
    stats_.instancedTiles += renderedTiles;
}

// Faz 2B: Texture array instanced rendering
void TileRenderer::RenderFlatTilesInstancedArray(uint32_t textureArrayId,
                                                 int segments,
                                                 const std::vector<FlatTileInstance>& instances) {
    if (!batchActive_ || instances.empty() || textureArrayId == 0) {
        return;
    }
    if (!EnsureInstancedGrid(segments) || instancedArrayProgram_ == 0 || instancedIndexCount_ == 0) {
        return;
    }

    // extent(4) + texScaleOffset(4) + data(4: fade + layer index)
    instancedArrayInstanceData_.clear();
    instancedArrayInstanceData_.reserve(instances.size() * 12);

    int renderedTiles = 0;
    for (const FlatTileInstance& instance : instances) {
        if (instance.tile == nullptr || !instance.tile->hasMesh || instance.tile->textureId == 0) {
            continue;
        }
        // Only array-backed tiles can be rendered with this path
        if (!instance.tile->usesTextureArray || instance.tile->textureArrayLayer < 0) {
            continue;
        }
        Extent extent = instance.tile->extent;
        if (extent.Width() == 0.0 || extent.Height() == 0.0) {
            extent = Extent::FromTileWGS84(instance.tile->key.x, instance.tile->key.y, instance.tile->key.level);
        }
        instancedArrayInstanceData_.push_back(static_cast<float>(extent.West()));
        instancedArrayInstanceData_.push_back(static_cast<float>(extent.East()));
        instancedArrayInstanceData_.push_back(static_cast<float>(extent.South()));
        instancedArrayInstanceData_.push_back(static_cast<float>(extent.North()));
        instancedArrayInstanceData_.push_back(instance.tile->texScaleOffset.x);
        instancedArrayInstanceData_.push_back(instance.tile->texScaleOffset.y);
        instancedArrayInstanceData_.push_back(instance.tile->texScaleOffset.z);
        instancedArrayInstanceData_.push_back(instance.tile->texScaleOffset.w);
        instancedArrayInstanceData_.push_back(std::clamp(instance.fade, 0.0f, 1.0f));
        instancedArrayInstanceData_.push_back(static_cast<float>(instance.tile->textureArrayLayer));  // Layer index
        instancedArrayInstanceData_.push_back(0.0f);
        instancedArrayInstanceData_.push_back(0.0f);
        ++renderedTiles;
    }

    if (renderedTiles == 0) {
        return;
    }

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevActiveTex = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    glUseProgram(instancedArrayProgram_);
    glUniformMatrix4fv(instancedArrayMvpLoc_, 1, GL_FALSE, glm::value_ptr(currentMvp_));
    glUniform1i(instancedArrayTextureLoc_, 0);
    glUniform1i(instancedArrayUseLogDepthLoc_, useLogDepthBatch_ ? 1 : 0);
    glUniform1f(instancedArrayLogDepthFarLoc_, logDepthFarBatch_);

    // Bind texture array (shared tier texture)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArrayId);
    
    // Use shared grid VAO/VBO from standard instanced path
    glBindVertexArray(instancedVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instancedArrayInstanceVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(instancedArrayInstanceData_.size() * sizeof(float)),
                 instancedArrayInstanceData_.data(),
                 GL_DYNAMIC_DRAW);
    
    // Re-setup instance attributes for array shader
    // extent(4) + texScaleOffset(4) + data(4) = 12 floats per instance
    constexpr GLsizei stride = static_cast<GLsizei>(12 * sizeof(float));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribDivisor(1, 1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);
    
    glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(instancedIndexCount_),
                            GL_UNSIGNED_INT,
                            nullptr,
                            renderedTiles);
    glBindVertexArray(static_cast<GLuint>(prevVao));

    glUseProgram(static_cast<GLuint>(prevProgram));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));

    stats_.tilesRendered += renderedTiles;
    stats_.drawCalls += 1;
    stats_.trianglesRendered += static_cast<int>((instancedIndexCount_ / 3) * renderedTiles);
    stats_.instancedBatches += 1;
    stats_.instancedTiles += renderedTiles;
}

void TileRenderer::EndBatch() {
    if (!batchActive_) return;
    
    // Reset polygon mode
    if (wireframeMode_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    glDisable(GL_POLYGON_OFFSET_FILL);
    batchActive_ = false;
}

void TileRenderer::InitPivotGeometry() {
    // Create a simple crosshair/target for pivot visualization
    // Google Earth style: circle with crosshairs
    const int circleSegments = 32;
    const float outerRadius = 0.02f;  // Screen-relative size
    const float innerRadius = 0.008f;
    
    std::vector<float> vertices;
    
    // Outer circle
    for (int i = 0; i <= circleSegments; ++i) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / circleSegments;
        vertices.push_back(std::cos(angle) * outerRadius);
        vertices.push_back(std::sin(angle) * outerRadius);
        vertices.push_back(0.0f);
    }
    
    // Crosshair lines
    // Horizontal
    vertices.push_back(-outerRadius * 1.5f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(-innerRadius); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(innerRadius); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(outerRadius * 1.5f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    
    // Vertical
    vertices.push_back(0.0f); vertices.push_back(-outerRadius * 1.5f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(-innerRadius); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(innerRadius); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(outerRadius * 1.5f); vertices.push_back(0.0f);
    
    pivotVertexCount_ = static_cast<int>(vertices.size() / 3);
    
    glGenVertexArrays(1, &pivotVao_);
    glGenBuffers(1, &pivotVbo_);
    
    glBindVertexArray(pivotVao_);
    glBindBuffer(GL_ARRAY_BUFFER, pivotVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
}

void TileRenderer::DestroyPivotGeometry() {
    if (pivotVao_ != 0) {
        glDeleteVertexArrays(1, &pivotVao_);
        pivotVao_ = 0;
    }
    if (pivotVbo_ != 0) {
        glDeleteBuffers(1, &pivotVbo_);
        pivotVbo_ = 0;
    }
}

void TileRenderer::RenderPivot(const glm::mat4& mvp, const glm::dvec3& pivotPoint, bool visible) {
    if (!visible || pivotVao_ == 0) return;
    
    // Transform pivot to screen space and render as overlay
    // This is a simplified version - full implementation would use screen-space rendering
    glm::vec4 clipPos = mvp * glm::vec4(glm::vec3(pivotPoint), 1.0f);
    if (clipPos.w <= 0.0f) return;  // Behind camera
    
    // Could render at screen position - for now just indicate pivot exists
    // Full implementation would use separate screen-space shader
}

} // namespace globe
