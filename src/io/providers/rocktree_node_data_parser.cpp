// RockTree NodeData Protobuf Parser Implementation

#include "rocktree_node_data_parser.h"
#include <cstring>
#include <algorithm>

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

// Parse field key from varint, returns bytes consumed or 0 on error
static size_t ParseFieldKeyVarint(const uint8_t* data, size_t len, uint32_t& outFieldNum, WireType& outType) {
    uint64_t key;
    size_t bytes = RockTreeNodeDataParser::ReadVarint(data, len, key);
    if (bytes == 0) return 0;
    outFieldNum = static_cast<uint32_t>(key >> 3);
    outType = static_cast<WireType>(key & 0x07);
    if (outFieldNum == 0) return 0;  // Invalid field number
    return bytes;
}

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
}

bool RockTreeNodeDataParser::ParseTopLevel(ParsedNodeData& out, const uint8_t* data, size_t len) {
    size_t pos = 0;
    
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
                if (length != 128) {
                    out.error = "Transform matrix must be 128 bytes, got " + std::to_string(length);
                    return false;
                }
                if (!ReadDoubleArray(data + pos, length, out.transform.data(), 16)) {
                    out.error = "Failed to read transform matrix";
                    return false;
                }
                out.hasTransform = true;
            } else if (fieldNum == 2) {
                if (!ParsePayload(out, data + pos, length)) {
                    return false;
                }
            }
            
            pos += length;
        } else {
            // Skip unknown wire types instead of failing
            if (!SkipUnknownField(data, pos, len, wireType)) {
                out.error = "Failed to skip field at position " + std::to_string(fieldStart);
                return false;
            }
        }
    }
    
    // B1: Parser hardening - require transform matrix
    if (!out.hasTransform) {
        out.error = "Missing required transform matrix";
        return false;
    }
    
    return true;
}

bool RockTreeNodeDataParser::ParsePayload(ParsedNodeData& out, const uint8_t* data, size_t len) {
    size_t pos = 0;
    
    while (pos < len) {
        uint32_t fieldNum;
        WireType wireType;
        size_t keyBytes = ParseFieldKeyVarint(data + pos, len - pos, fieldNum, wireType);
        if (keyBytes == 0) {
            out.error = "Invalid field key in payload at " + std::to_string(pos);
            return false;
        }
        pos += keyBytes;
        
        size_t fieldStart = pos;
        
        if (wireType == WireType::LengthDelimited) {
            uint64_t length;
            size_t varintBytes = ReadVarint(data + pos, len - pos, length);
            if (varintBytes == 0) {
                out.error = "Failed to read length in payload";
                return false;
            }
            pos += varintBytes;
            
            if (pos + length > len) {
                out.error = "Payload field length exceeds buffer";
                return false;
            }
            
            if (fieldNum == 1) {
                // Positions: int16 triplets
                if (length % 6 != 0) {
                    out.error = "Positions length not multiple of 6 (got " + std::to_string(length) + ")";
                    return false;
                }
                // P0: DoS fix - check limit BEFORE allocation
                const size_t MAX_POSITION_BYTES = 60'000'000;  // 60MB (10M vertices * 6 bytes)
                if (length > MAX_POSITION_BYTES) {
                    out.error = "Positions data too large: " + std::to_string(length) + " bytes";
                    return false;
                }
                out.positions.resize(length / 2);  // int16 count
                memcpy(out.positions.data(), data + pos, length);
                out.vertexCount = static_cast<int>(out.positions.size()) / 3;
            } else if (fieldNum == 3) {
                // Indices: varint stream
                size_t idxPos = 0;
                uint64_t indexCount;
                size_t countBytes = ReadVarint(data + pos, length, indexCount);
                if (countBytes == 0) {
                    out.error = "Failed to read index count";
                    return false;
                }
                // P1: Index count cap (DoS prevention)
                const uint64_t MAX_INDEX_COUNT = 50'000'000;  // 50M indices ~ 200MB
                if (indexCount > MAX_INDEX_COUNT) {
                    out.error = "Index count exceeds limit: " + std::to_string(indexCount);
                    return false;
                }
                idxPos += countBytes;
                
                std::vector<uint32_t> rawStrip;
                rawStrip.reserve(static_cast<size_t>(indexCount));
                
                while (rawStrip.size() < indexCount && idxPos < length) {
                    uint64_t value;
                    size_t vb = ReadVarint(data + pos + idxPos, length - idxPos, value);
                    if (vb == 0) break;
                    idxPos += vb;
                    // P1: Varint overflow check - reject values that don't fit in uint32_t
                    if (value > 0xFFFFFFFFULL) {
                        out.error = "Index value exceeds uint32_t range";
                        return false;
                    }
                    rawStrip.push_back(static_cast<uint32_t>(value));
                }
                
                if (rawStrip.size() != indexCount) {
                    out.error = "Index count mismatch: expected " + std::to_string(indexCount) + 
                               ", got " + std::to_string(rawStrip.size());
                    return false;
                }
                
                if (!ConvertStripToTriangles(rawStrip, out.vertexCount, out.indices, out.error)) {
                    return false;
                }
                out.triangleCount = static_cast<int>(out.indices.size()) / 3;
            } else if (fieldNum == 10) {
                // UV quantization: 4 floats = 16 bytes
                if (length != 16) {
                    out.error = "UV quant length must be 16, got " + std::to_string(length);
                    return false;
                }
                float floats[4];
                if (!ReadFloatArray(data + pos, length, floats, 4)) {
                    out.error = "Failed to read UV quant";
                    return false;
                }
                out.uvQuant.offsetU = floats[0];
                out.uvQuant.offsetV = floats[1];
                out.uvQuant.scaleU = floats[2];
                out.uvQuant.scaleV = floats[3];
                out.hasUvQuant = true;
            } else if (fieldNum == 11) {
                // UV coordinates: uint16 pairs
                if (length % 4 != 0) {
                    out.error = "UV length not multiple of 4 (got " + std::to_string(length) + ")";
                    return false;
                }
                size_t uvCount = length / 4;  // number of UV pairs
                out.uv.resize(uvCount * 2);
                memcpy(out.uv.data(), data + pos, length);
            } else if (fieldNum == 6) {
                // Texture message
                if (!ParseTexture(out.texture, data + pos, length)) {
                    out.texture.valid = false;
                }
            }
            // Unknown fields are silently skipped
            
            pos += length;
        } else {
            // Skip unknown wire types instead of failing
            if (!SkipUnknownField(data, pos, len, wireType)) {
                out.error = "Failed to skip field in payload at " + std::to_string(fieldStart);
                return false;
            }
        }
    }
    
    // Validate UV count matches vertex count
    if (!out.uv.empty() && out.uv.size() / 2 != static_cast<size_t>(out.vertexCount)) {
        out.error = "UV count (" + std::to_string(out.uv.size() / 2) + 
                   ") doesn't match vertex count (" + std::to_string(out.vertexCount) + ")";
        return false;
    }
    
    // B1: Parser hardening - vertex count limits (prevent memory DoS)
    const int MAX_VERTEX_COUNT = 10'000'000;  // 10M vertices ~ 60MB position data
    if (out.vertexCount > MAX_VERTEX_COUNT) {
        out.error = "Vertex count exceeds limit: " + std::to_string(out.vertexCount);
        return false;
    }
    
    // B1: Parser hardening - indices consistency check
    if (!out.indices.empty()) {
        uint32_t maxIndex = 0;
        for (uint32_t idx : out.indices) {
            if (idx != 0xFFFFFFFFU) {  // Skip strip restart markers
                maxIndex = std::max(maxIndex, idx);
            }
        }
        if (maxIndex >= static_cast<uint32_t>(out.vertexCount)) {
            out.error = "Index value out of range: max=" + std::to_string(maxIndex) + 
                       " vs vertices=" + std::to_string(out.vertexCount);
            return false;
        }
    }
    
    return true;
}

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
                // JPEG bytes
                out.jpegBytes.resize(length);
                memcpy(out.jpegBytes.data(), data + pos, length);
                out.valid = true;
            }
            // Unknown fields are skipped
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
            break;
        }
    }
    
    return out.valid;
}

size_t RockTreeNodeDataParser::ReadVarint(const uint8_t* data, size_t len, uint64_t& outValue) {
    outValue = 0;
    size_t shift = 0;
    size_t pos = 0;
    
    while (pos < len && pos < 10) {  // Max 10 bytes for 64-bit varint
        uint8_t byte = data[pos];
        outValue |= static_cast<uint64_t>(byte & 0x7F) << shift;
        pos++;
        if ((byte & 0x80) == 0) {
            return pos;  // Success
        }
        shift += 7;
        if (shift >= 64) break;  // Overflow
    }
    
    return 0;  // Error
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

bool RockTreeNodeDataParser::ConvertStripToTriangles(
    const std::vector<uint32_t>& rawStrip,
    int vertexCount,
    std::vector<uint32_t>& outTriangles,
    std::string& outError) {
    
    outTriangles.clear();
    
    if (rawStrip.empty()) {
        return true;  // Empty is valid
    }
    
    uint32_t restartMarker = static_cast<uint32_t>(vertexCount) * 2;
    
    std::vector<uint32_t> currentStrip;
    
    for (uint32_t raw : rawStrip) {
        if (raw >= restartMarker) {
            // Restart marker - finish current strip and start new one
            if (currentStrip.size() >= 3) {
                // Convert current strip to triangles
                for (size_t i = 2; i < currentStrip.size(); ++i) {
                    if (i % 2 == 0) {
                        outTriangles.push_back(currentStrip[i - 2]);
                        outTriangles.push_back(currentStrip[i - 1]);
                        outTriangles.push_back(currentStrip[i]);
                    } else {
                        outTriangles.push_back(currentStrip[i - 2]);
                        outTriangles.push_back(currentStrip[i]);
                        outTriangles.push_back(currentStrip[i - 1]);
                    }
                }
            }
            currentStrip.clear();
        } else {
            // Regular index - extract vertex id
            uint32_t vid = raw >> 1;
            if (vid >= static_cast<uint32_t>(vertexCount)) {
                outError = "Strip index " + std::to_string(vid) + " >= vertexCount " + 
                          std::to_string(vertexCount);
                return false;
            }
            currentStrip.push_back(vid);
        }
    }
    
    // Don't forget the last strip
    if (currentStrip.size() >= 3) {
        for (size_t i = 2; i < currentStrip.size(); ++i) {
            if (i % 2 == 0) {
                outTriangles.push_back(currentStrip[i - 2]);
                outTriangles.push_back(currentStrip[i - 1]);
                outTriangles.push_back(currentStrip[i]);
            } else {
                outTriangles.push_back(currentStrip[i - 2]);
                outTriangles.push_back(currentStrip[i]);
                outTriangles.push_back(currentStrip[i - 1]);
            }
        }
    }
    
    return true;
}

} // namespace globe
