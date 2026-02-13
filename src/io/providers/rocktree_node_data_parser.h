// RockTree NodeData Protobuf Parser
// Decodes GE NodeData mesh format (RE-based Sprint 1 implementation)

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <optional>

namespace globe {

// UV quantization parameters (field 10: 4 floats)
struct UvQuantization {
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    
    // Decode uint16 UV to float (RockTree format: u = (u16 + offset) * scale)
    void Decode(uint16_t u, uint16_t v, float& outU, float& outV, bool flipV = true) const {
        outU = (static_cast<float>(u) + offsetU) * scaleU;
        outV = (static_cast<float>(v) + offsetV) * scaleV;
        if (flipV) {
            outV = 1.0f - outV;
        }
    }
};

// Parsed texture data (field 6)
struct NodeDataTexture {
    std::vector<uint8_t> jpegBytes;
    int width = 0;
    int height = 0;
    bool valid = false;
};

// Parsed mesh data from NodeData protobuf
struct ParsedNodeData {
    // Transform matrix (field 1: 16 doubles, column-major)
    // Stored as 4x4 matrix: matrix[col][row] for OpenGL compatibility
    std::array<double, 16> transform{};
    bool hasTransform = false;
    
    // Positions (field 2.1: int16 triplets, local space / 32768)
    std::vector<int16_t> positions;  // 3*V values
    int vertexCount = 0;
    
    // UV coordinates (field 2.11: uint16 pairs, quantized)
    std::vector<uint16_t> uv;  // 2*V values
    UvQuantization uvQuant;
    bool hasUvQuant = false;
    
    // Indices (field 2.3: varint stream with restart markers)
    // After strip-to-triangle conversion
    std::vector<uint32_t> indices;  // Triangle list (3 * triangleCount)
    int triangleCount = 0;
    
    // Texture (field 2.6)
    NodeDataTexture texture;
    
    // Error state
    std::string error;
    bool success = false;
};

// Parser for NodeData protobuf format
class RockTreeNodeDataParser {
public:
    // Parse raw NodeData protobuf bytes
    // Returns ParsedNodeData with success=false on error (check error field)
    static ParsedNodeData Parse(const std::vector<uint8_t>& data);
    
    // Convert triangle strip (with restart markers) to triangle list
    // Input: raw strip indices (with restart >= 2*V)
    // Output: triangle list indices (3 * triangleCount)
    static bool ConvertStripToTriangles(
        const std::vector<uint32_t>& rawStrip,
        int vertexCount,
        std::vector<uint32_t>& outTriangles,
        std::string& outError
    );
    
    // Read varint from buffer, returns bytes consumed or 0 on error
    static size_t ReadVarint(const uint8_t* data, size_t len, uint64_t& outValue);
    
private:
    // Parse wire-format protobuf helpers
    static bool ParseTopLevel(ParsedNodeData& out, const uint8_t* data, size_t len);
    static bool ParsePayload(ParsedNodeData& out, const uint8_t* data, size_t len);
    static bool ParseTexture(NodeDataTexture& out, const uint8_t* data, size_t len);
    
    // Read fixed-length fields
    static bool ReadDoubleArray(const uint8_t* data, size_t len, double* out, size_t count);
    static bool ReadFloatArray(const uint8_t* data, size_t len, float* out, size_t count);
};

} // namespace globe
