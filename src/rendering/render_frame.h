#pragma once

#include "../core/tile.h"
#include "../io/dem_manager.h"
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
    // Explicit unpop texture target selector mirrors TileRenderer semantics.
    using TextureTarget = TileRenderer::TextureTarget;

    struct TileDrawStats {
        int renderableLeaves = 0;   // Leaf tiles rendered normally
        int crossfadingLeaves = 0;  // Leaves currently blending from parent
        int fallbackTiles = 0;      // Parent fallback tiles rendered
        int placeholderTiles = 0;   // Last-resort placeholder tiles
        int leafNoMesh = 0;         // Leaves without mesh
        int leafNoTexture = 0;      // Leaves with mesh but no texture
        int leafNoTerrain = 0;      // Leaves with mesh+texture but missing required terrain data
        int missing = 0;            // True gaps (no ancestor, no mesh)
    };
    
    RenderFrame(TileRenderer& tileRenderer, ShaderManager& shaderManager);
    
    // Render only tiles (no clear / no pivot / no UI)
    // GE-style: parent fallback + placeholder last-resort
    TileDrawStats DrawTiles(
        const std::unordered_set<TileKey>& leafSet,
        std::unordered_map<TileKey, Tile>& tiles,
        const glm::mat4& mvp,
        const glm::vec3& cameraPos,
        double currentTime,
        float cameraSpeedKmPerSec,
        bool requireTerrainForLeaves,
        bool useLogDepth,
        float logDepthFarKm,
        bool wireframe,
        uint32_t loadingTexture,  // Placeholder texture ID
        HeightmapManager* heightmapManager = nullptr,  // Optional: GPU terrain displacement
        DemManager* demManager = nullptr,              // Optional: DEM coverage for terrain gating
        bool useRte = true,                            // RTE/RTC jitter-free rendering
        bool fallbackRequireParentUntilChildrenReady = true, // Parent fallback control
        bool useTextureArray = false,                  // Faz 2B: Use GL_TEXTURE_2D_ARRAY
        bool useDistanceBasedTerrainMorph = true,      // P2: Distance-based terrain morph
        float terrainMorphDistanceRangeKm = 0.2f,      // P2: Morph band width in km
        bool enableTerrainMorphTimeFallback = true     // P2: Allow time fallback if distance invalid
    );

private:
    // Find nearest renderable ancestor (parent → grandparent → base)
    // By default, renderable ancestor must have a non-placeholder raster texture.
    // When allowPlaceholder=true, placeholder textures are accepted as a last-resort
    // fallback to prevent transient black gaps while meshes/textures are streaming in.
    Tile* FindRenderableAncestor(const TileKey& key,
                                 std::unordered_map<TileKey, Tile>& tiles,
                                 uint32_t loadingTexture,
                                 bool allowPlaceholder);

    TileRenderer& tileRenderer_;
    ShaderManager& shaderManager_;
};

} // namespace globe
