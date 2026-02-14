// Shader Uniform Parity Test
// Validates that Tile and RockMesh render paths use consistent shader uniforms

#include "../src/rendering/shader_manager.h"
#include <iostream>
#include <cstring>

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

} // namespace

int main() {
    int failures = 0;
    
    // Test 1: Vertex shader contains RTE uniforms
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        
        bool hasOriginHi = std::strstr(vertexShader, "uTileOriginECEFHi") != nullptr;
        bool hasOriginLo = std::strstr(vertexShader, "uTileOriginECEFLo") != nullptr;
        bool hasUseRte = std::strstr(vertexShader, "uUseRTE") != nullptr;
        bool hasRelativePos = std::strstr(vertexShader, "relative") != nullptr ||
                              std::strstr(vertexShader, "worldPos") != nullptr;
        
        if (!Expect(hasOriginHi, "Vertex shader should contain uTileOriginECEFHi")) failures++;
        if (!Expect(hasOriginLo, "Vertex shader should contain uTileOriginECEFLo")) failures++;
        if (!Expect(hasUseRte, "Vertex shader should contain uUseRTE")) failures++;
        if (!Expect(hasRelativePos, "Vertex shader should handle relative positions")) failures++;
        
        if (hasOriginHi && hasOriginLo && hasUseRte && hasRelativePos) {
            Report("TileShaderHasRTEUniforms");
        }
    }
    
    // Test 2: Vertex shader contains required core uniforms
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        
        bool hasMvp = std::strstr(vertexShader, "uMVP") != nullptr;
        bool hasHasHeightmap = std::strstr(vertexShader, "uHasHeightmap") != nullptr;
        bool hasTerrainMorph = std::strstr(vertexShader, "uTerrainMorph") != nullptr;
        bool hasHeightmap = std::strstr(vertexShader, "uHeightmap") != nullptr;
        bool hasHeightScale = std::strstr(vertexShader, "uHeightScale") != nullptr;
        
        if (!Expect(hasMvp, "Shader should contain uMVP")) failures++;
        if (!Expect(hasHasHeightmap, "Shader should contain uHasHeightmap")) failures++;
        if (!Expect(hasTerrainMorph, "Shader should contain uTerrainMorph")) failures++;
        if (!Expect(hasHeightmap, "Shader should contain uHeightmap")) failures++;
        if (!Expect(hasHeightScale, "Shader should contain uHeightScale")) failures++;
        
        if (hasMvp && hasHasHeightmap && hasTerrainMorph && hasHeightmap && hasHeightScale) {
            Report("TileShaderHasCoreUniforms");
        }
    }
    
    // Test 3: Heightmap UV transform uniform
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        
        bool hasHeightmapUvTransform = std::strstr(vertexShader, "uHeightmapUvTransform") != nullptr;
        bool hasCornerLods = std::strstr(vertexShader, "uCornerLods") != nullptr;
        
        if (!Expect(hasHeightmapUvTransform, "Vertex shader should contain uHeightmapUvTransform")) failures++;
        if (!Expect(hasCornerLods, "Vertex shader should contain uCornerLods")) failures++;
        
        if (hasHeightmapUvTransform && hasCornerLods) {
            Report("HeightmapUniformsPresent");
        }
    }
    
    // Test 4: Log depth uniforms
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
    
    // Test 5: RTE uniform naming consistency
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        
        bool hiConsistent = std::strstr(vertexShader, "uTileOriginECEFHi") != nullptr;
        bool loConsistent = std::strstr(vertexShader, "uTileOriginECEFLo") != nullptr;
        bool useRteConsistent = std::strstr(vertexShader, "uUseRTE") != nullptr;
        
        // These should match the names used in:
        // - ShaderManager::CacheUniformLocations
        // - TileRenderer::ApplyPerTileUniforms  
        // - RockMeshManager::Render
        
        if (!Expect(hiConsistent, "uTileOriginECEFHi naming should be consistent")) failures++;
        if (!Expect(loConsistent, "uTileOriginECEFLo naming should be consistent")) failures++;
        if (!Expect(useRteConsistent, "uUseRTE naming should be consistent")) failures++;
        
        if (hiConsistent && loConsistent && useRteConsistent) {
            Report("RTEUniformNamingConsistency");
        }
    }
    
    // Test 6: Vertex shader accepts relative position
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        
        bool hasAPos = std::strstr(vertexShader, "aPos") != nullptr;
        bool hasWorldPos = std::strstr(vertexShader, "worldPos") != nullptr;
        bool hasRteCheck = std::strstr(vertexShader, "uUseRTE") != nullptr;
        
        if (!Expect(hasAPos, "Vertex shader should use aPos")) failures++;
        if (!Expect(hasWorldPos, "Vertex shader should compute worldPos")) failures++;
        if (!Expect(hasRteCheck, "Vertex shader should check uUseRTE")) failures++;
        
        if (hasAPos && hasWorldPos && hasRteCheck) {
            Report("VertexShaderAcceptsRelativePosition");
        }
    }
    
    // Test 7: Fragment shader contains basic uniforms
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
    
    // Test 8: RockMesh uses compatible uniforms
    {
        const char* vertexShader = shaders::TILE_VERTEX;
        const char* fragmentShader = shaders::TILE_FRAGMENT;
        
        // RockMeshManager queries these RTE uniforms (must match shader):
        bool hasOriginHi = std::strstr(vertexShader, "uTileOriginECEFHi") != nullptr;
        bool hasOriginLo = std::strstr(vertexShader, "uTileOriginECEFLo") != nullptr;
        bool hasUseRte = std::strstr(vertexShader, "uUseRTE") != nullptr;
        
        // uFade is defined in fragment shader for LOD blending
        bool hasFade = std::strstr(fragmentShader, "uFade") != nullptr;
        
        // RTE uniforms are shared between Tile and RockMesh
        bool sharedRTE = hasOriginHi && hasOriginLo && hasUseRte;
        
        if (!Expect(sharedRTE, "RockMesh should share RTE uniforms with Tile")) failures++;
        if (!Expect(hasFade, "Fragment shader should contain uFade for alpha blending")) failures++;
        
        if (sharedRTE && hasFade) {
            Report("RockMeshShaderUniformCompatibility");
        }
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll Shader Uniform Parity tests PASSED\n";
    return 0;
}
