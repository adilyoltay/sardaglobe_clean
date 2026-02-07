#include "tile_mesh_builder.h"
#include "mesh_template.h"
#include "../core/ellipsoid.h"
#include <glad/glad.h>
#include <cmath>
#include <algorithm>
#include <limits>

namespace globe {

namespace {

constexpr int kVertexStrideFloats = 9;  // pos(3), normal(3), uv(2), heightKm(1)

} // namespace

TileMeshBuilder::BuildResult TileMeshBuilder::Build(
    const TileKey& key,
    const Extent& inputExtent,
    uint8_t edgeMask,
    uint8_t stitchMask,
    uint8_t skirtMask,
    int demTargetLevel,
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
    
    // Use more segments for terrain mesh when DEM is enabled
    // Match segments to DEM grid: demMeshN points = demMeshN-1 segments
    const int segments = demManager ? std::max(config.meshSegments, config.demMeshN - 1) : config.meshSegments;
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
    bool demSamplingEnabled = false;
    bool hasExactDemTile = false;
    int authoritativeLevel = -1;
    if (demManager && config.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
        hasExactDemTile = demManager->HasData(key);
        if (demTargetLevel >= 0) {
            int clampedTargetLevel = std::clamp(demTargetLevel, 0, key.level);
            TileKey targetKey = key;
            while (targetKey.level > clampedTargetLevel) {
                targetKey = targetKey.Parent();
            }
            if (demManager->HasData(targetKey)) {
                authoritativeLevel = targetKey.level;
                demSamplingEnabled = true;
            }
        }

        if (!demSamplingEnabled) {
            TileKey probe = key;
            while (probe.level >= 0) {
                if (demManager->HasData(probe)) {
                    authoritativeLevel = probe.level;
                    demSamplingEnabled = true;
                    break;
                }
                if (probe.level == 0) {
                    break;
                }
                probe = probe.Parent();
            }
        }
    }
    if (authoritativeLevel < 0) {
        authoritativeLevel = key.level;
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
    bool sawUnexpectedAncestorSample = false;
    
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
                int sampleLevel = authoritativeLevel;
                bool useCoarserEdgeLevel = false;
                // Edge equalization should only be applied when sampling exact child DEM.
                if (sampleLevel == key.level && sampleLevel > 0) {
                    if (isNorthBorder && (edgeMask & Tile::EDGE_NORTH)) useCoarserEdgeLevel = true;
                    if (isEastBorder && (edgeMask & Tile::EDGE_EAST)) useCoarserEdgeLevel = true;
                    if (isSouthBorder && (edgeMask & Tile::EDGE_SOUTH)) useCoarserEdgeLevel = true;
                    if (isWestBorder && (edgeMask & Tile::EDGE_WEST)) useCoarserEdgeLevel = true;
                    if (useCoarserEdgeLevel) {
                        sampleLevel = std::max(0, sampleLevel - 1);
                    }
                }

                DemSampleResult sample;
                if (demManager->SampleHeightDetailed(lon, lat, sampleLevel, sample) && sample.ok) {
                    heightsKm[vertexIndex] =
                        static_cast<float>(sample.heightMeters * 0.001 * config.demHeightScale);
                    result.demUsed = true;
                    demSourceLevelMin = std::min(demSourceLevelMin, sample.sourceLevel);
                    demSourceLevelMax = std::max(demSourceLevelMax, sample.sourceLevel);
                    if (sample.sourceLevel < sampleLevel) {
                        int fallbackDelta = sampleLevel - sample.sourceLevel;
                        bool isBorderVertex = isNorthBorder || isEastBorder || isSouthBorder || isWestBorder;
                        bool toleratedBorderFallback = isBorderVertex && fallbackDelta <= 1;
                        // Expected mixed source levels can happen on border vertices:
                        // - explicit edge equalization (requested one level coarser)
                        // - coordinate quantization exactly on tile boundaries
                        // Interior fallbacks still indicate partial DEM availability.
                        if (!toleratedBorderFallback) {
                            sawUnexpectedAncestorSample = true;
                        }
                    }
                } else {
                    ++demMissingSamples;
                    result.demPending = true;
                }
            }
        }
    }

    // Parent-only fallback rule for partial availability:
    // Only trigger when data is incomplete (missing samples or deeper fallback than requested).
    // Mixed levels from intentional border equalization should not force full-tile downgrade.
    if (demSamplingEnabled &&
        (demMissingSamples > 0 ||
         sawUnexpectedAncestorSample)) {
        int uniformLevel = demSourceLevelMin;
        if (uniformLevel == std::numeric_limits<int>::max()) {
            uniformLevel = std::max(0, authoritativeLevel - 1);
        }

        const std::vector<float> previousHeightsKm = heightsKm;
        demSourceLevelMin = std::numeric_limits<int>::max();
        demSourceLevelMax = std::numeric_limits<int>::min();
        demMissingSamples = 0;
        result.demUsed = false;

        for (size_t i = 0; i < heightsKm.size(); ++i) {
            DemSampleResult sample;
            if (demManager->SampleHeightDetailed(sampleLonDeg[i], sampleLatDeg[i], uniformLevel, sample) && sample.ok) {
                heightsKm[i] = static_cast<float>(sample.heightMeters * 0.001 * config.demHeightScale);
                result.demUsed = true;
                demSourceLevelMin = std::min(demSourceLevelMin, sample.sourceLevel);
                demSourceLevelMax = std::max(demSourceLevelMax, sample.sourceLevel);
            } else {
                // Preserve the first-pass value instead of forcing sea-level zero.
                // Zero-injection creates visible cliffs at tile boundaries.
                heightsKm[i] = previousHeightsKm[i];
                ++demMissingSamples;
                if (std::fabs(heightsKm[i]) > 1e-6f) {
                    result.demUsed = true;
                }
            }
        }
        result.demPending = !hasExactDemTile || demMissingSamples > 0;
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

    // Recompute the per-vertex DEM request level using the same edge-equalization rule used for heights.
    // This is needed for border-normal sampling so both sides of an edge see consistent derivatives.
    auto computeVertexSampleLevel = [&](int ix, int iy) -> int {
        int sampleLevel = authoritativeLevel;
        if (sampleLevel == key.level && sampleLevel > 0) {
            bool isNorthBorder = (iy == 0);
            bool isSouthBorder = (iy == segments);
            bool isWestBorder = (ix == 0);
            bool isEastBorder = (ix == segments);
            bool useCoarserEdgeLevel = false;
            if (isNorthBorder && (edgeMask & Tile::EDGE_NORTH)) useCoarserEdgeLevel = true;
            if (isEastBorder && (edgeMask & Tile::EDGE_EAST)) useCoarserEdgeLevel = true;
            if (isSouthBorder && (edgeMask & Tile::EDGE_SOUTH)) useCoarserEdgeLevel = true;
            if (isWestBorder && (edgeMask & Tile::EDGE_WEST)) useCoarserEdgeLevel = true;
            if (useCoarserEdgeLevel) {
                sampleLevel = std::max(0, sampleLevel - 1);
            }
        }
        sampleLevel = std::clamp(sampleLevel, 0, key.level);
        return sampleLevel;
    };

    auto sampleOutsidePosition = [&](double lonDeg, double latDeg, int sampleLevel, glm::dvec3& outPos) -> bool {
        if (!demSamplingEnabled || demManager == nullptr) {
            return false;
        }
        DemSampleResult sample;
        if (!demManager->SampleHeightDetailed(lonDeg, latDeg, sampleLevel, sample) || !sample.ok) {
            return false;
        }
        float hKm = static_cast<float>(sample.heightMeters * 0.001 * config.demHeightScale);
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
    
    // Add skirt vertices for all 4 edges
    // North edge (top, iy=0)
    for (int ix = 0; ix <= segments; ++ix) {
        addSkirtVertex(ix);
    }
    unsigned int northSkirtStart = mainVertexCount;
    
    // South edge (bottom, iy=segments)
    for (int ix = 0; ix <= segments; ++ix) {
        addSkirtVertex(segments * (segments + 1) + ix);
    }
    unsigned int southSkirtStart = northSkirtStart + segments + 1;
    
    // West edge (left, ix=0)
    for (int iy = 0; iy <= segments; ++iy) {
        addSkirtVertex(iy * (segments + 1));
    }
    unsigned int westSkirtStart = southSkirtStart + segments + 1;
    
    // East edge (right, ix=segments)
    for (int iy = 0; iy <= segments; ++iy) {
        addSkirtVertex(iy * (segments + 1) + segments);
    }
    unsigned int eastSkirtStart = westSkirtStart + segments + 1;
    
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
