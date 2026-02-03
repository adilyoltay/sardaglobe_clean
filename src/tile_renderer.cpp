#include "tile_renderer.h"
#include <algorithm>
#include <cstdio>

namespace earth {

// ============================================================================
// SHADER SOURCES
// ============================================================================

const char* kTileVertexShader = R"(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec2 uUvOffset;
uniform vec2 uUvScale;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = uUvOffset + aTexCoord * uUvScale;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kTileFragmentShader = R"(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform float uAlpha;
uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform vec3 uCameraPos;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    
    // Simple lighting
    vec3 normal = normalize(vNormal);
    float nDotL = max(dot(normal, uLightDir), 0.0);
    float lighting = uAmbient + uDiffuse * nDotL;
    
    vec3 color = texColor.rgb * lighting;
    FragColor = vec4(color, texColor.a * uAlpha);
}
)";

// ============================================================================
// TILE RENDERER
// ============================================================================

TileRenderer::TileRenderer() : config_() {}

TileRenderer::~TileRenderer() {
    Shutdown();
}

bool TileRenderer::Initialize() {
    // Create vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    if (!CompileShader(vs, kTileVertexShader)) {
        glDeleteShader(vs);
        return false;
    }
    
    // Create fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!CompileShader(fs, kTileFragmentShader)) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }
    
    // Create program
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vs);
    glAttachShader(shaderProgram_, fs);
    
    if (!LinkProgram(shaderProgram_)) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
        return false;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    CacheUniformLocations();
    
    return true;
}

void TileRenderer::Shutdown() {
    if (shaderProgram_) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
}

bool TileRenderer::CompileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        fprintf(stderr, "[TileRenderer] Shader compile error: %s\n", log);
        return false;
    }
    
    return true;
}

bool TileRenderer::LinkProgram(GLuint program) {
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        fprintf(stderr, "[TileRenderer] Program link error: %s\n", log);
        return false;
    }
    
    return true;
}

void TileRenderer::CacheUniformLocations() {
    uniforms_.mvp = glGetUniformLocation(shaderProgram_, "uMVP");
    uniforms_.model = glGetUniformLocation(shaderProgram_, "uModel");
    uniforms_.texture0 = glGetUniformLocation(shaderProgram_, "uTexture");
    uniforms_.alpha = glGetUniformLocation(shaderProgram_, "uAlpha");
    uniforms_.uvOffset = glGetUniformLocation(shaderProgram_, "uUvOffset");
    uniforms_.uvScale = glGetUniformLocation(shaderProgram_, "uUvScale");
    uniforms_.lightDir = glGetUniformLocation(shaderProgram_, "uLightDir");
    uniforms_.ambient = glGetUniformLocation(shaderProgram_, "uAmbient");
    uniforms_.diffuse = glGetUniformLocation(shaderProgram_, "uDiffuse");
    uniforms_.cameraPos = glGetUniformLocation(shaderProgram_, "uCameraPos");
}

void TileRenderer::SetProgram(GLuint program) {
    if (stateCache_.boundProgram != program) {
        glUseProgram(program);
        stateCache_.boundProgram = program;
        stats_.shaderSwitches++;
    }
}

void TileRenderer::SetVAO(GLuint vao) {
    if (stateCache_.boundVao != vao) {
        glBindVertexArray(vao);
        stateCache_.boundVao = vao;
    }
}

void TileRenderer::SetTexture(GLuint texture, int unit) {
    if (stateCache_.boundTexture != texture) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        stateCache_.boundTexture = texture;
        stats_.textureBinds++;
    }
}

void TileRenderer::SetDepthTest(bool enabled) {
    if (stateCache_.depthTest != enabled) {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        stateCache_.depthTest = enabled;
    }
}

void TileRenderer::SetBlend(bool enabled) {
    if (stateCache_.blend != enabled) {
        if (enabled) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        stateCache_.blend = enabled;
    }
}

void TileRenderer::SetCullFace(bool enabled) {
    if (stateCache_.cullFace != enabled) {
        if (enabled) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        stateCache_.cullFace = enabled;
    }
}

void TileRenderer::SortTilesFrontToBack(std::vector<Tile*>& tiles, const glm::vec3& cameraPos) {
    std::sort(tiles.begin(), tiles.end(), [&cameraPos](const Tile* a, const Tile* b) {
        float distA = glm::length(a->center - cameraPos);
        float distB = glm::length(b->center - cameraPos);
        return distA < distB; // Front to back
    });
}

void TileRenderer::Render(
    const std::vector<Tile*>& tiles,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPos,
    GLuint fallbackTexture
) {
    if (tiles.empty() || !shaderProgram_) return;
    
    // Sort front-to-back for early-Z rejection
    std::vector<Tile*> sortedTiles = tiles;
    SortTilesFrontToBack(sortedTiles, cameraPos);
    
    // Setup render state
    SetProgram(shaderProgram_);
    SetDepthTest(true);
    SetCullFace(true);
    SetBlend(false);
    
    if (config_.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    
    // Set constant uniforms
    glm::mat4 vp = projection * view;
    glm::mat4 model = glm::mat4(1.0f);
    
    if (uniforms_.model >= 0) {
        glUniformMatrix4fv(uniforms_.model, 1, GL_FALSE, &model[0][0]);
    }
    if (uniforms_.lightDir >= 0) {
        glUniform3fv(uniforms_.lightDir, 1, &config_.lightDirection[0]);
    }
    if (uniforms_.ambient >= 0) {
        glUniform1f(uniforms_.ambient, config_.ambientLight);
    }
    if (uniforms_.diffuse >= 0) {
        glUniform1f(uniforms_.diffuse, config_.diffuseLight);
    }
    if (uniforms_.cameraPos >= 0) {
        glUniform3fv(uniforms_.cameraPos, 1, &cameraPos[0]);
    }
    if (uniforms_.texture0 >= 0) {
        glUniform1i(uniforms_.texture0, 0);
    }
    
    // Render tiles
    for (Tile* tile : sortedTiles) {
        if (!tile || tile->mesh.vao == 0) continue;
        
        // MVP
        glm::mat4 mvp = vp; // Tiles are already in world space
        if (uniforms_.mvp >= 0) {
            glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, &mvp[0][0]);
        }
        
        // UV transform
        if (uniforms_.uvOffset >= 0) {
            glUniform2fv(uniforms_.uvOffset, 1, &tile->uvOffset[0]);
        }
        if (uniforms_.uvScale >= 0) {
            glUniform2fv(uniforms_.uvScale, 1, &tile->uvScale[0]);
        }
        
        // Alpha (for fade transitions)
        if (uniforms_.alpha >= 0) {
            glUniform1f(uniforms_.alpha, tile->fade);
        }
        
        // Texture
        GLuint texture = tile->texture;
        if (texture == 0 && fallbackTexture != 0) {
            texture = fallbackTexture;
        }
        SetTexture(texture);
        
        // Draw
        SetVAO(tile->mesh.vao);
        glDrawElements(GL_TRIANGLES, tile->mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        
        stats_.tilesRendered++;
        stats_.drawCalls++;
        stats_.triangles += tile->mesh.indexCount / 3;
    }
    
    if (config_.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    // Reset state
    SetVAO(0);
}

void TileRenderer::RenderPoles(
    const PoleMesh& northPole,
    const PoleMesh& southPole,
    const glm::mat4& mvp,
    GLuint poleTexture
) {
    if (!shaderProgram_) return;
    
    SetProgram(shaderProgram_);
    SetDepthTest(true);
    SetCullFace(false); // Poles need both faces
    
    if (uniforms_.mvp >= 0) {
        glUniformMatrix4fv(uniforms_.mvp, 1, GL_FALSE, &mvp[0][0]);
    }
    if (uniforms_.alpha >= 0) {
        glUniform1f(uniforms_.alpha, 1.0f);
    }
    if (uniforms_.uvOffset >= 0) {
        glUniform2f(uniforms_.uvOffset, 0.0f, 0.0f);
    }
    if (uniforms_.uvScale >= 0) {
        glUniform2f(uniforms_.uvScale, 1.0f, 1.0f);
    }
    
    SetTexture(poleTexture);
    
    // North pole
    if (northPole.initialized && northPole.vao != 0) {
        SetVAO(northPole.vao);
        glDrawElements(GL_TRIANGLES, northPole.indexCount, GL_UNSIGNED_INT, nullptr);
        stats_.drawCalls++;
        stats_.triangles += northPole.indexCount / 3;
    }
    
    // South pole
    if (southPole.initialized && southPole.vao != 0) {
        SetVAO(southPole.vao);
        glDrawElements(GL_TRIANGLES, southPole.indexCount, GL_UNSIGNED_INT, nullptr);
        stats_.drawCalls++;
        stats_.triangles += southPole.indexCount / 3;
    }
    
    SetCullFace(true);
    SetVAO(0);
}

void TileRenderer::ResetStats() {
    stats_ = Stats();
}

} // namespace earth
