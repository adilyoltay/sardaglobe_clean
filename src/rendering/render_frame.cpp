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

RenderFrame::TileDrawStats RenderFrame::DrawTiles(
    const std::unordered_set<TileKey>& leafSet,
    std::unordered_map<TileKey, Tile>& tiles,
    const glm::mat4& mvp,
    double currentTime,
    bool wireframe
) {
    TileDrawStats stats;
    
    // Collect tiles to render with fade-in animation (Google Earth style)
    std::vector<std::pair<Tile*, float>> tilesToRender;
    tilesToRender.reserve(leafSet.size());
    
    for (const TileKey& key : leafSet) {
        auto it = tiles.find(key);
        if (it != tiles.end()) {
            Tile& tile = it->second;
            if (tile.IsReady() && tile.hasMesh && tile.textureId != 0) {
                float alpha = tile.UpdateFade(currentTime);
                tilesToRender.push_back({&tile, alpha});
            } else {
                ++stats.tilesSkipped;
            }
        }
    }
    
    // Sort by alpha for proper blending (opaque first, then transparent)
    std::sort(tilesToRender.begin(), tilesToRender.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Begin batch rendering
    tileRenderer_.BeginBatch(mvp, wireframe);
    
    // Enable blending for fade-in effect
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Render collected tiles with fade
    for (const auto& [tile, alpha] : tilesToRender) {
        glUniform1f(shaderManager_.GetFadeLocation(), alpha);
        tileRenderer_.RenderTile(*tile);
        ++stats.tilesRendered;
    }
    
    glDisable(GL_BLEND);
    
    tileRenderer_.EndBatch();
    
    return stats;
}

} // namespace globe
