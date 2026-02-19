// RockTree NodeData Protobuf Parser Implementation
// Reference: retroplasma/earth-reverse-engineering (rocktree_decoder.h, rocktree_ex.h)

#include "rocktree_node_data_parser.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace globe {

// Protobuf wire types
enum class WireType : uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    StartGroup = 3,
    EndGroup = 4,
    Fixed32 = 5
};

static size_t ParseFieldKeyVarint(const uint8_t* data, size_t len, uint32_t& outFieldNum, WireType& outType) {
    uint64_t key;
    size_t bytes = RockTreeNodeDataParser::ReadVarint(data, len, key);
    if (bytes == 0) return 0;
    outFieldNum = static_cast<uint32_t>(key >> 3);
    outType = static_cast<WireType>(key & 0x07);
    if (outFieldNum == 0) return 0;
    return bytes;
}

namespace {
    using namespace globe;
    
    bool SkipUnknownField(const uint8_t* data, size_t& pos, size_t len, WireType wireType) {
        switch (wireType) {
            case WireType::Varint: {
                uint64_t value;
                size_t vb = RockTreeNodeDataParser::ReadVarint(data + pos, len - pos, value);
                if (vb == 0) return false;
                pos += vb;
                return true;
            }
            case WireType::Fixed64: {
                if (pos + 8 > len) return false;
                pos += 8;
                return true;
            }
            case WireType::LengthDelimited: {
                uint64_t length;
                size_t vb = RockTreeNodeDataParser::ReadVarint(data + pos, len - pos, length);
                if (vb == 0) return false;
                pos += vb + length;
                return pos <= len;
            }
            case WireType::Fixed32: {
                if (pos + 4 > len) return false;
                pos += 4;
                return true;
            }
            default:
                return false;
        }
    }
    
    // Read a varint from raw bytes at offset, returns bytes consumed
    int ReadVarIntRaw(const uint8_t* data, size_t len, int offset) {
        int c = 0, d = 1;
        int pos = offset;
        int e;
        do {
            if (pos >= static_cast<int>(len)) return -1;
            e = data[pos++];
            c += (e & 0x7F) * d;
            d <<= 7;
        } while (e & 0x80);
        return c;
    }
    
    // Read varint and advance offset, returns the value. Sets offset to -1 on error.
    int ReadVarIntAdv(const uint8_t* data, size_t len, int& offset) {
        if (offset < 0 || offset >= static_cast<int>(len)) { offset = -1; return 0; }
        int c = 0, d = 1;
        int e;
        do {
            if (offset >= static_cast<int>(len)) { offset = -1; return 0; }
            e = data[offset++];
            c += (e & 0x7F) * d;
            d <<= 7;
        } while (e & 0x80);
        return c;
    }
}

// ============================================================================
// Public decode helpers (matching retroplasma/earth-reverse-engineering)
// ============================================================================

std::vector<uint8_t> RockTreeNodeDataParser::UnpackVertices(const uint8_t* data, size_t len) {
    // Vertex data is planar delta-encoded uint8: [X deltas][Y deltas][Z deltas]
    // Each component stream has count bytes; total = count * 3
    if (len < 3 || len % 3 != 0) return {};
    
    size_t count = len / 3;
    std::vector<uint8_t> out(count * 3);  // interleaved x,y,z
    
    uint8_t x = 0, y = 0, z = 0;
    for (size_t i = 0; i < count; i++) {
        x += data[count * 0 + i];  // delta decode X
        y += data[count * 1 + i];  // delta decode Y
        z += data[count * 2 + i];  // delta decode Z
        out[i * 3 + 0] = x;
        out[i * 3 + 1] = y;
        out[i * 3 + 2] = z;
    }
    
    return out;
}

bool RockTreeNodeDataParser::UnpackTexCoords(const uint8_t* data, size_t len, int vertexCount,
                                              std::vector<uint16_t>& outUV, UvQuantization& outQuant) {
    // Format: 4-byte header (uint16 u_mod-1, uint16 v_mod-1) + count*4 planar data
    // Data layout: [u_lo bytes][v_lo bytes][u_hi bytes][v_hi bytes]
    if (vertexCount <= 0) return false;
    size_t count = static_cast<size_t>(vertexCount);
    if (len < 4 + count * 4) return false;
    
    uint16_t u_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 0);
    uint16_t v_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 2);
    const uint8_t* d = data + 4;
    
    outUV.resize(count * 2);
    int u = 0, v = 0;
    for (size_t i = 0; i < count; i++) {
        u = (u + d[count * 0 + i] + (d[count * 2 + i] << 8)) % u_mod;
        v = (v + d[count * 1 + i] + (d[count * 3 + i] << 8)) % v_mod;
        outUV[i * 2 + 0] = static_cast<uint16_t>(u);
        outUV[i * 2 + 1] = static_cast<uint16_t>(v);
    }
    
    // Default UV quant from modular decode
    outQuant.offsetU = 0.5f;
    outQuant.offsetV = 0.5f;
    outQuant.scaleU = 1.0f / static_cast<float>(u_mod);
    outQuant.scaleV = 1.0f / static_cast<float>(v_mod);
    
    return true;
}

std::vector<uint16_t> RockTreeNodeDataParser::UnpackIndices(const uint8_t* data, size_t len) {
    // retroplasma zeros-val algorithm:
    // First varint = strip length
    // Then strip_length varints: triangle_strip[i] = (a=b, b=c, c=zeros-val)
    // if val == 0 then zeros++
    int offset = 0;
    int stripLen = ReadVarIntAdv(data, len, offset);
    if (offset < 0 || stripLen <= 0) return {};
    
    std::vector<uint16_t> strip(stripLen);
    int zeros = 0;
    int a = 0, b = 0, c = 0;
    
    for (int i = 0; i < stripLen; i++) {
        int val = ReadVarIntAdv(data, len, offset);
        if (offset < 0) { strip.resize(i); break; }
        a = b;
        b = c;
        c = zeros - val;
        strip[i] = static_cast<uint16_t>(c);
        if (val == 0) zeros++;
    }
    
    return strip;
}

bool RockTreeNodeDataParser::StripToTriangleList(const std::vector<uint16_t>& strip,
                                                  std::vector<uint32_t>& outTriangles) {
    outTriangles.clear();
    if (strip.size() < 3) return true;
    
    // Standard triangle strip to triangle list conversion
    // Skip degenerate triangles (where any two indices are equal)
    for (size_t i = 2; i < strip.size(); i++) {
        uint16_t a = strip[i - 2];
        uint16_t b = strip[i - 1];
        uint16_t c = strip[i];
        
        // Skip degenerate triangles
        if (a == b || a == c || b == c) continue;
        
        if (i % 2 == 0) {
            outTriangles.push_back(a);
            outTriangles.push_back(b);
            outTriangles.push_back(c);
        } else {
            // Flip winding for odd triangles
            outTriangles.push_back(a);
            outTriangles.push_back(c);
            outTriangles.push_back(b);
        }
    }
    
    return true;
}

bool RockTreeNodeDataParser::UnpackForNormals(const uint8_t* data, size_t len,
                                               std::vector<uint8_t>& outPalette, int& outCount) {
    // Format: uint16 count + uint8 scale + count*2 octahedral-encoded normal pairs
    if (len < 3) return false;
    
    uint16_t count = *reinterpret_cast<const uint16_t*>(data);
    if (count * 2 != static_cast<int>(len) - 3) return false;
    int s = data[2];
    const uint8_t* d = data + 3;
    
    outCount = count;
    outPalette.resize(3 * count);
    
    // f1: expand low-bit value to 8-bit range
    auto f1 = [](int v, int l) -> int {
        if (4 >= l)
            return (v << l) + (v & ((1 << l) - 1));
        if (6 >= l) {
            int r = 8 - l;
            return (v << l) + ((v << l) >> r) + ((v << l) >> r >> r) + ((v << l) >> r >> r >> r);
        }
        return -(v & 1);
    };
    
    // f2: clamp to [0, 255]
    auto f2 = [](double c) -> int {
        int cr = static_cast<int>(std::round(c));
        if (cr < 0) return 0;
        if (cr > 255) return 255;
        return cr;
    };
    
    for (int i = 0; i < count; i++) {
        double aa = f1(d[0 + i], s) / 255.0;
        double ff = f1(d[count + i], s) / 255.0;
        
        double bb = aa, cc = ff;
        double g = bb + cc, h = bb - cc;
        int sign = 1;
        
        if (!(.5 <= g && 1.5 >= g && -.5 <= h && .5 >= h)) {
            sign = -1;
            if (.5 >= g) {
                bb = .5 - ff;
                cc = .5 - aa;
            } else if (1.5 <= g) {
                bb = 1.5 - ff;
                cc = 1.5 - aa;
            } else if (-.5 >= h) {
                bb = ff - .5;
                cc = aa + .5;
            } else {
                bb = ff + .5;
                cc = aa - .5;
            }
            g = bb + cc;
            h = bb - cc;
        }
        
        double na = std::fmin(std::fmin(2*g - 1, 3 - 2*g), std::fmin(2*h + 1, 1 - 2*h)) * sign;
        double nb = 2 * bb - 1;
        double nc = 2 * cc - 1;
        double m = 127.0 / std::sqrt(na*na + nb*nb + nc*nc);
        
        outPalette[3*i + 0] = static_cast<uint8_t>(f2(m * na + 127));
        outPalette[3*i + 1] = static_cast<uint8_t>(f2(m * nb + 127));
        outPalette[3*i + 2] = static_cast<uint8_t>(f2(m * nc + 127));
    }
    
    return true;
}

bool RockTreeNodeDataParser::UnpackNormals(const uint8_t* data, size_t len, int vertexCount,
                                            const std::vector<uint8_t>& palette, int paletteCount,
                                            std::vector<uint8_t>& outNormals) {
    // Mesh normals field: uint16 indices (low byte + high byte, split)
    // Format: count = len/2, for each vertex: index = data[i] + (data[count+i] << 8)
    if (len == 0 || palette.empty()) {
        // No normals data — fill with default (127,127,127)
        outNormals.resize(vertexCount * 3, 127);
        return true;
    }
    
    int count = static_cast<int>(len / 2);
    outNormals.resize(vertexCount * 3);
    
    const uint8_t* input = data;
    for (int i = 0; i < count && i < vertexCount; ++i) {
        int j = input[i] + (input[count + i] << 8);
        if (j >= 0 && 3*j + 2 < static_cast<int>(palette.size())) {
            outNormals[3*i + 0] = palette[3*j + 0];
            outNormals[3*i + 1] = palette[3*j + 1];
            outNormals[3*i + 2] = palette[3*j + 2];
        } else {
            outNormals[3*i + 0] = 127;
            outNormals[3*i + 1] = 127;
            outNormals[3*i + 2] = 127;
        }
    }
    
    // Fill remaining vertices with default if count < vertexCount
    for (int i = count; i < vertexCount; ++i) {
        outNormals[3*i + 0] = 127;
        outNormals[3*i + 1] = 127;
        outNormals[3*i + 2] = 127;
    }
    
    return true;
}

// ============================================================================
// Main parse entry point
// ============================================================================

ParsedNodeData RockTreeNodeDataParser::Parse(const std::vector<uint8_t>& data) {
    ParsedNodeData result;
    if (!ParseTopLevel(result, data.data(), data.size())) {
        if (result.error.empty()) {
            result.error = "Failed to parse NodeData";
        }
        return result;
    }
    result.success = true;
    return result;
}

// ============================================================================
// Top-level NodeData parsing
// ============================================================================

bool RockTreeNodeDataParser::ParseTopLevel(ParsedNodeData& out, const uint8_t* data, size_t len) {
    size_t pos = 0;
    bool firstMeshParsed = false;
    
    while (pos < len) {
        uint32_t fieldNum;
        WireType wireType;
        size_t keyBytes = ParseFieldKeyVarint(data + pos, len - pos, fieldNum, wireType);
        if (keyBytes == 0) {
            out.error = "Invalid field key at position " + std::to_string(pos);
            return false;
        }
        pos += keyBytes;
        
        size_t fieldStart = pos;
        
        if (wireType == WireType::LengthDelimited) {
            uint64_t length;
            size_t varintBytes = ReadVarint(data + pos, len - pos, length);
            if (varintBytes == 0) {
                out.error = "Failed to read length at position " + std::to_string(pos);
                return false;
            }
            pos += varintBytes;
            
            if (pos + length > len) {
                out.error = "Field length exceeds buffer";
                return false;
            }
            
            if (fieldNum == 1) {
                // NodeData field 1: matrix_globe_from_mesh (16 doubles = 128 bytes)
                if (length != 128) {
                    out.error = "Transform matrix must be 128 bytes, got " + std::to_string(length);
                    return false;
                }
                if (!ReadDoubleArray(data + pos, length, out.transform.data(), 16)) {
                    out.error = "Failed to read transform matrix";
                    return false;
                }
                out.hasTransform = true;
            } else if (fieldNum == 2 && !firstMeshParsed) {
                // NodeData field 2: meshes (repeated Mesh) — parse first mesh only
                if (!ParseMesh(out, data + pos, length)) {
                    return false;
                }
                firstMeshParsed = true;
            } else if (fieldNum == 8) {
                // NodeData field 8: for_normals (shared normal palette)
                UnpackForNormals(data + pos, length, out.forNormalsDecoded, out.forNormalsCount);
            }
            
            pos += length;
        } else {
            if (!SkipUnknownField(data, pos, len, wireType)) {
                out.error = "Failed to skip field at position " + std::to_string(fieldStart);
                return false;
            }
        }
    }
    
    if (!out.hasTransform) {
        out.error = "Missing required transform matrix";
        return false;
    }
    
    // Post-processing: decode raw fields that depend on vertex count
    
    // Decode texture coordinates from raw field 7 data
    if (!out.rawTexCoords.empty() && out.vertexCount > 0) {
        if (UnpackTexCoords(out.rawTexCoords.data(), out.rawTexCoords.size(),
                            out.vertexCount, out.texCoords, out.uvQuant)) {
            out.hasTexCoords = true;
        }
    }
    
    // Decode normals from raw field 11 using for_normals palette
    if (!out.rawNormals.empty() && out.vertexCount > 0) {
        if (UnpackNormals(out.rawNormals.data(), out.rawNormals.size(),
                          out.vertexCount, out.forNormalsDecoded, out.forNormalsCount,
                          out.normals)) {
            out.hasNormals = true;
        }
    }
    
    return true;
}

// ============================================================================
// Mesh sub-message parsing (handles first mesh in repeated field 2)
// ============================================================================

bool RockTreeNodeDataParser::ParseMesh(ParsedNodeData& out, const uint8_t* data, size_t len) {
    size_t pos = 0;
    bool hasUvOffsetAndScale = false;
    float uvOffsetAndScale[4] = {};
    
    while (pos < len) {
        uint32_t fieldNum;
        WireType wireType;
        size_t keyBytes = ParseFieldKeyVarint(data + pos, len - pos, fieldNum, wireType);
        if (keyBytes == 0) {
            out.error = "Invalid field key in mesh at " + std::to_string(pos);
            return false;
        }
        pos += keyBytes;
        
        size_t fieldStart = pos;
        
        if (wireType == WireType::LengthDelimited) {
            uint64_t length;
            size_t varintBytes = ReadVarint(data + pos, len - pos, length);
            if (varintBytes == 0) {
                out.error = "Failed to read length in mesh";
                return false;
            }
            pos += varintBytes;
            
            if (pos + length > len) {
                out.error = "Mesh field length exceeds buffer";
                return false;
            }
            
            if (fieldNum == 1) {
                // Mesh field 1: vertices (uint8 delta-encoded, planar)
                const size_t MAX_VERTEX_BYTES = 30'000'000;
                if (length > MAX_VERTEX_BYTES) {
                    out.error = "Vertex data too large: " + std::to_string(length) + " bytes";
                    return false;
                }
                out.positions = UnpackVertices(data + pos, length);
                out.vertexCount = static_cast<int>(out.positions.size()) / 3;
            } else if (fieldNum == 3) {
                // Mesh field 3: indices (varint-encoded triangle strip)
                out.stripIndices = UnpackIndices(data + pos, length);
                
                // Convert to triangle list for backward compatibility
                if (!StripToTriangleList(out.stripIndices, out.indices)) {
                    out.error = "Failed to convert triangle strip to list";
                    return false;
                }
                out.triangleCount = static_cast<int>(out.indices.size()) / 3;
            } else if (fieldNum == 6) {
                // Mesh field 6: texture
                if (!ParseTexture(out.texture, data + pos, length)) {
                    out.texture.valid = false;
                }
            } else if (fieldNum == 7) {
                // Mesh field 7: texture_coordinates (primary UV source)
                // Store raw bytes — decoded in post-processing after vertex count is known
                out.rawTexCoords.assign(data + pos, data + pos + length);
            } else if (fieldNum == 8) {
                // Mesh field 8: layer_and_octant_counts
                out.rawLayerAndOctant.assign(data + pos, data + pos + length);
            } else if (fieldNum == 10) {
                // Mesh field 10: uv_offset_and_scale (4 floats, packed repeated)
                // This is a packed repeated float field
                if (length >= 16) {
                    if (ReadFloatArray(data + pos, length, uvOffsetAndScale, 4)) {
                        hasUvOffsetAndScale = true;
                    }
                }
            } else if (fieldNum == 11) {
                // Mesh field 11: normals (uint16 indices into for_normals palette)
                out.rawNormals.assign(data + pos, data + pos + length);
            }
            
            pos += length;
        } else {
            if (!SkipUnknownField(data, pos, len, wireType)) {
                out.error = "Failed to skip field in mesh at " + std::to_string(fieldStart);
                return false;
            }
        }
    }
    
    // Apply UV offset/scale override from field 10 if present
    // (retroplasma: if uv_offset_and_scale_size() == 4, override the computed values)
    if (hasUvOffsetAndScale) {
        out.uvQuant.offsetU = uvOffsetAndScale[0];
        out.uvQuant.offsetV = uvOffsetAndScale[1];
        out.uvQuant.scaleU = uvOffsetAndScale[2];
        out.uvQuant.scaleV = uvOffsetAndScale[3];
    } else if (out.hasTexCoords) {
        // retroplasma fallback: uv_offset[1] -= 1/uv_scale[1]; uv_scale[1] *= -1
        out.uvQuant.offsetV -= 1.0f / out.uvQuant.scaleV;
        out.uvQuant.scaleV *= -1.0f;
    }
    
    // Vertex count DoS limit
    const int MAX_VERTEX_COUNT = 10'000'000;
    if (out.vertexCount > MAX_VERTEX_COUNT) {
        out.error = "Vertex count exceeds limit: " + std::to_string(out.vertexCount);
        return false;
    }
    
    // Index consistency check
    if (!out.indices.empty() && out.vertexCount > 0) {
        uint32_t maxIndex = 0;
        for (uint32_t idx : out.indices) {
            maxIndex = std::max(maxIndex, idx);
        }
        if (maxIndex >= static_cast<uint32_t>(out.vertexCount)) {
            out.error = "Index value out of range: max=" + std::to_string(maxIndex) + 
                       " vs vertices=" + std::to_string(out.vertexCount);
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Texture sub-message parsing (unchanged)
// ============================================================================

bool RockTreeNodeDataParser::ParseTexture(NodeDataTexture& out, const uint8_t* data, size_t len) {
    size_t pos = 0;
    out.valid = false;
    out.width = 0;
    out.height = 0;
    
    while (pos < len) {
        uint32_t fieldNum;
        WireType wireType;
        size_t keyBytes = ParseFieldKeyVarint(data + pos, len - pos, fieldNum, wireType);
        if (keyBytes == 0) break;
        pos += keyBytes;
        
        if (wireType == WireType::LengthDelimited) {
            uint64_t length;
            size_t varintBytes = ReadVarint(data + pos, len - pos, length);
            if (varintBytes == 0) break;
            pos += varintBytes;
            
            if (pos + length > len) break;
            
            if (fieldNum == 1) {
                out.jpegBytes.resize(length);
                memcpy(out.jpegBytes.data(), data + pos, length);
                out.valid = true;
            }
            pos += length;
        } else if (wireType == WireType::Varint) {
            uint64_t value;
            size_t vb = ReadVarint(data + pos, len - pos, value);
            if (vb == 0) break;
            pos += vb;
            
            if (fieldNum == 3) {
                out.width = static_cast<int>(value);
            } else if (fieldNum == 4) {
                out.height = static_cast<int>(value);
            }
        } else {
            if (!SkipUnknownField(data, pos, len, wireType)) break;
        }
    }
    
    return out.valid;
}

// ============================================================================
// Low-level helpers
// ============================================================================

size_t RockTreeNodeDataParser::ReadVarint(const uint8_t* data, size_t len, uint64_t& outValue) {
    outValue = 0;
    size_t shift = 0;
    size_t pos = 0;
    
    while (pos < len && pos < 10) {
        uint8_t byte = data[pos];
        outValue |= static_cast<uint64_t>(byte & 0x7F) << shift;
        pos++;
        if ((byte & 0x80) == 0) {
            return pos;
        }
        shift += 7;
        if (shift >= 64) break;
    }
    
    return 0;
}

bool RockTreeNodeDataParser::ReadDoubleArray(const uint8_t* data, size_t len, double* out, size_t count) {
    if (len < count * sizeof(double)) return false;
    memcpy(out, data, count * sizeof(double));
    return true;
}

bool RockTreeNodeDataParser::ReadFloatArray(const uint8_t* data, size_t len, float* out, size_t count) {
    if (len < count * sizeof(float)) return false;
    memcpy(out, data, count * sizeof(float));
    return true;
}

} // namespace globe
