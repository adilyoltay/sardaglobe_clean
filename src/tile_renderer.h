#pragma once

#include "tile.h"
#include "tile_mesh_builder.h"
#include "tile_texture_manager.h"
#include "tile_lod_selector.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>

namespace earth {

// Shader uniform locations cache
struct TileShaderUniforms {
    GLint mvp = -1;
    GLint model = -1;
    GLint texture0 = -1;
    GLint alpha = -1;
    GLint uvOffset = -1;
    GLint uvScale = -1;
    GLint lightDir = -1;
    GLint ambient = -1;
    GLint diffuse = -1;
    GLint globeCenter = -1;
    GLint cameraPos = -1;
};

// Render batch for instanced drawing
struct TileRenderBatch {
    GLuint vao = 0;
    GLuint texture = 0;
    GLsizei indexCount = 0;
    glm::mat4 model;
    glm::vec2 uvOffset;
    glm::vec2 uvScale;
    float alpha = 1.0f;
};

// High-performance tile renderer
// - Minimal state changes
// - Batch similar tiles
// - Front-to-back sorting for early-Z
class TileRenderer {
public:
    struct Config {
        bool enableLighting = true;
        bool enableFog = false;
        bool wireframe = false;
        bool showTileBorders = false;
        float ambientLight = 0.3f;
        float diffuseLight = 0.7f;
        glm::vec3 lightDirection = glm::normalize(glm::vec3(1.0f, 0.5f, 0.3f));
    };
    
    struct Stats {
        int tilesRendered = 0;
        int drawCalls = 0;
        int triangles = 0;
        int textureBinds = 0;
        int shaderSwitches = 0;
    };
    
    TileRenderer();
    ~TileRenderer();
    
    // Initialize shaders
    bool Initialize();
    void Shutdown();
    
    // Main render function
    void Render(
        const std::vector<Tile*>& tiles,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        GLuint fallbackTexture = 0
    );
    
    // Render poles
    void RenderPoles(
        const PoleMesh& northPole,
        const PoleMesh& southPole,
        const glm::mat4& mvp,
        GLuint poleTexture
    );
    
    // Configuration
    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }
    
    // Stats
    const Stats& GetStats() const { return stats_; }
    void ResetStats();
    
    // Shader access (for custom rendering)
    GLuint GetShaderProgram() const { return shaderProgram_; }
    const TileShaderUniforms& GetUniforms() const { return uniforms_; }
    
private:
    Config config_;
    Stats stats_;
    
    GLuint shaderProgram_ = 0;
    TileShaderUniforms uniforms_;
    
    // GL state cache for minimal state changes
    struct GLStateCache {
        GLuint boundProgram = 0;
        GLuint boundVao = 0;
        GLuint boundTexture = 0;
        bool depthTest = true;
        bool blend = false;
        bool cullFace = true;
    };
    GLStateCache stateCache_;
    
    // Helpers
    bool CompileShader(GLuint shader, const char* source);
    bool LinkProgram(GLuint program);
    void CacheUniformLocations();
    
    void SetProgram(GLuint program);
    void SetVAO(GLuint vao);
    void SetTexture(GLuint texture, int unit = 0);
    void SetDepthTest(bool enabled);
    void SetBlend(bool enabled);
    void SetCullFace(bool enabled);
    
    void SortTilesFrontToBack(std::vector<Tile*>& tiles, const glm::vec3& cameraPos);
};

// Vertex shader source
extern const char* kTileVertexShader;

// Fragment shader source
extern const char* kTileFragmentShader;

} // namespace earth
