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
    struct RenderStats {
        int tilesRendered = 0;
        int drawCalls = 0;
        int trianglesRendered = 0;
    };
    
    explicit TileRenderer(ShaderManager& shaderManager);
    ~TileRenderer() = default;
    
    // Begin a render batch
    void BeginBatch(const glm::mat4& mvp, bool wireframe = false);
    
    // Render a single tile
    void RenderTile(const Tile& tile);
    
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
    ShaderManager& shaderManager_;
    
    // Batch state
    bool batchActive_ = false;
    bool wireframeMode_ = false;
    glm::mat4 currentMvp_;
    
    // Stats
    RenderStats stats_;
    
    // Pivot gizmo
    uint32_t pivotVao_ = 0;
    uint32_t pivotVbo_ = 0;
    int pivotVertexCount_ = 0;
};

} // namespace globe
