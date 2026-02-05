#include "tile_mesh_builder.h"
#include "mesh_template.h"
#include "../core/ellipsoid.h"
#include <glad/glad.h>
#include <cmath>
#include <algorithm>

namespace globe {

TileMeshBuilder::BuildResult TileMeshBuilder::Build(
    const TileKey& key,
    const Extent& inputExtent,
    uint8_t edgeMask,
    DemManager* demManager,
    const Config& config,
    bool useSharedEBO
) {
    BuildResult result;
    result.key = key;
    result.useSharedEBO = useSharedEBO;
    
    // Use more segments for terrain mesh when DEM is enabled
    const int segments = demManager ? std::max(config.meshSegments, 8) : config.meshSegments;
    const int vertexCount = (segments + 1) * (segments + 1);
    const int indexCount = segments * segments * 6;
    result.segments = segments;
    
    result.vertices.reserve(vertexCount * 8);  // pos(3) + normal(3) + uv(2)
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
    
    // Get height sampler if DEM is available
    HeightSampler heightSampler = nullptr;
    if (demManager) {
        heightSampler = demManager->GetHeightSampler();
    }
    
    result.demUsed = false;
    result.demPending = false;
    
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
            
            // Determine sample level for DEM (edge equalization)
            int sampleLevel = key.level;
            if (heightSampler && sampleLevel > 0) {
                // Use coarser level for border vertices with coarser neighbors
                bool useCoarser = false;
                if (isNorthBorder && (edgeMask & Tile::EDGE_NORTH)) useCoarser = true;
                if (isEastBorder && (edgeMask & Tile::EDGE_EAST)) useCoarser = true;
                if (isSouthBorder && (edgeMask & Tile::EDGE_SOUTH)) useCoarser = true;
                if (isWestBorder && (edgeMask & Tile::EDGE_WEST)) useCoarser = true;
                
                if (useCoarser) {
                    sampleLevel = std::max(0, sampleLevel - 1);
                }
            }
            
            // Get elevation from DEM
            double heightKm = 0.0;
            if (heightSampler) {
                double heightMeters = 0.0;
                if (heightSampler(lon, lat, sampleLevel, heightMeters)) {
                    heightKm = heightMeters * 0.001 * config.demHeightScale;
                    result.demUsed = true;
                } else {
                    result.demPending = true;
                }
            }
            
            // Use Ellipsoid for geodetic to cartesian (OpenGlobus style)
            glm::dvec3 pos = ellipsoid.GeodeticToCartesian(lon, lat, heightKm);
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);
            
            // Normal from ellipsoid surface
            glm::dvec3 surfaceNormal = ellipsoid.GetSurfaceNormal(pos);
            glm::vec3 normal = glm::vec3(surfaceNormal);
            
            result.vertices.push_back(x);
            result.vertices.push_back(y);
            result.vertices.push_back(z);
            result.vertices.push_back(normal.x);
            result.vertices.push_back(normal.y);
            result.vertices.push_back(normal.z);
            result.vertices.push_back(u);
            result.vertices.push_back(1.0f - v);  // Flip V
        }
    }
    
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
    }
    
    // Generate skirts (GE-style seam hiding)
    GenerateSkirts(result.vertices, useSharedEBO ? nullptr : &result.indices, segments, key.level);

    if (useSharedEBO) {
        result.indexCount = MeshTemplate::GetIndexCount(segments);
    } else {
        result.indexCount = static_cast<uint32_t>(result.indices.size());
    }
    
    return result;
}

void TileMeshBuilder::GenerateSkirts(
    std::vector<float>& vertices,
    std::vector<unsigned int>* indices,
    int segments,
    int level
) {
    const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));
    
    // Calculate skirt depth based on tile size at this zoom level
    double tileArcKm = 40075.0 / (1 << level);
    double skirtDepth = std::max(tileArcKm * 0.01, 0.001);  // 1% of tile or min 1m
    skirtDepth = std::min(skirtDepth, tileArcKm * 0.5);     // Max 50% of tile
    
    // Lambda to add a skirt vertex
    auto addSkirtVertex = [&](int mainIdx) {
        float px = vertices[mainIdx * 8 + 0];
        float py = vertices[mainIdx * 8 + 1];
        float pz = vertices[mainIdx * 8 + 2];
        float u = vertices[mainIdx * 8 + 6];
        float v = vertices[mainIdx * 8 + 7];
        
        // Push vertex inward (toward Earth center)
        glm::vec3 pos(px, py, pz);
        glm::vec3 radialDir = glm::normalize(pos);
        glm::vec3 skirtPos = pos - radialDir * static_cast<float>(skirtDepth);
        
        vertices.push_back(skirtPos.x);
        vertices.push_back(skirtPos.y);
        vertices.push_back(skirtPos.z);
        vertices.push_back(radialDir.x);  // Normal
        vertices.push_back(radialDir.y);
        vertices.push_back(radialDir.z);
        vertices.push_back(u);
        vertices.push_back(v);
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
    // North edge skirt
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
    
    // South edge skirt (reversed winding)
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
    
    // West edge skirt (reversed winding)
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
    
    // East edge skirt
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
        tile.ebo = MeshTemplate::GetOrCreateEbo(result.segments);
        tile.ownsEBO = false;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.ebo);
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, result.indices.size() * sizeof(unsigned int),
                     result.indices.data(), GL_STATIC_DRAW);
        tile.ownsEBO = true;
    }
    
    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // TexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
    
    tile.indexCount = result.indexCount;
    tile.hasMesh = true;
    tile.demUsed = result.demUsed;
    tile.demPending = result.demPending;
    tile.builtSegments = result.segments;
    tile.meshPending = false;
}

void TileMeshBuilder::DeleteMesh(Tile& tile) {
    if (tile.hasMesh) {
        if (tile.vao != 0) glDeleteVertexArrays(1, &tile.vao);
        if (tile.vbo != 0) glDeleteBuffers(1, &tile.vbo);
        if (tile.ebo != 0 && tile.ownsEBO) glDeleteBuffers(1, &tile.ebo);
        tile.vao = tile.vbo = tile.ebo = 0;
        tile.hasMesh = false;
        tile.ownsEBO = true;
    }
}

} // namespace globe
