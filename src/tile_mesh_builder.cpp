#include "tile_mesh_builder.h"
#include <glad/glad.h>
#include <algorithm>

namespace earth {

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

glm::vec3 TileMeshBuilder::LatLonToSphere(double latRad, double lonRad, double radius) {
    double cosLat = std::cos(latRad);
    return glm::vec3(
        radius * cosLat * std::cos(lonRad),
        radius * cosLat * std::sin(lonRad),
        radius * std::sin(latRad)
    );
}

double TileMeshBuilder::Tile2Lon(int x, int z) {
    return x / static_cast<double>(1 << z) * 360.0 - 180.0;
}

double TileMeshBuilder::Tile2Lat(int y, int z) {
    double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << z);
    return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

// ============================================================================
// TILE MESH BUILDER
// ============================================================================

TileMeshBuilder::TileMeshBuilder() : config_() {}

TileMeshBuilder::TileMeshBuilder(const Config& config) : config_(config) {}

MeshData TileMeshBuilder::Build(int x, int y, int z, int edgeFlags,
                                 const HeightSampler* heightSampler) {
    MeshData data;
    // Use fixed segments (adaptive caused rendering issues)
    const int segments = config_.segments;
    const int vertsPerRow = segments + 1;
    
    // Reserve memory upfront for performance
    // Main grid + 4 skirt edges (each edge has segments+1 vertices)
    size_t mainVerts = vertsPerRow * vertsPerRow;
    size_t skirtVerts = config_.generateSkirts ? 4 * (segments + 1) : 0;
    data.vertices.reserve((mainVerts + skirtVerts) * 8); // 3 pos + 3 norm + 2 uv
    data.indices.reserve(segments * segments * 6 + (config_.generateSkirts ? segments * 4 * 6 : 0));
    
    // Generate grid vertices with DEM sampling
    GenerateGridVertices(data, x, y, z, heightSampler);
    
    // Calculate smooth normals
    CalculateNormals(data, vertsPerRow);
    
    // Apply edge stitching for LOD transitions
    if (edgeFlags != EDGE_NONE) {
        ApplyEdgeStitching(data, edgeFlags, vertsPerRow);
    }
    
    // Generate triangle indices
    GenerateIndices(data, segments);
    
    // Generate skirts to hide LOD seams
    if (config_.generateSkirts) {
        GenerateSkirts(data, z, segments);
    }
    
    return data;
}

void TileMeshBuilder::GenerateGridVertices(MeshData& data, int x, int y, int z,
                                            const HeightSampler* heightSampler) {
    const int segments = config_.segments;
    const int vertsPerRow = segments + 1;
    
    const double lonLeft = Tile2Lon(x, z);
    const double lonRight = Tile2Lon(x + 1, z);
    const double n = static_cast<double>(1 << z);
    
    data.minHeight = 1e9;
    data.maxHeight = -1e9;
    
    // Generate vertices in Mercator Y space for correct UV mapping
    for (int j = 0; j <= segments; ++j) {
        double t = static_cast<double>(j) / segments;
        
        // Interpolate in Mercator Y space (linear in tile texture coordinates)
        double mercatorY = (static_cast<double>(y) + t) / n;
        double latRad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * mercatorY)));
        double latDeg = latRad * 180.0 / M_PI;
        
        for (int i = 0; i <= segments; ++i) {
            double s = static_cast<double>(i) / segments;
            double lonDeg = lonLeft + (lonRight - lonLeft) * s;
            double lonRad = lonDeg * M_PI / 180.0;
            
            double radius = GLOBE_RADIUS;
            
            // Sample DEM for elevation
            if (heightSampler && *heightSampler) {
                double heightMeters = 0.0;
                if ((*heightSampler)(lonDeg, latDeg, z, heightMeters)) {
                    radius = GLOBE_RADIUS + heightMeters * GLOBE_RADIUS_K;
                    data.demUsed = true;
                    data.minHeight = std::min(data.minHeight, heightMeters);
                    data.maxHeight = std::max(data.maxHeight, heightMeters);
                } else {
                    data.demPending = true;
                }
            }
            
            glm::vec3 pos = LatLonToSphere(latRad, lonRad, radius);
            
            // UV coordinates
            float u = static_cast<float>(s);
            float v = 1.0f - static_cast<float>(t);
            
            // Position (normals will be calculated later)
            data.vertices.push_back(pos.x);
            data.vertices.push_back(pos.y);
            data.vertices.push_back(pos.z);
            data.vertices.push_back(0.0f); // nx placeholder
            data.vertices.push_back(0.0f); // ny placeholder
            data.vertices.push_back(1.0f); // nz placeholder
            data.vertices.push_back(u);
            data.vertices.push_back(v);
        }
    }
}

void TileMeshBuilder::CalculateNormals(MeshData& data, int vertsPerRow) {
    const int segments = vertsPerRow - 1;
    std::vector<glm::vec3> normals(vertsPerRow * vertsPerRow, glm::vec3(0.0f));
    
    // Accumulate face normals to vertices
    for (int j = 0; j < segments; ++j) {
        for (int i = 0; i < segments; ++i) {
            int i0 = j * vertsPerRow + i;
            int i1 = j * vertsPerRow + (i + 1);
            int i2 = (j + 1) * vertsPerRow + i;
            int i3 = (j + 1) * vertsPerRow + (i + 1);
            
            glm::vec3 v0(data.vertices[i0 * 8], data.vertices[i0 * 8 + 1], data.vertices[i0 * 8 + 2]);
            glm::vec3 v1(data.vertices[i1 * 8], data.vertices[i1 * 8 + 1], data.vertices[i1 * 8 + 2]);
            glm::vec3 v2(data.vertices[i2 * 8], data.vertices[i2 * 8 + 1], data.vertices[i2 * 8 + 2]);
            glm::vec3 v3(data.vertices[i3 * 8], data.vertices[i3 * 8 + 1], data.vertices[i3 * 8 + 2]);
            
            glm::vec3 n1 = glm::cross(v1 - v0, v2 - v0);
            glm::vec3 n2 = glm::cross(v2 - v1, v3 - v1);
            
            normals[i0] += n1;
            normals[i1] += n1 + n2;
            normals[i2] += n1 + n2;
            normals[i3] += n2;
        }
    }
    
    // Normalize and write back
    for (int idx = 0; idx < vertsPerRow * vertsPerRow; ++idx) {
        glm::vec3 n = glm::normalize(normals[idx]);
        data.vertices[idx * 8 + 3] = n.x;
        data.vertices[idx * 8 + 4] = n.y;
        data.vertices[idx * 8 + 5] = n.z;
    }
}

void TileMeshBuilder::ApplyEdgeStitching(MeshData& data, int edgeFlags, int vertsPerRow) {
    const int segments = vertsPerRow - 1;
    
    auto snapVertex = [&](int targetIdx, int src1Idx, int src2Idx) {
        for (int k = 0; k < 3; ++k) {
            data.vertices[targetIdx * 8 + k] = 
                0.5f * (data.vertices[src1Idx * 8 + k] + data.vertices[src2Idx * 8 + k]);
        }
    };
    
    // Snap odd vertices to midpoint of neighbors for half-resolution edge
    if (edgeFlags & EDGE_LEFT) {
        for (int j = 1; j < segments; j += 2) {
            snapVertex(j * vertsPerRow, (j - 1) * vertsPerRow, (j + 1) * vertsPerRow);
        }
    }
    if (edgeFlags & EDGE_RIGHT) {
        int col = segments;
        for (int j = 1; j < segments; j += 2) {
            snapVertex(j * vertsPerRow + col, (j - 1) * vertsPerRow + col, (j + 1) * vertsPerRow + col);
        }
    }
    if (edgeFlags & EDGE_TOP) {
        for (int i = 1; i < segments; i += 2) {
            snapVertex(i, i - 1, i + 1);
        }
    }
    if (edgeFlags & EDGE_BOTTOM) {
        for (int i = 1; i < segments; i += 2) {
            snapVertex(segments * vertsPerRow + i, 
                       segments * vertsPerRow + i - 1, 
                       segments * vertsPerRow + i + 1);
        }
    }
}

void TileMeshBuilder::GenerateIndices(MeshData& data, int segments) {
    const int vertsPerRow = segments + 1;
    
    for (int j = 0; j < segments; ++j) {
        for (int i = 0; i < segments; ++i) {
            unsigned int row1 = j * vertsPerRow;
            unsigned int row2 = (j + 1) * vertsPerRow;
            
            // Two triangles per quad
            data.indices.push_back(row1 + i);
            data.indices.push_back(row2 + i);
            data.indices.push_back(row2 + i + 1);
            
            data.indices.push_back(row1 + i);
            data.indices.push_back(row2 + i + 1);
            data.indices.push_back(row1 + i + 1);
        }
    }
}

void TileMeshBuilder::GenerateSkirts(MeshData& data, int z, int segments) {
    const int vertsPerRow = segments + 1;
    
    // Calculate skirt depth based on tile size (Google Earth style)
    const double tileAngularSize = M_PI / (1 << z);
    const double tileArcLength = GLOBE_RADIUS * tileAngularSize;
    const double skirtRaw = tileArcLength * config_.skirtDepthPercent;
    const double skirtFloor = config_.skirtMinKm;
    const double skirtCeil = std::max(tileArcLength * config_.skirtMaxPercent, skirtFloor);
    const float skirtDepth = static_cast<float>(std::clamp(skirtRaw, skirtFloor, skirtCeil));
    
    const unsigned int mainVertexCount = static_cast<unsigned int>(data.vertices.size() / 8);
    
    auto addSkirtVertex = [&](int mainIdx) {
        float px = data.vertices[mainIdx * 8 + 0];
        float py = data.vertices[mainIdx * 8 + 1];
        float pz = data.vertices[mainIdx * 8 + 2];
        float u = data.vertices[mainIdx * 8 + 6];
        float v = data.vertices[mainIdx * 8 + 7];
        
        // Push vertex inward toward globe center
        glm::vec3 pos(px, py, pz);
        glm::vec3 radialDir = glm::normalize(pos);
        glm::vec3 skirtPos = pos - radialDir * skirtDepth;
        
        // Use radial direction as normal for skirt faces
        data.vertices.push_back(skirtPos.x);
        data.vertices.push_back(skirtPos.y);
        data.vertices.push_back(skirtPos.z);
        data.vertices.push_back(radialDir.x);
        data.vertices.push_back(radialDir.y);
        data.vertices.push_back(radialDir.z);
        data.vertices.push_back(u);
        data.vertices.push_back(v);
    };
    
    // Add skirt vertices for all 4 edges
    // Top edge
    for (int i = 0; i <= segments; ++i) {
        addSkirtVertex(i);
    }
    unsigned int topSkirtStart = mainVertexCount;
    
    // Bottom edge
    for (int i = 0; i <= segments; ++i) {
        addSkirtVertex(segments * vertsPerRow + i);
    }
    unsigned int bottomSkirtStart = topSkirtStart + segments + 1;
    
    // Left edge
    for (int j = 0; j <= segments; ++j) {
        addSkirtVertex(j * vertsPerRow);
    }
    unsigned int leftSkirtStart = bottomSkirtStart + segments + 1;
    
    // Right edge
    for (int j = 0; j <= segments; ++j) {
        addSkirtVertex(j * vertsPerRow + segments);
    }
    unsigned int rightSkirtStart = leftSkirtStart + segments + 1;
    
    // Generate skirt triangles
    // Top edge skirt
    for (int i = 0; i < segments; ++i) {
        unsigned int v0 = i;
        unsigned int v1 = i + 1;
        unsigned int v2 = topSkirtStart + i;
        unsigned int v3 = topSkirtStart + i + 1;
        data.indices.push_back(v0); data.indices.push_back(v2); data.indices.push_back(v3);
        data.indices.push_back(v0); data.indices.push_back(v3); data.indices.push_back(v1);
    }
    
    // Bottom edge skirt (reversed winding)
    for (int i = 0; i < segments; ++i) {
        unsigned int v0 = segments * vertsPerRow + i;
        unsigned int v1 = segments * vertsPerRow + i + 1;
        unsigned int v2 = bottomSkirtStart + i;
        unsigned int v3 = bottomSkirtStart + i + 1;
        data.indices.push_back(v0); data.indices.push_back(v3); data.indices.push_back(v2);
        data.indices.push_back(v0); data.indices.push_back(v1); data.indices.push_back(v3);
    }
    
    // Left edge skirt (reversed winding)
    for (int j = 0; j < segments; ++j) {
        unsigned int v0 = j * vertsPerRow;
        unsigned int v1 = (j + 1) * vertsPerRow;
        unsigned int v2 = leftSkirtStart + j;
        unsigned int v3 = leftSkirtStart + j + 1;
        data.indices.push_back(v0); data.indices.push_back(v3); data.indices.push_back(v2);
        data.indices.push_back(v0); data.indices.push_back(v1); data.indices.push_back(v3);
    }
    
    // Right edge skirt
    for (int j = 0; j < segments; ++j) {
        unsigned int v0 = j * vertsPerRow + segments;
        unsigned int v1 = (j + 1) * vertsPerRow + segments;
        unsigned int v2 = rightSkirtStart + j;
        unsigned int v3 = rightSkirtStart + j + 1;
        data.indices.push_back(v0); data.indices.push_back(v2); data.indices.push_back(v3);
        data.indices.push_back(v0); data.indices.push_back(v3); data.indices.push_back(v1);
    }
}

TileMesh TileMeshBuilder::UploadToGPU(const MeshData& data) {
    TileMesh mesh;
    
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    
    glBindVertexArray(mesh.vao);
    
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, 
                 data.vertices.size() * sizeof(float),
                 data.vertices.data(), 
                 GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 data.indices.size() * sizeof(unsigned int),
                 data.indices.data(),
                 GL_STATIC_DRAW);
    
    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    
    // Normal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    
    // UV (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    
    glBindVertexArray(0);
    
    mesh.indexCount = static_cast<GLsizei>(data.indices.size());
    
    return mesh;
}

void TileMeshBuilder::DeleteMesh(TileMesh& mesh) {
    if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
    mesh = TileMesh();
}

// ============================================================================
// POLE MESH BUILDER
// ============================================================================

MeshData PoleMeshBuilder::BuildNorthPole(int segments) {
    MeshData data;
    const double poleLatRad = 85.051128 * M_PI / 180.0; // Web Mercator limit
    
    // Center vertex at pole
    glm::vec3 center = TileMeshBuilder::LatLonToSphere(M_PI / 2.0, 0.0, GLOBE_RADIUS);
    data.vertices.push_back(center.x);
    data.vertices.push_back(center.y);
    data.vertices.push_back(center.z);
    data.vertices.push_back(0.0f);
    data.vertices.push_back(0.0f);
    data.vertices.push_back(1.0f);
    data.vertices.push_back(0.5f);
    data.vertices.push_back(0.5f);
    
    // Ring vertices
    for (int i = 0; i <= segments; ++i) {
        double lon = 2.0 * M_PI * i / segments;
        glm::vec3 pos = TileMeshBuilder::LatLonToSphere(poleLatRad, lon, GLOBE_RADIUS);
        glm::vec3 normal = glm::normalize(pos);
        
        data.vertices.push_back(pos.x);
        data.vertices.push_back(pos.y);
        data.vertices.push_back(pos.z);
        data.vertices.push_back(normal.x);
        data.vertices.push_back(normal.y);
        data.vertices.push_back(normal.z);
        data.vertices.push_back(0.5f + 0.5f * std::cos(lon));
        data.vertices.push_back(0.5f + 0.5f * std::sin(lon));
    }
    
    // Fan triangles
    for (int i = 0; i < segments; ++i) {
        data.indices.push_back(0);
        data.indices.push_back(i + 1);
        data.indices.push_back(i + 2);
    }
    
    return data;
}

MeshData PoleMeshBuilder::BuildSouthPole(int segments) {
    MeshData data;
    const double poleLatRad = -85.051128 * M_PI / 180.0;
    
    // Center vertex at pole
    glm::vec3 center = TileMeshBuilder::LatLonToSphere(-M_PI / 2.0, 0.0, GLOBE_RADIUS);
    data.vertices.push_back(center.x);
    data.vertices.push_back(center.y);
    data.vertices.push_back(center.z);
    data.vertices.push_back(0.0f);
    data.vertices.push_back(0.0f);
    data.vertices.push_back(-1.0f);
    data.vertices.push_back(0.5f);
    data.vertices.push_back(0.5f);
    
    // Ring vertices
    for (int i = 0; i <= segments; ++i) {
        double lon = 2.0 * M_PI * i / segments;
        glm::vec3 pos = TileMeshBuilder::LatLonToSphere(poleLatRad, lon, GLOBE_RADIUS);
        glm::vec3 normal = glm::normalize(pos);
        
        data.vertices.push_back(pos.x);
        data.vertices.push_back(pos.y);
        data.vertices.push_back(pos.z);
        data.vertices.push_back(normal.x);
        data.vertices.push_back(normal.y);
        data.vertices.push_back(normal.z);
        data.vertices.push_back(0.5f + 0.5f * std::cos(lon));
        data.vertices.push_back(0.5f + 0.5f * std::sin(lon));
    }
    
    // Fan triangles (reversed winding for south pole)
    for (int i = 0; i < segments; ++i) {
        data.indices.push_back(0);
        data.indices.push_back(i + 2);
        data.indices.push_back(i + 1);
    }
    
    return data;
}

PoleMesh PoleMeshBuilder::UploadToGPU(const MeshData& data) {
    PoleMesh mesh;
    TileMesh tileMesh = TileMeshBuilder::UploadToGPU(data);
    mesh.vao = tileMesh.vao;
    mesh.vbo = tileMesh.vbo;
    mesh.ebo = tileMesh.ebo;
    mesh.indexCount = tileMesh.indexCount;
    mesh.initialized = true;
    return mesh;
}

} // namespace earth
