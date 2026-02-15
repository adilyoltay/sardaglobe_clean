#include "render_frame.h"
#include "unpop_crossfade.h"
#include <glad/glad.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace globe {

namespace {

bool HasRenderableSurface(const Tile& tile) {
    return tile.hasMesh && tile.surfaceVertexCount > 0;
}
std::size_t gInvalidUnpopTargetFallbacks = 0;

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
            const bool hasSurfaceGeometry = HasRenderableSurface(tile);
            const bool hasTexture = tile.textureId != 0 &&
                                    (allowPlaceholder || tile.textureId != loadingTexture);
            if (hasSurfaceGeometry && hasTexture) {
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
    bool requireTerrainForLeaves,
    bool useLogDepth,
    float logDepthFarKm,
    bool wireframe,
    uint32_t loadingTexture,
    HeightmapManager* heightmapManager,
    DemManager* demManager,
    bool useRte,
    bool fallbackRequireParentUntilChildrenReady,
    bool useTextureArray,
    bool useDistanceBasedTerrainMorph,
    float terrainMorphDistanceRangeKm,
    bool enableTerrainMorphTimeFallback
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
        int unpopTextureLayer = -1;
        TileRenderer::TextureTarget unpopTarget = TileRenderer::TextureTarget::k2D;
        bool useShaderCrossfade = false;
    };
    
    // Collect tiles for 3-pass rendering (GE-style gap-free)
    std::vector<RenderableLeaf> renderableLeaves;           // Pass 2: Ready leaves (fade/crossfade)
    std::unordered_set<TileKey> fallbackSet;                // Dedupe ancestors
    std::vector<Tile*> fallbackTiles;                       // Pass 1: Parent fallbacks (opaque)
    std::vector<Tile*> placeholderTiles;                    // Pass 0: Last-resort placeholder
    
    renderableLeaves.reserve(leafSet.size());

    auto hasAnyHeightmap = [&](const TileKey& key) -> bool {
        if (!heightmapManager) return false;
        TileKey probe = key;
        while (true) {
            if (heightmapManager->HasTexture(probe)) {
                return true;
            }
            if (probe.level == 0) break;
            probe = probe.Parent();
        }
        return false;
    };

    auto hasAnyDemCoverage = [&](const TileKey& key) -> bool {
        if (!demManager) return false;
        return demManager->HasDataOrAncestor(key);
    };

    auto findTerrainAncestor = [&](const TileKey& key, bool allowPlaceholder) -> Tile* {
        if (!requireTerrainForLeaves) {
            return nullptr;
        }

        TileKey parentKey = key.Parent();
        while (parentKey.level >= 0) {
            auto it = tiles.find(parentKey);
            if (it != tiles.end()) {
                Tile& tile = it->second;
                const bool hasSurfaceGeometry = HasRenderableSurface(tile);
                const bool hasTexture = tile.textureId != 0 &&
                                        (allowPlaceholder || tile.textureId != loadingTexture) &&
                                        !tile.mostlyBlackOpaqueRaster;
                if (hasSurfaceGeometry && hasTexture) {
                    bool hasTerrain = heightmapManager ? hasAnyHeightmap(parentKey) : tile.demUsed;
                    if (!hasTerrain) {
                        const bool terrainExpected = tile.demPending || hasAnyDemCoverage(parentKey);
                        if (!terrainExpected) {
                            // No DEM coverage for this region: allow flat ancestor rendering.
                            hasTerrain = true;
                        }
                    }
                    if (hasTerrain) {
                        return &tile;
                    }
                }
            }
            if (parentKey.level == 0) {
                break;
            }
            parentKey = parentKey.Parent();
        }
        return nullptr;
    };

    auto addFallbackAncestor = [&](const TileKey& key) -> bool {
        Tile* ancestor = nullptr;

        if (!fallbackRequireParentUntilChildrenReady) {
            return false;
        }

        // When terrain is required, prefer an ancestor that also has terrain data.
        // Rendering a displaced child against a flat ancestor is the primary source of
        // visible cliff/wall artifacts at joins.
        ancestor = findTerrainAncestor(key, /*allowPlaceholder=*/false);
        if (!ancestor) {
            ancestor = findTerrainAncestor(key, /*allowPlaceholder=*/true);
        }
        if (!ancestor) {
            ancestor = FindRenderableAncestor(key, tiles, loadingTexture, false);
            if (!ancestor) {
                // Keep coverage continuous while streaming: allow placeholder ancestors
                // only when no real raster ancestor exists.
                ancestor = FindRenderableAncestor(key, tiles, loadingTexture, true);
            }
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
        const bool hasRealTexture = tile.textureId != 0 &&
                                    tile.textureId != loadingTexture &&
                                    !tile.mostlyBlackOpaqueRaster;
        bool hasRequiredTerrain = true;
        if (requireTerrainForLeaves) {
            if (heightmapManager) {
                hasRequiredTerrain = hasAnyHeightmap(tile.key);
            } else {
                hasRequiredTerrain = tile.demUsed;
            }
            if (!hasRequiredTerrain) {
                const bool terrainExpected = tile.demPending || hasAnyDemCoverage(tile.key);
                if (!terrainExpected) {
                    // DEM unavailable for this tile/ancestors: don't block rendering.
                    hasRequiredTerrain = true;
                }
            }
        }
        bool isRenderable = HasRenderableSurface(tile) && hasRealTexture && hasRequiredTerrain;
        
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

            // Some providers encode nodata with transparent texels (alpha < 1).
            // Keep an ancestor underlay beneath such leaves so transparent regions
            // reveal valid parent imagery instead of the clear color (black gaps/lines).
            if (tile.hasTransparentPixels) {
                addFallbackAncestor(key);
            }

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
                    leaf.unpopTextureLayer = ancestor->textureArrayLayer;
                    const bool useUnpopArray = useTextureArray &&
                                              ancestor->usesTextureArray &&
                                              ancestor->textureArrayLayer >= 0 &&
                                              ancestor->textureArrayTier >= 0 &&
                                              ancestor->textureId != 0;
                    leaf.unpopTarget = useUnpopArray
                        ? TileRenderer::TextureTarget::kArray
                        : TileRenderer::TextureTarget::k2D;
                    if (leaf.unpopTarget == TileRenderer::TextureTarget::kArray &&
                        leaf.unpopTextureLayer < 0) {
                        // Defensive guard: malformed state should never block rendering.
                        ++gInvalidUnpopTargetFallbacks;
                        leaf.unpopTarget = TileRenderer::TextureTarget::k2D;
                    }
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
            if (!HasRenderableSurface(tile)) {
                ++stats.leafNoMesh;
            } else if (!hasRealTexture) {
                ++stats.leafNoTexture;  // Has mesh but no real texture (or loading placeholder only)
            } else if (requireTerrainForLeaves && !hasRequiredTerrain) {
                ++stats.leafNoTerrain;
            }
            
            // GE-Style: Use parent tile until child is ready
            if (!addFallbackAncestor(key)) {
                // Last resort: placeholder if mesh exists
                if (HasRenderableSurface(tile) && loadingTexture != 0) {
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

    auto resolveHeightmap = [&](const TileKey& tileKey,
                                HeightmapTexture& outTex,
                                glm::vec4& outUvTransform) -> bool {
        if (!heightmapManager) {
            return false;
        }
        TileKey probe = tileKey;
        while (true) {
            HeightmapTexture tex;
            if (heightmapManager->GetTexture(probe, tex)) {
                outTex = tex;
                outUvTransform = (probe == tileKey)
                    ? glm::vec4(1.0f, 1.0f, 0.0f, 0.0f)
                    : ComputeUnpopUvTransform(tileKey, probe);
                return true;
            }
            if (probe.level == 0) {
                break;
            }
            probe = probe.Parent();
        }
        return false;
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
    tileRenderer_.BeginBatch(mvp, wireframe, useLogDepth, logDepthFarKm, useRte, useTextureArray);
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
        // FIX 1: Tiles awaiting DEM must go through the standard skirt-capable path.
        // Instanced flat path has no skirt geometry, so demPending tiles rendered here
        // leave uncovered gaps between displaced neighbors and flat self.
        if (tile.demPending) return false;
        // Transparent or mostly-black tiles need ancestor underlay, which the
        // instanced path does not support (no per-tile crossfade/fallback).
        if (tile.hasTransparentPixels || tile.mostlyBlackOpaqueRaster) return false;
        if (!tile.atlasAllocated) return false;
        if (tile.textureId == 0) return false;
        if (tile.builtSegments <= 1) return false;
        if (tile.skirtIndexCount > 0) return false;
        return true;
    };
    
    // Depth bias policy (GE-style):
    // - Parent/placeholder fallback tiles should be pushed behind leaves to avoid z-fighting.
    // - Main leaves render without polygon offset to avoid depth discontinuities that
    //   can manifest as dark grids / "black gap" lines at tile boundaries.
    // FIX 4: Increased offset from (1.0, 1.0) to (2.0, 4.0) to prevent log-depth
    // quantization-induced z-fighting between ancestor underlay and leaf overlay,
    // especially visible at oblique (tilted) views and far-horizon tiles.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

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
        glm::vec4 hmUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
        bool hasHeightmap = resolveHeightmap(tile->key, hmTex, hmUvTransform);
        bool hasTerrainData = hasHeightmap || tile->demUsed;
        // P2: Calculate camera distance for distance-based morph
        float distanceKm = glm::length(tile->center - cameraPos);
        float terrainMorph = tile->UpdateTerrainMorph(
            currentTime, hasTerrainData, distanceKm,
            useDistanceBasedTerrainMorph, terrainMorphDistanceRangeKm,
            Tile::TERRAIN_MORPH_DURATION, enableTerrainMorphTimeFallback
        );
        if (canBatchFlatTile(*tile, hasHeightmap, false)) {
            BatchKey key{tile->textureId, tile->builtSegments};
            fallbackInstancedBatches[key].push_back(TileRenderer::FlatTileInstance{tile, 1.0f});
            continue;
        }
        if (hasHeightmap) {
            tileRenderer_.RenderTileWithHeightmap(*tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight,
                                                  hmUvTransform, terrainMorph);
        } else {
            tileRenderer_.RenderTile(*tile, tile->demUsed ? terrainMorph : 1.0f);
        }
    }
    for (auto& [key, instances] : fallbackInstancedBatches) {
        tileRenderer_.RenderFlatTilesInstanced(key.textureId, key.segments, instances);
    }
    
    // Pass 2: Renderable leaves (legacy fade or shader-level crossfade)
    glDisable(GL_POLYGON_OFFSET_FILL);
    InstancedBatchMap leafInstancedBatches;
    for (const RenderableLeaf& leaf : renderableLeaves) {
        HeightmapTexture hmTex;
        glm::vec4 hmUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
        bool hasHeightmap = resolveHeightmap(leaf.tile->key, hmTex, hmUvTransform);
        bool hasTerrainData = hasHeightmap || leaf.tile->demUsed;
        // P2: Calculate camera distance for distance-based morph
        float distanceKm = glm::length(leaf.tile->center - cameraPos);
        float terrainMorph = leaf.tile->UpdateTerrainMorph(
            currentTime, hasTerrainData, distanceKm,
            useDistanceBasedTerrainMorph, terrainMorphDistanceRangeKm,
            Tile::TERRAIN_MORPH_DURATION, enableTerrainMorphTimeFallback
        );

        if (leaf.useShaderCrossfade && leaf.unpopAncestor) {
            glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
            tileRenderer_.RenderTileWithCrossfade(
                *leaf.tile,
                leaf.unpopAncestor->textureId,
                leaf.unpopTextureLayer,
                leaf.unpopTarget,
                leaf.unpopUvTransform,
                leaf.alpha,
                hasHeightmap ? hmTex.textureId : 0,
                hasHeightmap ? hmTex.minHeight : 0.0f,
                hasHeightmap ? hmTex.maxHeight : 0.0f,
                hasHeightmap ? hmUvTransform : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
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
                tileRenderer_.RenderTileWithHeightmap(*leaf.tile, hmTex.textureId, hmTex.minHeight, hmTex.maxHeight,
                                                      hmUvTransform, terrainMorph);
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
