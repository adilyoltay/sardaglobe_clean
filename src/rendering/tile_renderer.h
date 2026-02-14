#pragma once

#include "../core/tile.h"
#include "shader_manager.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <unordered_map>

namespace globe {

// Dedicated tile renderer - separates rendering logic from engine
// Google Earth style: batch rendering with state management
class TileRenderer {
public:
    struct FlatTileInstance {
        const Tile* tile = nullptr;
        float fade = 1.0f;
    };

    struct RenderStats {
        int tilesRendered = 0;
        int drawCalls = 0;
        int trianglesRendered = 0;
        int instancedBatches = 0;
        int instancedTiles = 0;
    };
    
    explicit TileRenderer(ShaderManager& shaderManager);
    ~TileRenderer();
    
    // Begin a render batch
    void BeginBatch(const glm::mat4& mvp, bool wireframe = false,
                    bool useLogDepth = false, float logDepthFarKm = 1.0f,
                    bool useRte = true, bool useTextureArray = false);
    
    // Render a single tile (uses tile's own texture)
    void RenderTile(const Tile& tile, float terrainMorph = 1.0f);
    
    // Render a tile with a specific texture (for placeholder)
    void RenderTileWithTexture(const Tile& tile, uint32_t textureId, float terrainMorph = 1.0f);
    
    // Render a tile with heightmap for terrain displacement
    void RenderTileWithHeightmap(const Tile& tile, uint32_t heightmapId, 
                                  float heightMin, float heightMax,
                                  const glm::vec4& heightmapUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
                                  float terrainMorph = 1.0f);

    // Render child tile with shader-level parent/child crossfade (unpop)
    void RenderTileWithCrossfade(const Tile& tile,
                                 uint32_t unpopTextureId,
                                 int unpopTextureLayer,
                                 bool unpopUsesArray,
                                 const glm::vec4& texScaleOffsetUnpop,
                                 float unpopBlend,
                                 uint32_t heightmapId = 0,
                                 float heightMin = 0.0f,
                                 float heightMax = 0.0f,
                                 const glm::vec4& heightmapUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
                                 float terrainMorph = 1.0f);

    // Render flat (no-heightmap/no-crossfade) tiles as a single instanced draw.
    // All instances must share the same textureId and mesh segment count.
    void RenderFlatTilesInstanced(uint32_t textureId,
                                  int segments,
                                  const std::vector<FlatTileInstance>& instances);
    bool SupportsInstancedFlatPath() const { return instancedProgram_ != 0; }
    
    // End the batch and restore state
    void EndBatch();
    
    // Render pivot/target gizmo (Google Earth style)
    void RenderPivot(const glm::mat4& mvp, const glm::dvec3& pivotPoint, bool visible);
    
    // Get stats from last batch
    const RenderStats& GetStats() const { return stats_; }
    
    // Create pivot geometry (call once during init)
    void InitPivotGeometry();
    void DestroyPivotGeometry();

private:
    bool InitInstancedResources();
    void DestroyInstancedResources();
    bool EnsureInstancedGrid(int segments);
    uint32_t CompileShader(uint32_t type, const char* source);
    uint32_t LinkProgram(uint32_t vertexShader, uint32_t fragmentShader);

    ShaderManager& shaderManager_;
    
    // Batch state
    bool batchActive_ = false;
    bool wireframeMode_ = false;
    glm::mat4 currentMvp_;
    bool useLogDepthBatch_ = false;
    float logDepthFarBatch_ = 1.0f;
    bool useRteBatch_ = true;
    bool useTextureArrayBatch_ = false;  // Faz 2B: Use GL_TEXTURE_2D_ARRAY
    
    // Stats
    RenderStats stats_;
    
    // Pivot gizmo
    uint32_t pivotVao_ = 0;
    uint32_t pivotVbo_ = 0;
    int pivotVertexCount_ = 0;

    // Flat tile instancing batch path (P3.2 draw-call reduction).
    uint32_t instancedProgram_ = 0;
    uint32_t instancedVao_ = 0;
    uint32_t instancedGridVbo_ = 0;
    uint32_t instancedGridEbo_ = 0;
    uint32_t instancedInstanceVbo_ = 0;
    int instancedSegments_ = -1;
    uint32_t instancedIndexCount_ = 0;
    int instancedMvpLoc_ = -1;
    int instancedTextureLoc_ = -1;
    int instancedUseLogDepthLoc_ = -1;
    int instancedLogDepthFarLoc_ = -1;
    std::vector<float> instancedInstanceData_;
};

} // namespace globe
