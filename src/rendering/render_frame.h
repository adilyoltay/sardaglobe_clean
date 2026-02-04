#pragma once

#include "../core/tile.h"
#include "tile_renderer.h"
#include "shader_manager.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>

namespace globe {

// RenderFrame - Minimal tile rendering separation (GE-style)
// Handles only tile drawing: collect, fade, sort, render
// Clear, pivot, ImGui remain in GlobeEngine
class RenderFrame {
public:
    struct TileDrawStats {
        int tilesRendered = 0;
        int tilesSkipped = 0;
    };
    
    RenderFrame(TileRenderer& tileRenderer, ShaderManager& shaderManager);
    
    // Render only tiles (no clear / no pivot / no UI)
    TileDrawStats DrawTiles(
        const std::unordered_set<TileKey>& leafSet,
        std::unordered_map<TileKey, Tile>& tiles,
        const glm::mat4& mvp,
        double currentTime,
        bool wireframe
    );

private:
    TileRenderer& tileRenderer_;
    ShaderManager& shaderManager_;
};

} // namespace globe
