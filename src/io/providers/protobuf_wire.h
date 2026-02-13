#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

namespace globe {
namespace protobuf {

// Protobuf wire types
enum class WireType : uint8_t {
    Varint = 0,      // int32, int64, uint32, uint64, sint32, sint64, bool, enum
    Fixed64 = 1,     // fixed64, sfixed64, double
    LengthDelimited = 2,  // string, bytes, embedded messages, packed repeated fields
    Fixed32 = 5      // fixed32, sfixed32, float
};

// Simple protobuf encoder for GE elevation requests
class ProtobufEncoder {
public:
    // Encode a field tag (field number + wire type)
    void EncodeTag(uint32_t fieldNumber, WireType wireType);
    
    // Encode varint (variable-length integer)
    void EncodeVarint(uint64_t value);
    void EncodeVarint(int64_t value) { EncodeVarint(static_cast<uint64_t>(value)); }
    
    // Encode fixed 64-bit
    void EncodeFixed64(uint64_t value);
    void EncodeFixed64(double value);
    
    // Encode fixed 32-bit
    void EncodeFixed32(uint32_t value);
    void EncodeFixed32(float value);
    
    // Encode length-delimited (string, bytes)
    void EncodeLengthDelimited(const std::string& value);
    void EncodeLengthDelimited(const std::vector<uint8_t>& value);
    
    // Encode packed repeated double array
    void EncodePackedDouble(uint32_t fieldNumber, const std::vector<double>& values);
    
    // Get encoded bytes
    const std::vector<uint8_t>& GetBytes() const { return buffer_; }
    std::vector<uint8_t> TakeBytes() { return std::move(buffer_); }
    
    void Clear() { buffer_.clear(); }

private:
    std::vector<uint8_t> buffer_;
};

// Simple protobuf decoder for GE elevation responses
class ProtobufDecoder {
public:
    explicit ProtobufDecoder(const std::vector<uint8_t>& data);
    explicit ProtobufDecoder(const uint8_t* data, size_t size);
    
    // Read next field tag, returns false if at end
    bool ReadTag(uint32_t& fieldNumber, WireType& wireType);
    
    // Skip current field (for unknown fields)
    void SkipField(WireType wireType);
    
    // Decode varint
    uint64_t DecodeVarint();
    int64_t DecodeSVarint();
    
    // Decode fixed 64-bit
    uint64_t DecodeFixed64();
    double DecodeDouble();
    
    // Decode fixed 32-bit
    uint32_t DecodeFixed32();
    float DecodeFloat();
    
    // Decode length-delimited
    std::string DecodeString();
    std::vector<uint8_t> DecodeBytes();
    
    // Decode packed repeated double array
    std::vector<double> DecodePackedDouble();
    
    // Check if at end
    bool AtEnd() const { return pos_ >= data_.size(); }
    
    // Get current position
    size_t GetPosition() const { return pos_; }

private:
    const std::vector<uint8_t> data_;
    size_t pos_ = 0;
    
    uint8_t ReadByte();
};

// Google Earth specific protobuf helpers
namespace ge {

// Build elevation request protobuf message
// Request format (simplified):
// message ElevationRequest {
//   repeated double latitudes = 1 [packed=true];
//   repeated double longitudes = 2 [packed=true];
//   int32 elevation_type = 3;
// }
std::vector<uint8_t> BuildElevationRequest(const std::vector<double>& lats,
                                           const std::vector<double>& lons,
                                           int elevationType = 0);

// Parse elevation response protobuf message
// Response format (simplified):
// message ElevationResponse {
//   repeated double elevations = 1 [packed=true];
//   // ... other fields
// }
std::vector<double> ParseElevationResponse(const std::vector<uint8_t>& responseData);

} // namespace ge

} // namespace protobuf
} // namespace globe
