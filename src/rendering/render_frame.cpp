#include "render_frame.h"
#include <glad/glad.h>
#include <algorithm>
#include <vector>

namespace globe {

RenderFrame::RenderFrame(TileRenderer& tileRenderer, ShaderManager& shaderManager)
    : tileRenderer_(tileRenderer)
    , shaderManager_(shaderManager)
{
}

// Find nearest renderable ancestor (parent → grandparent → base)
// Renderable = hasMesh && textureId != 0
Tile* RenderFrame::FindRenderableAncestor(const TileKey& key, std::unordered_map<TileKey, Tile>& tiles) {
    TileKey parentKey = key.Parent();
    
    while (parentKey.level >= 0) {
        auto it = tiles.find(parentKey);
        if (it != tiles.end()) {
            Tile& tile = it->second;
            // Renderable = hasMesh && textureId != 0 (not IsReady!)
            if (tile.hasMesh && tile.textureId != 0) {
                return &tile;
            }
        }
        if (parentKey.level == 0) break;
        parentKey = parentKey.Parent();
    }
    
    return nullptr;
}

RenderFrame::TileDrawStats RenderFrame::DrawTiles(
    const std::unordered_set<TileKey>& leafSet,
    std::unordered_map<TileKey, Tile>& tiles,
    const glm::mat4& mvp,
    double currentTime,
    bool wireframe,
    uint32_t loadingTexture,
    HeightmapManager* heightmapManager
) {
    TileDrawStats stats;
    
    // Collect tiles for 3-pass rendering (GE-style gap-free)
    std::vector<std::pair<Tile*, float>> renderableLeaves;  // Pass 2: Ready leaves with fade
    std::unordered_set<TileKey> fallbackSet;                // Dedupe ancestors
    std::vector<Tile*> fallbackTiles;                       // Pass 1: Parent fallbacks (opaque)
    std::vector<Tile*> placeholderTiles;                    // Pass 0: Last-resort placeholder
    
    renderableLeaves.reserve(leafSet.size());
    
    for (const TileKey& key : leafSet) {
        auto it = tiles.find(key);
        if (it == tiles.end()) {
            // Leaf key not in tiles map - find ancestor or placeholder
            Tile* ancestor = FindRenderableAncestor(key, tiles);
            if (ancestor && fallbackSet.find(ancestor->key) == fallbackSet.end()) {
                fallbackSet.insert(ancestor->key);
                fallbackTiles.push_back(ancestor);
            } else if (!ancestor) {
                ++stats.missing;  // True gap - no ancestor available
            }
            continue;
        }
        
        Tile& tile = it->second;
        
        // Renderable = hasMesh && textureId != 0 (not IsReady!)
        bool isRenderable = tile.hasMesh && tile.textureId != 0;
        
        if (isRenderable) {
            // Leaf is renderable - render with fade
            float alpha = tile.UpdateFade(currentTime);
            renderableLeaves.push_back({&tile, alpha});
            ++stats.renderableLeaves;
        } else {
            // Leaf not renderable - categorize and find fallback
            if (!tile.hasMesh) {
                ++stats.leafNoMesh;
            } else {
                ++stats.leafNoTexture;  // Has mesh but no texture
            }
            
            // GE-Style: Use parent tile until child is ready
            Tile* ancestor = FindRenderableAncestor(key, tiles);
            if (ancestor && fallbackSet.find(ancestor->key) == fallbackSet.end()) {
                fallbackSet.insert(ancestor->key);
                fallbackTiles.push_back(ancestor);
            } else if (!ancestor) {
                // Last resort: placeholder if mesh exists
                if (tile.hasMesh && loadingTexture != 0) {
                    placeholderTiles.push_back(&tile);
                } else {
                    ++stats.missing;  // True gap
                }
            }
        }
    }
    
    stats.fallbackTiles = static_cast<int>(fallbackTiles.size());
    stats.placeholderTiles = static_cast<int>(placeholderTiles.size());
    
    // Sort fallback tiles by level ascending (coarser first)
    std::sort(fallbackTiles.begin(), fallbackTiles.end(),
              [](const Tile* a, const Tile* b) { return a->key.level < b->key.level; });
    
    // Sort ready leaves by alpha (opaque first, then fading)
    std::sort(renderableLeaves.begin(), renderableLeaves.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Begin batch rendering
    tileRenderer_.BeginBatch(mvp, wireframe);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Pass 0: Placeholder tiles (last-resort, underneath everything)
    if (loadingTexture != 0) {
        for (Tile* tile : placeholderTiles) {
            glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
            tileRenderer_.RenderTileWithTexture(*tile, loadingTexture);
        }
    }
    
    // Pass 1: Fallback ancestor tiles (opaque, parent texture)
    for (Tile* tile : fallbackTiles) {
        glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
        // Try heightmap rendering if available
        HeightmapTexture hmTex;
        if (heightmapManager && heightmapManager->GetTexture(tile->key, hmTex)) {
            tileRenderer_.RenderTileWithHeightmap(*tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight);
        } else {
            tileRenderer_.RenderTile(*tile);
        }
    }
    
    // Pass 2: Renderable leaves (with fade-in)
    for (const auto& [tile, alpha] : renderableLeaves) {
        glUniform1f(shaderManager_.GetFadeLocation(), alpha);
        // Try heightmap rendering if available
        HeightmapTexture hmTex;
        if (heightmapManager && heightmapManager->GetTexture(tile->key, hmTex)) {
            tileRenderer_.RenderTileWithHeightmap(*tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight);
        } else {
            tileRenderer_.RenderTile(*tile);
        }
    }
    
    glDisable(GL_BLEND);
    tileRenderer_.EndBatch();
    
    return stats;
}

} // namespace globe
