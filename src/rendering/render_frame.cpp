#include "render_frame.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace globe {

namespace {

constexpr float kUnpopShortenStartKmPerSec = 120.0f;
constexpr float kUnpopBypassKmPerSec = 900.0f;
constexpr float kUnpopMinDurationSec = 0.08f;
constexpr float kFadeCompleteEpsilon = 0.999f;

float ComputeUnpopDurationSec(float cameraSpeedKmPerSec) {
    float speed = std::max(0.0f, cameraSpeedKmPerSec);
    if (!std::isfinite(speed)) {
        speed = 0.0f;
    }
    if (speed <= kUnpopShortenStartKmPerSec) {
        return Tile::FADE_DURATION;
    }
    if (speed >= kUnpopBypassKmPerSec) {
        return kUnpopMinDurationSec;
    }
    float t = (speed - kUnpopShortenStartKmPerSec) /
              (kUnpopBypassKmPerSec - kUnpopShortenStartKmPerSec);
    return Tile::FADE_DURATION + t * (kUnpopMinDurationSec - Tile::FADE_DURATION);
}

bool ShouldBypassUnpop(float cameraSpeedKmPerSec) {
    float speed = std::max(0.0f, cameraSpeedKmPerSec);
    return std::isfinite(speed) && speed >= kUnpopBypassKmPerSec;
}

glm::vec4 ComputeUnpopUvTransform(const TileKey& leaf, const TileKey& ancestor) {
    if (ancestor.level >= leaf.level) {
        return glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    }

    int delta = leaf.level - ancestor.level;
    float scale = std::ldexp(1.0f, -delta);  // 1 / (2^delta)
    std::int64_t factor = static_cast<std::int64_t>(1) << delta;
    std::int64_t relX = static_cast<std::int64_t>(leaf.x) -
                        static_cast<std::int64_t>(ancestor.x) * factor;
    std::int64_t relY = static_cast<std::int64_t>(leaf.y) -
                        static_cast<std::int64_t>(ancestor.y) * factor;
    return glm::vec4(scale, scale, static_cast<float>(relX) * scale, static_cast<float>(relY) * scale);
}

glm::vec4 ComposeUvTransform(const glm::vec4& outerTransform, const glm::vec4& innerTransform) {
    // Apply "inner" first, then "outer":
    // uv1 = uv * inner.xy + inner.zw
    // uv2 = uv1 * outer.xy + outer.zw
    // -> uv2 = uv * (inner.xy * outer.xy) + (inner.zw * outer.xy + outer.zw)
    return glm::vec4(
        innerTransform.x * outerTransform.x,
        innerTransform.y * outerTransform.y,
        innerTransform.z * outerTransform.x + outerTransform.z,
        innerTransform.w * outerTransform.y + outerTransform.w
    );
}

} // namespace

RenderFrame::RenderFrame(TileRenderer& tileRenderer, ShaderManager& shaderManager)
    : tileRenderer_(tileRenderer)
    , shaderManager_(shaderManager)
{
}

// Find nearest renderable ancestor (parent → grandparent → base).
// Renderable ancestor must have a real raster texture (not loading placeholder) unless
// allowPlaceholder=true (used only as a last-resort gap filler).
Tile* RenderFrame::FindRenderableAncestor(const TileKey& key,
                                          std::unordered_map<TileKey, Tile>& tiles,
                                          uint32_t loadingTexture,
                                          bool allowPlaceholder) {
    TileKey parentKey = key.Parent();
    
    while (parentKey.level >= 0) {
        auto it = tiles.find(parentKey);
        if (it != tiles.end()) {
            Tile& tile = it->second;
            const bool hasTexture = tile.textureId != 0 &&
                                    (allowPlaceholder || tile.textureId != loadingTexture);
            if (tile.hasMesh && hasTexture) {
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
    const glm::vec3& cameraPos,
    double currentTime,
    float cameraSpeedKmPerSec,
    bool useLogDepth,
    float logDepthFarKm,
    bool wireframe,
    uint32_t loadingTexture,
    HeightmapManager* heightmapManager
) {
    TileDrawStats stats;
    const float fadeDurationSec = ComputeUnpopDurationSec(cameraSpeedKmPerSec);
    const bool bypassUnpop = ShouldBypassUnpop(cameraSpeedKmPerSec);
    const bool useInstancedFlatPath = tileRenderer_.SupportsInstancedFlatPath() && !wireframe;

    struct RenderableLeaf {
        Tile* tile = nullptr;
        float alpha = 1.0f;                // Used for legacy fade-only path
        Tile* unpopAncestor = nullptr;     // Used in shader-level crossfade
        glm::vec4 unpopUvTransform{1.0f};  // scale.xy + offset.zw
        bool useShaderCrossfade = false;
    };
    
    // Collect tiles for 3-pass rendering (GE-style gap-free)
    std::vector<RenderableLeaf> renderableLeaves;           // Pass 2: Ready leaves (fade/crossfade)
    std::unordered_set<TileKey> fallbackSet;                // Dedupe ancestors
    std::vector<Tile*> fallbackTiles;                       // Pass 1: Parent fallbacks (opaque)
    std::vector<Tile*> placeholderTiles;                    // Pass 0: Last-resort placeholder
    
    renderableLeaves.reserve(leafSet.size());

    auto addFallbackAncestor = [&](const TileKey& key) -> bool {
        Tile* ancestor = FindRenderableAncestor(key, tiles, loadingTexture, false);
        if (!ancestor) {
            // Keep coverage continuous while streaming: allow placeholder ancestors
            // only when no real raster ancestor exists.
            ancestor = FindRenderableAncestor(key, tiles, loadingTexture, true);
        }
        if (ancestor && fallbackSet.find(ancestor->key) == fallbackSet.end()) {
            fallbackSet.insert(ancestor->key);
            fallbackTiles.push_back(ancestor);
        }
        return ancestor != nullptr;
    };
    
    for (const TileKey& key : leafSet) {
        auto it = tiles.find(key);
        if (it == tiles.end()) {
            // Leaf key not in tiles map - find ancestor or placeholder
            if (!addFallbackAncestor(key)) {
                ++stats.missing;  // True gap - no ancestor available
            }
            continue;
        }
        
        Tile& tile = it->second;
        
        // Treat loading texture as placeholder-only content.
        // It should not participate in normal leaf rendering or ancestor fallback.
        const bool hasRealTexture = tile.textureId != 0 && tile.textureId != loadingTexture;
        bool isRenderable = tile.hasMesh && hasRealTexture;
        
        if (isRenderable) {
            // Leaf is renderable - render with fade
            float alpha = 1.0f;
            if (bypassUnpop) {
                tile.fadeAlpha = 1.0f;
                tile.fadeComplete = true;
                if (tile.fadeStartTime == 0.0) {
                    tile.fadeStartTime = currentTime;
                }
            } else {
                alpha = tile.UpdateFade(currentTime, fadeDurationSec);
            }
            
            RenderableLeaf leaf;
            leaf.tile = &tile;
            leaf.alpha = alpha;

            if (alpha < kFadeCompleteEpsilon) {
                Tile* ancestor = FindRenderableAncestor(key, tiles, loadingTexture, false);
                if (ancestor) {
                    ++stats.crossfadingLeaves;

                    // Prefer shader-level raster crossfade when parent texture exists.
                    // This mirrors GE's uUnpopBlend + unpop texture path.
                    leaf.useShaderCrossfade = true;
                    leaf.unpopAncestor = ancestor;
                    glm::vec4 relativeUnpopUv = ComputeUnpopUvTransform(tile.key, ancestor->key);
                    leaf.unpopUvTransform = ComposeUvTransform(ancestor->texScaleOffset, relativeUnpopUv);
                } else {
                    // No parent available -> avoid temporary transparency holes.
                    leaf.alpha = 1.0f;
                }
            }

            // Legacy fallback path: keep parent tile underlay only when shader crossfade is unavailable.
            if (!leaf.useShaderCrossfade &&
                leaf.alpha < kFadeCompleteEpsilon &&
                addFallbackAncestor(key)) {
                ++stats.crossfadingLeaves;
            }

            renderableLeaves.push_back(leaf);
            ++stats.renderableLeaves;
        } else {
            // Leaf not renderable - categorize and find fallback
            if (!tile.hasMesh) {
                ++stats.leafNoMesh;
            } else {
                ++stats.leafNoTexture;  // Has mesh but no real texture (or loading placeholder only)
            }
            
            // GE-Style: Use parent tile until child is ready
            if (!addFallbackAncestor(key)) {
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
    
    auto distanceSqToCamera = [&](const Tile* tile) {
        glm::vec3 delta = tile->center - cameraPos;
        return glm::dot(delta, delta);
    };

    // GE P5.2 parity: fallback tiles sorted front-to-back by camera distance.
    std::sort(fallbackTiles.begin(), fallbackTiles.end(),
              [&](const Tile* a, const Tile* b) {
                  float da = distanceSqToCamera(a);
                  float db = distanceSqToCamera(b);
                  if (da != db) {
                      return da < db;
                  }
                  return a->key.level > b->key.level;
              });

    // GE P5.2 parity:
    // 1) opaque leaves first (alpha ~ 1.0),
    // 2) fading leaves next,
    // each group front-to-back for better early-Z efficiency.
    std::sort(renderableLeaves.begin(), renderableLeaves.end(),
              [&](const RenderableLeaf& a, const RenderableLeaf& b) {
                  bool aOpaque = a.alpha >= kFadeCompleteEpsilon;
                  bool bOpaque = b.alpha >= kFadeCompleteEpsilon;
                  if (aOpaque != bOpaque) {
                      return aOpaque > bOpaque;
                  }
                  float da = distanceSqToCamera(a.tile);
                  float db = distanceSqToCamera(b.tile);
                  if (da != db) {
                      return da < db;
                  }
                  return a.alpha > b.alpha;
              });
    
    // Begin batch rendering
    tileRenderer_.BeginBatch(mvp, wireframe, useLogDepth, logDepthFarKm);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    struct BatchKey {
        uint32_t textureId = 0;
        int segments = 0;
        bool operator==(const BatchKey& other) const {
            return textureId == other.textureId && segments == other.segments;
        }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& key) const noexcept {
            return (static_cast<std::size_t>(key.textureId) << 1) ^
                   static_cast<std::size_t>(key.segments * 2654435761u);
        }
    };
    using InstancedBatchMap = std::unordered_map<BatchKey, std::vector<TileRenderer::FlatTileInstance>, BatchKeyHash>;

    auto canBatchFlatTile = [&](const Tile& tile, bool hasHeightmap, bool usesCrossfade) {
        if (!useInstancedFlatPath) return false;
        if (usesCrossfade || hasHeightmap) return false;
        if (tile.demUsed || tile.terrainMorphActive) return false;
        if (!tile.atlasAllocated) return false;
        if (tile.textureId == 0) return false;
        if (tile.builtSegments <= 1) return false;
        if (tile.skirtIndexCount > 0) return false;
        return true;
    };
    
    // Pass 0: Placeholder tiles (last-resort, underneath everything)
    if (loadingTexture != 0) {
        for (Tile* tile : placeholderTiles) {
            glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
            tileRenderer_.RenderTileWithTexture(*tile, loadingTexture, 1.0f);
        }
    }
    
    // Pass 1: Fallback ancestor tiles (opaque, parent texture)
    InstancedBatchMap fallbackInstancedBatches;
    for (Tile* tile : fallbackTiles) {
        glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
        // Try heightmap rendering if available
        HeightmapTexture hmTex;
        bool hasHeightmap = heightmapManager && heightmapManager->GetTexture(tile->key, hmTex);
        bool hasTerrainData = hasHeightmap || tile->demUsed;
        float terrainMorph = tile->UpdateTerrainMorph(currentTime, hasTerrainData);
        if (canBatchFlatTile(*tile, hasHeightmap, false)) {
            BatchKey key{tile->textureId, tile->builtSegments};
            fallbackInstancedBatches[key].push_back(TileRenderer::FlatTileInstance{tile, 1.0f});
            continue;
        }
        if (hasHeightmap) {
            tileRenderer_.RenderTileWithHeightmap(*tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight, terrainMorph);
        } else {
            tileRenderer_.RenderTile(*tile, tile->demUsed ? terrainMorph : 1.0f);
        }
    }
    for (auto& [key, instances] : fallbackInstancedBatches) {
        tileRenderer_.RenderFlatTilesInstanced(key.textureId, key.segments, instances);
    }
    
    // Pass 2: Renderable leaves (legacy fade or shader-level crossfade)
    InstancedBatchMap leafInstancedBatches;
    for (const RenderableLeaf& leaf : renderableLeaves) {
        HeightmapTexture hmTex;
        bool hasHeightmap = heightmapManager && heightmapManager->GetTexture(leaf.tile->key, hmTex);
        bool hasTerrainData = hasHeightmap || leaf.tile->demUsed;
        float terrainMorph = leaf.tile->UpdateTerrainMorph(currentTime, hasTerrainData);

        if (leaf.useShaderCrossfade && leaf.unpopAncestor) {
            glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
            tileRenderer_.RenderTileWithCrossfade(
                *leaf.tile,
                leaf.unpopAncestor->textureId,
                leaf.unpopUvTransform,
                leaf.alpha,
                hasHeightmap ? hmTex.textureId : 0,
                hasHeightmap ? hmTex.minHeight : 0.0f,
                hasHeightmap ? hmTex.maxHeight : 0.0f,
                terrainMorph
            );
        } else {
            if (canBatchFlatTile(*leaf.tile, hasHeightmap, false)) {
                BatchKey key{leaf.tile->textureId, leaf.tile->builtSegments};
                leafInstancedBatches[key].push_back(TileRenderer::FlatTileInstance{leaf.tile, leaf.alpha});
                continue;
            }
            glUniform1f(shaderManager_.GetFadeLocation(), leaf.alpha);
            if (hasHeightmap) {
                tileRenderer_.RenderTileWithHeightmap(*leaf.tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight, terrainMorph);
            } else {
                tileRenderer_.RenderTile(*leaf.tile, leaf.tile->demUsed ? terrainMorph : 1.0f);
            }
        }
    }
    for (auto& [key, instances] : leafInstancedBatches) {
        tileRenderer_.RenderFlatTilesInstanced(key.textureId, key.segments, instances);
    }
    
    glDisable(GL_BLEND);
    tileRenderer_.EndBatch();
    
    return stats;
}

} // namespace globe
