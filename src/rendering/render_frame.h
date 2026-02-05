#pragma once

#include "../core/tile.h"
#include "tile_renderer.h"
#include "shader_manager.h"
#include "heightmap_manager.h"
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
        int renderableLeaves = 0;   // Leaf tiles rendered normally
        int fallbackTiles = 0;      // Parent fallback tiles rendered
        int placeholderTiles = 0;   // Last-resort placeholder tiles
        int leafNoMesh = 0;         // Leaves without mesh
        int leafNoTexture = 0;      // Leaves with mesh but no texture
        int missing = 0;            // True gaps (no ancestor, no mesh)
    };
    
    RenderFrame(TileRenderer& tileRenderer, ShaderManager& shaderManager);
    
    // Render only tiles (no clear / no pivot / no UI)
    // GE-style: parent fallback + placeholder last-resort
    TileDrawStats DrawTiles(
        const std::unordered_set<TileKey>& leafSet,
        std::unordered_map<TileKey, Tile>& tiles,
        const glm::mat4& mvp,
        double currentTime,
        bool wireframe,
        uint32_t loadingTexture,  // Placeholder texture ID
        HeightmapManager* heightmapManager = nullptr  // Optional: GPU terrain displacement
    );

private:
    // Find nearest renderable ancestor (parent → grandparent → base)
    // Renderable = hasMesh && textureId != 0
    Tile* FindRenderableAncestor(const TileKey& key, std::unordered_map<TileKey, Tile>& tiles);

    TileRenderer& tileRenderer_;
    ShaderManager& shaderManager_;
};

} // namespace globe
