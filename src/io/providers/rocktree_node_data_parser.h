// RockTree NodeData Protobuf Parser
// Decodes GE NodeData mesh format
// Reference: retroplasma/earth-reverse-engineering (rocktree.proto + rocktree_decoder.h)
//
// Proto field mapping (Mesh sub-message):
//   1 = vertices        (uint8 delta-encoded, planar XYZ layout)
//   2 = texture_coords  (alternative UV source)
//   3 = indices          (varint delta-encoded triangle strip)
//   6 = texture          (JPEG/DXT1/CRN sub-message)
//   7 = texture_coordinates (primary UV source, delta+modular encoded)
//   8 = layer_and_octant_counts
//  10 = uv_offset_and_scale (4 floats, overrides computed UV quant)
//  11 = normals          (uint16 indices into for_normals palette)
//
// NodeData top-level:
//   1 = matrix_globe_from_mesh (16 doubles)
//   2 = meshes (repeated Mesh)
//   8 = for_normals (shared normal palette, octahedral encoding)

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <optional>

namespace globe {

// UV quantization parameters
// Computed by unpackTexCoords (uv_offset, uv_scale) or overridden by field 10
struct UvQuantization {
    float offsetU = 0.5f;
    float offsetV = 0.5f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    
    // Decode uint16 UV to float: outU = (u16 * scaleU) + offsetU
    void Decode(uint16_t u, uint16_t v, float& outU, float& outV) const {
        outU = static_cast<float>(u) * scaleU + offsetU;
        outV = static_cast<float>(v) * scaleV + offsetV;
    }
};

// Parsed texture data (Mesh field 6)
struct NodeDataTexture {
    std::vector<uint8_t> jpegBytes;
    int width = 0;
    int height = 0;
    bool valid = false;
};

// Parsed mesh data from NodeData protobuf
struct ParsedNodeData {
    // Transform matrix (NodeData field 1: 16 doubles, column-major)
    std::array<double, 16> transform{};
    bool hasTransform = false;
    
    // Positions: uint8 per component, delta-decoded from Mesh field 1
    // Layout: interleaved [x0,y0,z0, x1,y1,z1, ...] — V*3 values in [0,255]
    std::vector<uint8_t> positions;  // V*3 uint8 values
    int vertexCount = 0;
    
    // Texture coordinates: uint16 per component, delta+modular decoded from Mesh field 7
    // Layout: interleaved [u0,v0, u1,v1, ...] — V*2 values
    std::vector<uint16_t> texCoords;  // V*2 uint16 values
    UvQuantization uvQuant;
    bool hasTexCoords = false;
    
    // Normals: uint8 per component, decoded from NodeData field 8 + Mesh field 11
    // Layout: [nx0,ny0,nz0, nx1,ny1,nz1, ...] — V*3 values (128 = zero)
    std::vector<uint8_t> normals;  // V*3 uint8 values
    bool hasNormals = false;
    
    // Octant mask per vertex (from Mesh field 8 layer_and_octant_counts)
    std::vector<uint8_t> octants;  // V values
    
    // Layer bounds (from Mesh field 8), 10 entries
    int layerBounds[10] = {};
    bool hasLayerBounds = false;
    
    // Indices: triangle strip decoded from Mesh field 3 (zeros-val algorithm)
    std::vector<uint16_t> stripIndices;
    
    // Also converted to triangle list for backward compatibility
    std::vector<uint32_t> indices;  // Triangle list (3 * triangleCount)
    int triangleCount = 0;
    
    // Texture (Mesh field 6)
    NodeDataTexture texture;
    
    // Shared normal palette decoded from NodeData field 8 (for_normals)
    std::vector<uint8_t> forNormalsDecoded;  // count*3 uint8 values (octahedral decoded)
    int forNormalsCount = 0;
    
    // Raw bytes for fields that need deferred processing
    std::vector<uint8_t> rawNormals;        // Mesh field 11 raw bytes
    std::vector<uint8_t> rawTexCoords;      // Mesh field 7 raw bytes
    std::vector<uint8_t> rawLayerAndOctant; // Mesh field 8 raw bytes
    
    // Error state
    std::string error;
    bool success = false;
};

// Parser for NodeData protobuf format
class RockTreeNodeDataParser {
public:
    // Parse raw NodeData protobuf bytes
    static ParsedNodeData Parse(const std::vector<uint8_t>& data);
    
    // Read varint from buffer, returns bytes consumed or 0 on error
    static size_t ReadVarint(const uint8_t* data, size_t len, uint64_t& outValue);
    
    // --- Decode helpers (public for testing) ---
    
    // Decode delta-encoded uint8 vertices from Mesh field 1
    // Input: raw bytes (count*3 planar: [X deltas][Y deltas][Z deltas])
    // Output: interleaved [x0,y0,z0, ...] with cumulative delta decode
    static std::vector<uint8_t> UnpackVertices(const uint8_t* data, size_t len);
    
    // Decode delta+modular texture coordinates from Mesh field 7
    // Input: raw bytes (4-byte header + count*4 planar data)
    // Output: interleaved [u0,v0, ...] uint16 values + computed uv_offset/uv_scale
    static bool UnpackTexCoords(const uint8_t* data, size_t len, int vertexCount,
                                std::vector<uint16_t>& outUV, UvQuantization& outQuant);
    
    // Decode triangle strip indices from Mesh field 3 (zeros-val algorithm)
    // Output: triangle strip uint16 indices
    static std::vector<uint16_t> UnpackIndices(const uint8_t* data, size_t len);
    
    // Convert triangle strip to triangle list
    static bool StripToTriangleList(const std::vector<uint16_t>& strip,
                                    std::vector<uint32_t>& outTriangles);
    
    // Decode shared normal palette from NodeData field 8 (for_normals)
    // Output: count*3 uint8 values (nx,ny,nz per entry, 128=zero)
    static bool UnpackForNormals(const uint8_t* data, size_t len,
                                 std::vector<uint8_t>& outPalette, int& outCount);
    
    // Decode per-vertex normals from Mesh field 11 using the shared palette
    static bool UnpackNormals(const uint8_t* data, size_t len, int vertexCount,
                              const std::vector<uint8_t>& palette, int paletteCount,
                              std::vector<uint8_t>& outNormals);
    
private:
    // Parse wire-format protobuf helpers
    static bool ParseTopLevel(ParsedNodeData& out, const uint8_t* data, size_t len);
    static bool ParseMesh(ParsedNodeData& out, const uint8_t* data, size_t len);
    static bool ParseTexture(NodeDataTexture& out, const uint8_t* data, size_t len);
    
    // Read fixed-length fields
    static bool ReadDoubleArray(const uint8_t* data, size_t len, double* out, size_t count);
    static bool ReadFloatArray(const uint8_t* data, size_t len, float* out, size_t count);
};

} // namespace globe
