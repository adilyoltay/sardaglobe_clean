#include "tile_mesh_builder.h"
#include "mesh_template.h"
#include "../core/ellipsoid.h"
#include <glad/glad.h>
#include <cmath>
#include <array>
#include <algorithm>
#include <limits>

namespace globe {

namespace {

constexpr int kVertexStrideFloats = 9;  // pos(3), normal(3), uv(2), heightKm(1)
constexpr double kMaxMercatorLatDeg = 85.05112878;

double WrapLonDeg(double lonDeg) {
    while (lonDeg < -180.0) lonDeg += 360.0;
    while (lonDeg > 180.0) lonDeg -= 360.0;
    return lonDeg;
}

double SampleBilinear(const DemGridData& data, double u, double v) {
    if (!data.valid || data.heights.empty() || data.meshN <= 1) return 0.0;

    const int meshN = data.meshN;
    double gx = std::clamp(u, 0.0, 1.0) * static_cast<double>(meshN - 1);
    double gy = std::clamp(v, 0.0, 1.0) * static_cast<double>(meshN - 1);

    int x0 = static_cast<int>(std::floor(gx));
    int y0 = static_cast<int>(std::floor(gy));
    int x1 = std::min(x0 + 1, meshN - 1);
    int y1 = std::min(y0 + 1, meshN - 1);

    x0 = std::clamp(x0, 0, meshN - 1);
    y0 = std::clamp(y0, 0, meshN - 1);

    const double fx = gx - static_cast<double>(x0);
    const double fy = gy - static_cast<double>(y0);

    const double h00 = data.heights[static_cast<std::size_t>(y0 * meshN + x0)];
    const double h10 = data.heights[static_cast<std::size_t>(y0 * meshN + x1)];
    const double h01 = data.heights[static_cast<std::size_t>(y1 * meshN + x0)];
    const double h11 = data.heights[static_cast<std::size_t>(y1 * meshN + x1)];

    const double h0 = h00 + fx * (h10 - h00);
    const double h1 = h01 + fx * (h11 - h01);
    return h0 + fy * (h1 - h0);
}

struct DemTileSampler {
    TileKey key;
    Extent extent;
    DemGridData data;
    bool valid = false;
};

TileKey KeyAtLevel(TileKey k, int targetLevel) {
    int lvl = std::clamp(targetLevel, 0, k.level);
    while (k.level > lvl) {
        k = k.Parent();
    }
    return k;
}

bool ResolveDemSampler(const TileKey& desiredKey, DemManager* demManager, DemTileSampler& out) {
    out = DemTileSampler{};
    if (!demManager) return false;

    TileKey probe = desiredKey;
    while (probe.level >= 0) {
        DemGridData grid;
        if (demManager->GetGridData(probe, grid) && grid.valid && grid.meshN > 1 && !grid.heights.empty()) {
            out.key = probe;
            out.extent = Extent::FromTileWGS84(probe.x, probe.y, probe.level);
            out.data = std::move(grid);
            out.valid = true;
            return true;
        }
        if (probe.level == 0) {
            break;
        }
        probe = probe.Parent();
    }
    return false;
}

bool SampleDemMeters(const DemTileSampler& sampler, double lonDeg, double latDeg, double& outMeters) {
    if (!sampler.valid) return false;

    const double lon = WrapLonDeg(lonDeg);
    const double latClamped = std::clamp(latDeg, -kMaxMercatorLatDeg, kMaxMercatorLatDeg);

    const double lonLeft = sampler.extent.West();
    const double lonRight = sampler.extent.East();
    const double latTop = sampler.extent.North();
    const double latBottom = sampler.extent.South();
    const double denomLon = lonRight - lonLeft;
    const double denomLat = latTop - latBottom;
    if (!std::isfinite(denomLon) || !std::isfinite(denomLat) || std::fabs(denomLon) < 1e-12 || std::fabs(denomLat) < 1e-12) {
        return false;
    }

    double u = (lon - lonLeft) / denomLon;
    // Service row order is south->north (bottom->top). So v=0 must map to latBottom.
    double v = (latClamped - latBottom) / denomLat;
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    outMeters = SampleBilinear(sampler.data, u, v);
    return true;
}

} // namespace

TileMeshBuilder::BuildResult TileMeshBuilder::Build(
    const TileKey& key,
    const Extent& inputExtent,
    uint8_t edgeMask,
    uint8_t stitchMask,
    uint8_t skirtMask,
    int demTargetLevel,
    uint32_t demEdgeLevelPack,
    DemManager* demManager,
    const Config& config,
    bool useSharedEBO
) {
    BuildResult result;
    result.key = key;
    result.useSharedEBO = useSharedEBO;
    result.stitchMask = stitchMask;
    result.skirtMask = skirtMask;
    result.demEffectiveLevel = static_cast<uint8_t>(std::clamp(demTargetLevel, 0, 255));
    (void)edgeMask;  // Edge equalization is now driven by demEdgeLevelPack + blend band.
    
    // Adaptive mesh segments: scale tessellation with tile LOD level.
    // Higher zoom tiles cover less area (less curvature) and DEM grid is small (e.g. 5×5),
    // so fewer segments suffice. This prevents the "sudden extreme resolution" jump
    // when zooming close to terrain (64 segments for every tile = 8K triangles each).
    const int segments = AdaptiveMeshSegments(key.level, config.meshSegments, config.demMeshN, demManager != nullptr);
    const int vertexCount = (segments + 1) * (segments + 1);
    const int indexCount = segments * segments * 6;
    result.segments = segments;
    
    result.vertices.reserve(vertexCount * kVertexStrideFloats);
    if (!useSharedEBO) {
        result.indices.reserve(indexCount);
    }
    
    // Use tile's Extent (or compute from tile key using proper Web Mercator projection)
    Extent extent = inputExtent;
    if (extent.Width() == 0.0) {
        // CRITICAL: Use Extent::FromTileWGS84 for correct Web Mercator bounds
        // Linear interpolation would distort at higher latitudes
        extent = Extent::FromTileWGS84(key.x, key.y, key.level);
    }
    
    double lonLeft = extent.West();
    double lonRight = extent.East();
    double latTop = extent.North();
    double latBottom = extent.South();
    
    // WGS84 ellipsoid (km units for camera compatibility)
    const Ellipsoid& ellipsoid = Ellipsoid::WGS84_KM();
    
    // Pre-calculate Mercator Y for top and bottom (for correct UV mapping)
    // Web Mercator tiles are linear in Mercator Y space, NOT in latitude space
    double latTopRad = latTop * M_PI / 180.0;
    double latBottomRad = latBottom * M_PI / 180.0;
    double mercatorYTop = std::log(std::tan(M_PI / 4.0 + latTopRad / 2.0));
    double mercatorYBottom = std::log(std::tan(M_PI / 4.0 + latBottomRad / 2.0));
    
    // DEM sampling for CPU-mesh displacement mode.
    //
    // IMPORTANT: Avoid per-vertex DemManager::SampleHeightDetailed() calls (mutex + parent walk).
    // Resolve a small set of DEM grid tiles once, then sample bilinear in the mesh worker.
    bool demSamplingEnabled = false;
    bool hasExactDemTile = false;
    int authoritativeLevel = key.level;
    DemTileSampler demInterior;
    DemTileSampler demEdge[4];  // N,E,S,W (resolved on self's ancestor chain for determinism)

    int clampedTargetLevel = std::clamp(demTargetLevel >= 0 ? demTargetLevel : key.level, 0, key.level);
    TileKey desiredInteriorKey = KeyAtLevel(key, clampedTargetLevel);
    if (demManager && config.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
        hasExactDemTile = demManager->HasData(key);

        if (ResolveDemSampler(desiredInteriorKey, demManager, demInterior)) {
            demSamplingEnabled = true;
            authoritativeLevel = demInterior.key.level;
        } else {
            // Coherence target (demTargetLevel) may be an ancestor key that isn't cached yet,
            // even when we already have exact child DEM. Never bake "flat" in that case:
            // fall back to best-available (exact-or-ancestor) on self's chain.
            //
            // Engine-side policy will request the coherent ancestor keys and trigger rebuilds
            // once they arrive.
            if (ResolveDemSampler(key, demManager, demInterior)) {
                demSamplingEnabled = true;
                authoritativeLevel = demInterior.key.level;
            }
        }
    }
    result.demEffectiveLevel = static_cast<uint8_t>(std::clamp(authoritativeLevel, 0, 255));
    
    result.demUsed = false;
    // Keep pending=true while exact child DEM is missing, even if parent fallback provides heights
    // or there is currently no cached DEM at all. Otherwise tiles built "flat" early will never
    // rebuild when DEM arrives, causing persistent cliffs/walls at tile boundaries.
    result.demPending = (demManager != nullptr) &&
                        (config.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) &&
                        !hasExactDemTile;
    
    // First pass: compute lon/lat + DEM sample heights.
    std::vector<double> sampleLonDeg;
    std::vector<double> sampleLatDeg;
    sampleLonDeg.reserve(static_cast<size_t>(vertexCount));
    sampleLatDeg.reserve(static_cast<size_t>(vertexCount));

    std::vector<glm::dvec3> positions;
    std::vector<float> heightsKm;
    positions.reserve(static_cast<size_t>(vertexCount));
    heightsKm.resize(static_cast<size_t>(vertexCount), 0.0f);

    int demSourceLevelMin = std::numeric_limits<int>::max();
    int demSourceLevelMax = std::numeric_limits<int>::min();
    int demMissingSamples = 0;

    // DEM edge-coherence levels (packed 4x u8): N,E,S,W.
    // These levels are computed by the engine as the common ancestor of adjacent DEM keys.
    // Sampling border vertices at these levels avoids "same-coordinate chooses different DEM tile"
    // ambiguity on tile borders, which is the root cause of residual cracks/cliffs.
    int demEdgeLevels[4] = {
        static_cast<int>(demEdgeLevelPack & 0xFFu),
        static_cast<int>((demEdgeLevelPack >> 8) & 0xFFu),
        static_cast<int>((demEdgeLevelPack >> 16) & 0xFFu),
        static_cast<int>((demEdgeLevelPack >> 24) & 0xFFu)
    };
    for (int i = 0; i < 4; ++i) {
        demEdgeLevels[i] = std::clamp(demEdgeLevels[i], 0, key.level);
    }
    int edgeBlendBand = std::max(0, config.demEdgeBlendSegments);
    edgeBlendBand = std::min(edgeBlendBand, segments);

    // Resolve edge-coherent samplers (fixed keyAtLevel chain, not "tile containing coordinate").
    if (demSamplingEnabled && demManager) {
        for (int dir = 0; dir < 4; ++dir) {
            const TileKey desiredEdgeKey = KeyAtLevel(key, demEdgeLevels[dir]);
            ResolveDemSampler(desiredEdgeKey, demManager, demEdge[dir]);
        }
    }

    auto sampleKmFrom = [&](const DemTileSampler& sampler, double lonDeg, double latDeg,
                            float& outHeightKm, int& outSourceLevel) -> bool {
        double meters = 0.0;
        if (!SampleDemMeters(sampler, lonDeg, latDeg, meters)) {
            return false;
        }
        outHeightKm = static_cast<float>(meters * 0.001 * config.demHeightScale);
        outSourceLevel = sampler.key.level;
        return true;
    };
    
    for (int iy = 0; iy <= segments; ++iy) {
        float v = static_cast<float>(iy) / segments;
        
        // CRITICAL: Interpolate in Mercator Y space, then convert to latitude
        // This ensures mesh vertices match the Web Mercator tile texture sampling
        double mercatorY = mercatorYTop + (mercatorYBottom - mercatorYTop) * v;
        double latRad = 2.0 * std::atan(std::exp(mercatorY)) - M_PI / 2.0;
        double lat = latRad * 180.0 / M_PI;
        
        // Check if this row is on North or South border
        bool isNorthBorder = (iy == 0);
        bool isSouthBorder = (iy == segments);
        
        for (int ix = 0; ix <= segments; ++ix) {
            float u = static_cast<float>(ix) / segments;
            double lon = lonLeft + (lonRight - lonLeft) * u;
            
            // Check if this column is on West or East border
            bool isWestBorder = (ix == 0);
            bool isEastBorder = (ix == segments);
            
            const size_t vertexIndex = static_cast<size_t>(iy * (segments + 1) + ix);
            sampleLonDeg.push_back(lon);
            sampleLatDeg.push_back(lat);

            if (demSamplingEnabled) {
                float infN = 0.0f, infE = 0.0f, infS = 0.0f, infW = 0.0f;
                if (edgeBlendBand > 0) {
                    if (iy < edgeBlendBand) {
                        infN = static_cast<float>(edgeBlendBand - iy) / static_cast<float>(edgeBlendBand);
                    }
                    if (iy > segments - edgeBlendBand) {
                        infS = static_cast<float>(iy - (segments - edgeBlendBand)) / static_cast<float>(edgeBlendBand);
                    }
                    if (ix < edgeBlendBand) {
                        infW = static_cast<float>(edgeBlendBand - ix) / static_cast<float>(edgeBlendBand);
                    }
                    if (ix > segments - edgeBlendBand) {
                        infE = static_cast<float>(ix - (segments - edgeBlendBand)) / static_cast<float>(edgeBlendBand);
                    }
                } else {
                    if (isNorthBorder) infN = 1.0f;
                    if (isEastBorder) infE = 1.0f;
                    if (isSouthBorder) infS = 1.0f;
                    if (isWestBorder) infW = 1.0f;
                }

                float influence = std::max(std::max(infN, infS), std::max(infE, infW));
                influence = std::clamp(influence, 0.0f, 1.0f);

                const DemTileSampler* edgeSampler = &demInterior;
                // Choose the coarsest resolved edge sampler among influenced borders.
                if (influence > 0.0f) {
                    if (infN > 0.0f && demEdge[0].valid &&
                        (!edgeSampler->valid || demEdge[0].key.level < edgeSampler->key.level)) {
                        edgeSampler = &demEdge[0];
                    }
                    if (infE > 0.0f && demEdge[1].valid &&
                        (!edgeSampler->valid || demEdge[1].key.level < edgeSampler->key.level)) {
                        edgeSampler = &demEdge[1];
                    }
                    if (infS > 0.0f && demEdge[2].valid &&
                        (!edgeSampler->valid || demEdge[2].key.level < edgeSampler->key.level)) {
                        edgeSampler = &demEdge[2];
                    }
                    if (infW > 0.0f && demEdge[3].valid &&
                        (!edgeSampler->valid || demEdge[3].key.level < edgeSampler->key.level)) {
                        edgeSampler = &demEdge[3];
                    }
                }

                float hInterior = 0.0f;
                float hEdge = 0.0f;
                int srcInterior = -1;
                int srcEdge = -1;

                const bool isInteriorVertex = (influence == 0.0f);

                bool okInterior = sampleKmFrom(demInterior, lon, lat, hInterior, srcInterior);
                bool okEdge = okInterior;
                if (influence > 0.0f && edgeSampler != &demInterior) {
                    okEdge = sampleKmFrom(*edgeSampler, lon, lat, hEdge, srcEdge);
                } else {
                    hEdge = hInterior;
                    srcEdge = srcInterior;
                }

                float finalH = 0.0f;
                if (okInterior && okEdge) {
                    finalH = hInterior + (hEdge - hInterior) * influence;
                } else if (okEdge) {
                    finalH = hEdge;
                } else if (okInterior) {
                    finalH = hInterior;
                } else {
                    // No DEM data at either interior or edge-coherent level.
                    ++demMissingSamples;
                    result.demPending = true;
                    finalH = 0.0f;
                }
                heightsKm[vertexIndex] = finalH;

                if (okInterior && srcInterior >= 0) {
                    result.demUsed = true;
                    demSourceLevelMin = std::min(demSourceLevelMin, srcInterior);
                    demSourceLevelMax = std::max(demSourceLevelMax, srcInterior);
                }
                if (okEdge && srcEdge >= 0 && edgeSampler != &demInterior) {
                    result.demUsed = true;
                    demSourceLevelMin = std::min(demSourceLevelMin, srcEdge);
                    demSourceLevelMax = std::max(demSourceLevelMax, srcEdge);
                }
            }
        }
    }

    // Build cartesian positions from final sampled heights.
    for (size_t i = 0; i < heightsKm.size(); ++i) {
        glm::dvec3 pos = ellipsoid.GeodeticToCartesian(sampleLonDeg[i], sampleLatDeg[i], heightsKm[i]);
        positions.push_back(pos);
    }

    // Track final min/max height for skirt-depth and terrain stats.
    double minHeightKm = 0.0;
    double maxHeightKm = 0.0;
    if (result.demUsed && !heightsKm.empty()) {
        minHeightKm = std::numeric_limits<double>::max();
        maxHeightKm = std::numeric_limits<double>::lowest();
        for (float h : heightsKm) {
            minHeightKm = std::min(minHeightKm, static_cast<double>(h));
            maxHeightKm = std::max(maxHeightKm, static_cast<double>(h));
        }
    }
    
    // Second pass: Compute normals and build vertex buffer
    // For DEM terrain, use finite difference to compute terrain normals
    const double lonStepDeg = (segments > 0) ? ((lonRight - lonLeft) / static_cast<double>(segments)) : 0.0;
    const double mercatorYStep = (segments > 0) ? ((mercatorYBottom - mercatorYTop) / static_cast<double>(segments)) : 0.0;

    auto wrapLonDeg = [](double lonDeg) -> double {
        // Keep longitude in a stable range for TileX math.
        while (lonDeg < -180.0) lonDeg += 360.0;
        while (lonDeg > 180.0) lonDeg -= 360.0;
        return lonDeg;
    };

    // Recompute the per-vertex DEM request level used for border-normal sampling.
    // We sample one step outside the tile on border vertices so adjacent tiles compute compatible
    // derivatives. Use the edge-coherent DEM level on borders to avoid "tile grid" lighting seams.
    auto computeVertexSampleLevel = [&](int ix, int iy) -> int {
        const bool onNorth = (iy == 0);
        const bool onEast = (ix == segments);
        const bool onSouth = (iy == segments);
        const bool onWest = (ix == 0);
        if (!(onNorth || onEast || onSouth || onWest)) {
            return std::clamp(authoritativeLevel, 0, key.level);
        }

        int edgeLevel = key.level;
        if (onNorth) edgeLevel = std::min(edgeLevel, demEdgeLevels[0]);
        if (onEast) edgeLevel = std::min(edgeLevel, demEdgeLevels[1]);
        if (onSouth) edgeLevel = std::min(edgeLevel, demEdgeLevels[2]);
        if (onWest) edgeLevel = std::min(edgeLevel, demEdgeLevels[3]);
        edgeLevel = std::clamp(edgeLevel, 0, key.level);
        return edgeLevel;
    };

    // Avoid DemManager::SampleHeightDetailed() per-vertex (mutex + parent-walk).
    // Resolve a small set of DEM grid samplers per target level, then bilinear-sample locally.
    struct NormalSamplerEntry {
        int level = -1;
        DemTileSampler sampler;
    };
    std::array<NormalSamplerEntry, 8> normalSamplers;
    int normalSamplerCount = 0;

    auto getNormalSampler = [&](int lvl) -> const DemTileSampler* {
        lvl = std::clamp(lvl, 0, key.level);
        for (int i = 0; i < normalSamplerCount; ++i) {
            if (normalSamplers[i].level == lvl) {
                return &normalSamplers[i].sampler;
            }
        }
        if (normalSamplerCount >= static_cast<int>(normalSamplers.size())) {
            return demInterior.valid ? &demInterior : nullptr;
        }

        NormalSamplerEntry entry;
        entry.level = lvl;
        const TileKey desiredKey = KeyAtLevel(key, lvl);
        ResolveDemSampler(desiredKey, demManager, entry.sampler);
        normalSamplers[normalSamplerCount] = std::move(entry);
        ++normalSamplerCount;
        return &normalSamplers[normalSamplerCount - 1].sampler;
    };

    auto sampleOutsidePosition = [&](double lonDeg, double latDeg, int sampleLevel, glm::dvec3& outPos) -> bool {
        if (!demSamplingEnabled || demManager == nullptr) {
            return false;
        }
        const DemTileSampler* sampler = getNormalSampler(sampleLevel);
        if (!sampler || !sampler->valid) {
            return false;
        }
        double meters = 0.0;
        if (!SampleDemMeters(*sampler, lonDeg, latDeg, meters)) {
            return false;
        }
        float hKm = static_cast<float>(meters * 0.001 * config.demHeightScale);
        outPos = ellipsoid.GeodeticToCartesian(lonDeg, latDeg, hKm);
        return true;
    };

    for (int iy = 0; iy <= segments; ++iy) {
        float v = static_cast<float>(iy) / segments;
        
        for (int ix = 0; ix <= segments; ++ix) {
            float u = static_cast<float>(ix) / segments;
            int idx = iy * (segments + 1) + ix;
            
            glm::dvec3 pos = positions[idx];
            float heightKm = heightsKm[idx];
            glm::vec3 normal;
            
            if (result.demUsed) {
                // Compute terrain normal from neighboring positions using finite difference
                // Use one-sided difference at edges
                int ixPrev = std::max(0, ix - 1);
                int ixNext = std::min(segments, ix + 1);
                int iyPrev = std::max(0, iy - 1);
                int iyNext = std::min(segments, iy + 1);

                glm::dvec3 posLeft = positions[iy * (segments + 1) + ixPrev];
                glm::dvec3 posRight = positions[iy * (segments + 1) + ixNext];
                glm::dvec3 posUp = positions[iyPrev * (segments + 1) + ix];
                glm::dvec3 posDown = positions[iyNext * (segments + 1) + ix];

                // Border normals: sample one step outside the tile so adjacent tiles
                // compute compatible edge normals (reduces visible "tile grid" lines).
                const bool onWest = (ix == 0);
                const bool onEast = (ix == segments);
                const bool onNorth = (iy == 0);
                const bool onSouth = (iy == segments);
                if ((onWest || onEast || onNorth || onSouth) &&
                    demSamplingEnabled &&
                    demManager != nullptr &&
                    segments > 1) {
                    const int sampleLevel = computeVertexSampleLevel(ix, iy);
                    const double lonHere = sampleLonDeg[static_cast<std::size_t>(idx)];
                    const double latHere = sampleLatDeg[static_cast<std::size_t>(idx)];

                    if (onWest && std::isfinite(lonStepDeg) && std::fabs(lonStepDeg) > 0.0) {
                        glm::dvec3 outPos;
                        double lonOut = wrapLonDeg(lonHere - lonStepDeg);
                        if (sampleOutsidePosition(lonOut, latHere, sampleLevel, outPos)) {
                            posLeft = outPos;
                        }
                    } else if (onEast && std::isfinite(lonStepDeg) && std::fabs(lonStepDeg) > 0.0) {
                        glm::dvec3 outPos;
                        double lonOut = wrapLonDeg(lonHere + lonStepDeg);
                        if (sampleOutsidePosition(lonOut, latHere, sampleLevel, outPos)) {
                            posRight = outPos;
                        }
                    }

                    if (onNorth && std::isfinite(mercatorYStep) && std::fabs(mercatorYStep) > 0.0) {
                        glm::dvec3 outPos;
                        double latRadOut = 2.0 * std::atan(std::exp(mercatorYTop - mercatorYStep)) - M_PI / 2.0;
                        double latOut = latRadOut * 180.0 / M_PI;
                        if (sampleOutsidePosition(lonHere, latOut, sampleLevel, outPos)) {
                            posUp = outPos;
                        }
                    } else if (onSouth && std::isfinite(mercatorYStep) && std::fabs(mercatorYStep) > 0.0) {
                        glm::dvec3 outPos;
                        double latRadOut = 2.0 * std::atan(std::exp(mercatorYBottom + mercatorYStep)) - M_PI / 2.0;
                        double latOut = latRadOut * 180.0 / M_PI;
                        if (sampleOutsidePosition(lonHere, latOut, sampleLevel, outPos)) {
                            posDown = outPos;
                        }
                    }
                }
                
                glm::dvec3 dPdx = posRight - posLeft;
                glm::dvec3 dPdy = posDown - posUp;
                
                // Normal = cross(dPdy, dPdx) for correct winding
                glm::dvec3 terrainNormal = glm::cross(dPdy, dPdx);
                double len = glm::length(terrainNormal);
                if (len > 1e-10) {
                    terrainNormal /= len;
                } else {
                    // Fallback to ellipsoid normal
                    terrainNormal = ellipsoid.GetSurfaceNormal(pos);
                }
                normal = glm::vec3(terrainNormal);
            } else {
                // No DEM: use ellipsoid surface normal
                glm::dvec3 surfaceNormal = ellipsoid.GetSurfaceNormal(pos);
                normal = glm::vec3(surfaceNormal);
            }
            
            result.vertices.push_back(static_cast<float>(pos.x));
            result.vertices.push_back(static_cast<float>(pos.y));
            result.vertices.push_back(static_cast<float>(pos.z));
            result.vertices.push_back(normal.x);
            result.vertices.push_back(normal.y);
            result.vertices.push_back(normal.z);
            result.vertices.push_back(u);
            result.vertices.push_back(1.0f - v);  // Flip V
            result.vertices.push_back(heightKm);
        }
    }
    
    // Store height/sample diagnostics for skirt depth + continuity telemetry
    result.minHeightKm = (minHeightKm != std::numeric_limits<double>::max()) ? minHeightKm : 0.0;
    result.maxHeightKm = (maxHeightKm != std::numeric_limits<double>::lowest()) ? maxHeightKm : 0.0;
    if (!result.demUsed) {
        demSourceLevelMin = key.level;
        demSourceLevelMax = key.level;
    } else {
        if (demSourceLevelMin == std::numeric_limits<int>::max()) demSourceLevelMin = key.level;
        if (demSourceLevelMax == std::numeric_limits<int>::min()) demSourceLevelMax = key.level;
    }
    result.demSourceLevelMin = static_cast<uint8_t>(std::clamp(demSourceLevelMin, 0, 255));
    result.demSourceLevelMax = static_cast<uint8_t>(std::clamp(demSourceLevelMax, 0, 255));
    result.demMissingSamples = static_cast<uint16_t>(std::clamp(demMissingSamples, 0, 65535));
    
    if (!useSharedEBO) {
        // Indices for main grid
        for (int iy = 0; iy < segments; ++iy) {
            for (int ix = 0; ix < segments; ++ix) {
                unsigned int tl = iy * (segments + 1) + ix;
                unsigned int tr = tl + 1;
                unsigned int bl = tl + (segments + 1);
                unsigned int br = bl + 1;
                
                result.indices.push_back(tl);
                result.indices.push_back(bl);
                result.indices.push_back(tr);
                result.indices.push_back(tr);
                result.indices.push_back(bl);
                result.indices.push_back(br);
            }
        }
        result.mainIndexCount = static_cast<uint32_t>(result.indices.size());
    }
    
    // Generate skirts (GE-style seam hiding)
    double heightRange = result.maxHeightKm - result.minHeightKm;
    uint8_t effectiveSkirtMask = config.selectiveSkirts ? skirtMask : static_cast<uint8_t>(Tile::EDGE_NORTH |
                                                                                            Tile::EDGE_EAST |
                                                                                            Tile::EDGE_SOUTH |
                                                                                            Tile::EDGE_WEST);
    GenerateSkirts(result.vertices,
                   useSharedEBO ? nullptr : &result.indices,
                   segments,
                   key.level,
                   effectiveSkirtMask,
                   config,
                   heightRange);
    result.skirtMask = effectiveSkirtMask;
    result.stitchMask = config.edgeStitching ? stitchMask : 0;

    if (useSharedEBO) {
        result.mainIndexCount = MeshTemplate::GetMainIndexCount(segments, result.stitchMask);
        result.skirtIndexCount = MeshTemplate::GetSkirtIndexCount(segments, result.skirtMask);
        result.indexCount = MeshTemplate::GetIndexCount(segments, result.stitchMask, result.skirtMask);
    } else {
        if (result.mainIndexCount == 0) {
            result.mainIndexCount = MeshTemplate::GetMainIndexCount(segments);
        }
        if (result.indices.size() >= result.mainIndexCount) {
            result.skirtIndexCount = static_cast<uint32_t>(result.indices.size()) - result.mainIndexCount;
        } else {
            result.skirtIndexCount = 0;
        }
        result.indexCount = static_cast<uint32_t>(result.indices.size());
    }
    
    return result;
}

void TileMeshBuilder::GenerateSkirts(
    std::vector<float>& vertices,
    std::vector<unsigned int>* indices,
    int segments,
    int level,
    uint8_t skirtMask,
    const Config& config,
    double heightRange
) {
    const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));
    
    // Calculate skirt depth based on tile size at this zoom level
    double tileArcKm = 40075.0 / (1 << level);
    double minDepth = std::max(0.001, static_cast<double>(config.skirtDepthNearKm));
    double maxDepth = std::max(minDepth, static_cast<double>(config.skirtDepthFarKm));
    double lodT = std::clamp(tileArcKm / 2500.0, 0.0, 1.0);
    double skirtDepth = minDepth + (maxDepth - minDepth) * lodT;
    if (heightRange > 0.0) {
        skirtDepth = std::max(skirtDepth, heightRange * 0.10);
    }
    skirtDepth = std::clamp(skirtDepth, minDepth, maxDepth);
    
    // Lambda to add a skirt vertex
    auto addSkirtVertex = [&](int mainIdx) {
        float px = vertices[mainIdx * kVertexStrideFloats + 0];
        float py = vertices[mainIdx * kVertexStrideFloats + 1];
        float pz = vertices[mainIdx * kVertexStrideFloats + 2];
        float nx = vertices[mainIdx * kVertexStrideFloats + 3];
        float ny = vertices[mainIdx * kVertexStrideFloats + 4];
        float nz = vertices[mainIdx * kVertexStrideFloats + 5];
        float u = vertices[mainIdx * kVertexStrideFloats + 6];
        float v = vertices[mainIdx * kVertexStrideFloats + 7];
        float h = vertices[mainIdx * kVertexStrideFloats + 8];
        
        // Push vertex inward (toward Earth center)
        glm::vec3 pos(px, py, pz);
        glm::vec3 radialDir = glm::normalize(pos);
        glm::vec3 skirtPos = pos - radialDir * static_cast<float>(skirtDepth);
        
        vertices.push_back(skirtPos.x);
        vertices.push_back(skirtPos.y);
        vertices.push_back(skirtPos.z);
        // Copy main vertex normal to avoid visible lighting seams when skirts become exposed.
        // (Skirt geometry is a crack-hider; GE keeps it visually unobtrusive.)
        vertices.push_back(nx);
        vertices.push_back(ny);
        vertices.push_back(nz);
        vertices.push_back(u);
        vertices.push_back(v);
        vertices.push_back(h);
    };
    
    // Only generate skirt vertices for edges present in skirtMask (selective skirts).
    // Dynamic offsets track where each edge's skirt vertices begin in the VBO.
    unsigned int skirtCursor = mainVertexCount;

    unsigned int northSkirtStart = skirtCursor;
    if (skirtMask & Tile::EDGE_NORTH) {
        for (int ix = 0; ix <= segments; ++ix) {
            addSkirtVertex(ix);
        }
        skirtCursor += segments + 1;
    }

    unsigned int southSkirtStart = skirtCursor;
    if (skirtMask & Tile::EDGE_SOUTH) {
        for (int ix = 0; ix <= segments; ++ix) {
            addSkirtVertex(segments * (segments + 1) + ix);
        }
        skirtCursor += segments + 1;
    }

    unsigned int westSkirtStart = skirtCursor;
    if (skirtMask & Tile::EDGE_WEST) {
        for (int iy = 0; iy <= segments; ++iy) {
            addSkirtVertex(iy * (segments + 1));
        }
        skirtCursor += segments + 1;
    }

    unsigned int eastSkirtStart = skirtCursor;
    if (skirtMask & Tile::EDGE_EAST) {
        for (int iy = 0; iy <= segments; ++iy) {
            addSkirtVertex(iy * (segments + 1) + segments);
        }
        skirtCursor += segments + 1;
    }
    
    // Generate skirt triangles
    auto emitNorth = [&]() {
        if ((skirtMask & Tile::EDGE_NORTH) == 0) return;
        for (int i = 0; i < segments; ++i) {
            unsigned int v0 = i;
            unsigned int v1 = i + 1;
            unsigned int v2 = northSkirtStart + i;
            unsigned int v3 = northSkirtStart + i + 1;
            if (indices) {
                indices->push_back(v0); indices->push_back(v2); indices->push_back(v3);
                indices->push_back(v0); indices->push_back(v3); indices->push_back(v1);
            }
        }
    };

    auto emitSouth = [&]() {
        if ((skirtMask & Tile::EDGE_SOUTH) == 0) return;
        for (int i = 0; i < segments; ++i) {
            unsigned int v0 = segments * (segments + 1) + i;
            unsigned int v1 = segments * (segments + 1) + i + 1;
            unsigned int v2 = southSkirtStart + i;
            unsigned int v3 = southSkirtStart + i + 1;
            if (indices) {
                indices->push_back(v0); indices->push_back(v3); indices->push_back(v2);
                indices->push_back(v0); indices->push_back(v1); indices->push_back(v3);
            }
        }
    };

    auto emitWest = [&]() {
        if ((skirtMask & Tile::EDGE_WEST) == 0) return;
        for (int j = 0; j < segments; ++j) {
            unsigned int v0 = j * (segments + 1);
            unsigned int v1 = (j + 1) * (segments + 1);
            unsigned int v2 = westSkirtStart + j;
            unsigned int v3 = westSkirtStart + j + 1;
            if (indices) {
                indices->push_back(v0); indices->push_back(v3); indices->push_back(v2);
                indices->push_back(v0); indices->push_back(v1); indices->push_back(v3);
            }
        }
    };

    auto emitEast = [&]() {
        if ((skirtMask & Tile::EDGE_EAST) == 0) return;
        for (int j = 0; j < segments; ++j) {
            unsigned int v0 = j * (segments + 1) + segments;
            unsigned int v1 = (j + 1) * (segments + 1) + segments;
            unsigned int v2 = eastSkirtStart + j;
            unsigned int v3 = eastSkirtStart + j + 1;
            if (indices) {
                indices->push_back(v0); indices->push_back(v2); indices->push_back(v3);
                indices->push_back(v0); indices->push_back(v3); indices->push_back(v1);
            }
        }
    };

    emitNorth();
    emitEast();
    emitSouth();
    emitWest();
}

void TileMeshBuilder::UploadToGPU(Tile& tile, const BuildResult& result) {
    // Delete old mesh first
    DeleteMesh(tile);
    
    // Create VAO/VBO/EBO
    glGenVertexArrays(1, &tile.vao);
    glGenBuffers(1, &tile.vbo);
    if (!result.useSharedEBO) {
        glGenBuffers(1, &tile.ebo);
    }
    
    glBindVertexArray(tile.vao);
    
    glBindBuffer(GL_ARRAY_BUFFER, tile.vbo);
    glBufferData(GL_ARRAY_BUFFER, result.vertices.size() * sizeof(float), 
                 result.vertices.data(), GL_STATIC_DRAW);
    
    if (result.useSharedEBO) {
        tile.ebo = MeshTemplate::GetOrCreateEbo(result.segments, result.stitchMask, result.skirtMask);
        tile.ownsEBO = false;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.ebo);
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, result.indices.size() * sizeof(unsigned int),
                     result.indices.data(), GL_STATIC_DRAW);
        tile.ownsEBO = true;
    }
    
    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kVertexStrideFloats * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kVertexStrideFloats * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // TexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kVertexStrideFloats * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // Height (km) for CPU mesh terrain morph in shader path
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, kVertexStrideFloats * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    
    glBindVertexArray(0);
    
    tile.indexCount = result.indexCount;
    tile.mainIndexCount = result.mainIndexCount;
    tile.skirtIndexCount = result.skirtIndexCount;
    tile.hasMesh = true;
    tile.demUsed = result.demUsed;
    tile.demPending = result.demPending;
    tile.demSourceLevelMin = result.demSourceLevelMin;
    tile.demSourceLevelMax = result.demSourceLevelMax;
    tile.demMissingSamples = result.demMissingSamples;
    tile.demEffectiveLevel = result.demEffectiveLevel;
    tile.stitchMask = result.stitchMask;
    tile.skirtMask = result.skirtMask;
    tile.edgeGapMaxM = 0.0f;
    tile.edgeGapM = glm::vec4(0.0f);
    tile.seamGapMask = 0;
    tile.borderSegments = 0;
    tile.borderHeightsKm.clear();
    if (result.segments > 0) {
        const int segments = result.segments;
        const int samples = segments + 1;
        const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));
        if (result.vertices.size() >= static_cast<std::size_t>(mainVertexCount) * kVertexStrideFloats) {
            tile.borderSegments = static_cast<uint16_t>(segments);
            tile.borderHeightsKm.resize(static_cast<std::size_t>(4 * samples));
            auto readHeightKm = [&](int mainIdx) -> float {
                return result.vertices[static_cast<std::size_t>(mainIdx) * kVertexStrideFloats + 8];
            };

            const int northOffset = 0;
            const int eastOffset = samples;
            const int southOffset = 2 * samples;
            const int westOffset = 3 * samples;

            // North edge (iy=0): W->E
            for (int ix = 0; ix <= segments; ++ix) {
                tile.borderHeightsKm[static_cast<std::size_t>(northOffset + ix)] = readHeightKm(ix);
            }
            // East edge (ix=segments): N->S
            for (int iy = 0; iy <= segments; ++iy) {
                int mainIdx = iy * (segments + 1) + segments;
                tile.borderHeightsKm[static_cast<std::size_t>(eastOffset + iy)] = readHeightKm(mainIdx);
            }
            // South edge (iy=segments): W->E
            for (int ix = 0; ix <= segments; ++ix) {
                int mainIdx = segments * (segments + 1) + ix;
                tile.borderHeightsKm[static_cast<std::size_t>(southOffset + ix)] = readHeightKm(mainIdx);
            }
            // West edge (ix=0): N->S
            for (int iy = 0; iy <= segments; ++iy) {
                int mainIdx = iy * (segments + 1);
                tile.borderHeightsKm[static_cast<std::size_t>(westOffset + iy)] = readHeightKm(mainIdx);
            }
        }
    }
    tile.builtSegments = result.segments;
    tile.meshPending = false;
}

void TileMeshBuilder::DeleteMesh(Tile& tile) {
    if (tile.hasMesh) {
        if (tile.vao != 0) glDeleteVertexArrays(1, &tile.vao);
        if (tile.vbo != 0) glDeleteBuffers(1, &tile.vbo);
        if (tile.ebo != 0 && tile.ownsEBO) glDeleteBuffers(1, &tile.ebo);
        tile.vao = tile.vbo = tile.ebo = 0;
        tile.indexCount = 0;
        tile.mainIndexCount = 0;
        tile.skirtIndexCount = 0;
        tile.hasMesh = false;
        tile.ownsEBO = true;
        tile.borderSegments = 0;
        tile.borderHeightsKm.clear();
    }
}

} // namespace globe
