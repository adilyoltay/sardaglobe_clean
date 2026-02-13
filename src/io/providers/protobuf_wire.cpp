#include "protobuf_wire.h"
#include <cstring>
#include <algorithm>

namespace globe {
namespace protobuf {

// ============================================================================
// ProtobufEncoder implementation
// ============================================================================

void ProtobufEncoder::EncodeTag(uint32_t fieldNumber, WireType wireType) {
    uint64_t tag = (static_cast<uint64_t>(fieldNumber) << 3) | static_cast<uint64_t>(wireType);
    EncodeVarint(tag);
}

void ProtobufEncoder::EncodeVarint(uint64_t value) {
    while (value > 0x7F) {
        buffer_.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer_.push_back(static_cast<uint8_t>(value));
}

void ProtobufEncoder::EncodeFixed64(uint64_t value) {
    // Little-endian
    for (int i = 0; i < 8; ++i) {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

void ProtobufEncoder::EncodeFixed64(double value) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value), "Size mismatch");
    std::memcpy(&bits, &value, sizeof(value));
    EncodeFixed64(bits);
}

void ProtobufEncoder::EncodeFixed32(uint32_t value) {
    // Little-endian
    for (int i = 0; i < 4; ++i) {
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

void ProtobufEncoder::EncodeFixed32(float value) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value), "Size mismatch");
    std::memcpy(&bits, &value, sizeof(value));
    EncodeFixed32(bits);
}

void ProtobufEncoder::EncodeLengthDelimited(const std::string& value) {
    EncodeVarint(static_cast<uint64_t>(value.size()));
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void ProtobufEncoder::EncodeLengthDelimited(const std::vector<uint8_t>& value) {
    EncodeVarint(static_cast<uint64_t>(value.size()));
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void ProtobufEncoder::EncodePackedDouble(uint32_t fieldNumber, const std::vector<double>& values) {
    if (values.empty()) return;
    
    // Calculate payload size: 8 bytes per double
    size_t payloadSize = values.size() * 8;
    
    // Encode tag
    EncodeTag(fieldNumber, WireType::LengthDelimited);
    
    // Encode length
    EncodeVarint(static_cast<uint64_t>(payloadSize));
    
    // Encode doubles
    for (double value : values) {
        EncodeFixed64(value);
    }
}

// ============================================================================
// ProtobufDecoder implementation
// ============================================================================

ProtobufDecoder::ProtobufDecoder(const std::vector<uint8_t>& data)
    : data_(data), pos_(0) {
}

ProtobufDecoder::ProtobufDecoder(const uint8_t* data, size_t size)
    : data_(data, data + size), pos_(0) {
}

uint8_t ProtobufDecoder::ReadByte() {
    if (pos_ >= data_.size()) {
        throw std::runtime_error("ProtobufDecoder: Unexpected end of data");
    }
    return data_[pos_++];
}

bool ProtobufDecoder::ReadTag(uint32_t& fieldNumber, WireType& wireType) {
    if (AtEnd()) return false;
    
    uint64_t tag = DecodeVarint();
    fieldNumber = static_cast<uint32_t>(tag >> 3);
    wireType = static_cast<WireType>(tag & 0x07);
    return true;
}

void ProtobufDecoder::SkipField(WireType wireType) {
    switch (wireType) {
        case WireType::Varint:
            DecodeVarint();
            break;
        case WireType::Fixed64:
            if (pos_ + 8 > data_.size()) throw std::runtime_error("SkipField: Truncated fixed64");
            pos_ += 8;
            break;
        case WireType::LengthDelimited: {
            uint64_t length = DecodeVarint();
            if (pos_ + length > data_.size()) throw std::runtime_error("SkipField: Truncated length-delimited");
            pos_ += length;
            break;
        }
        case WireType::Fixed32:
            if (pos_ + 4 > data_.size()) throw std::runtime_error("SkipField: Truncated fixed32");
            pos_ += 4;
            break;
        default:
            throw std::runtime_error("SkipField: Unknown wire type");
    }
}

uint64_t ProtobufDecoder::DecodeVarint() {
    uint64_t result = 0;
    int shift = 0;
    
    while (true) {
        uint8_t byte = ReadByte();
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
        if (shift >= 64) throw std::runtime_error("DecodeVarint: Overflow");
    }
    
    return result;
}

int64_t ProtobufDecoder::DecodeSVarint() {
    uint64_t encoded = DecodeVarint();
    // ZigZag decode
    return static_cast<int64_t>((encoded >> 1) ^ (-(encoded & 1)));
}

uint64_t ProtobufDecoder::DecodeFixed64() {
    if (pos_ + 8 > data_.size()) {
        throw std::runtime_error("DecodeFixed64: Truncated data");
    }
    
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(data_[pos_++]) << (i * 8);
    }
    return result;
}

double ProtobufDecoder::DecodeDouble() {
    uint64_t bits = DecodeFixed64();
    double result;
    static_assert(sizeof(bits) == sizeof(result), "Size mismatch");
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

uint32_t ProtobufDecoder::DecodeFixed32() {
    if (pos_ + 4 > data_.size()) {
        throw std::runtime_error("DecodeFixed32: Truncated data");
    }
    
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result |= static_cast<uint32_t>(data_[pos_++]) << (i * 8);
    }
    return result;
}

float ProtobufDecoder::DecodeFloat() {
    uint32_t bits = DecodeFixed32();
    float result;
    static_assert(sizeof(bits) == sizeof(result), "Size mismatch");
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::string ProtobufDecoder::DecodeString() {
    uint64_t length = DecodeVarint();
    if (pos_ + length > data_.size()) {
        throw std::runtime_error("DecodeString: Truncated data");
    }
    
    std::string result;
    result.assign(reinterpret_cast<const char*>(data_.data() + pos_), length);
    pos_ += length;
    return result;
}

std::vector<uint8_t> ProtobufDecoder::DecodeBytes() {
    uint64_t length = DecodeVarint();
    if (pos_ + length > data_.size()) {
        throw std::runtime_error("DecodeBytes: Truncated data");
    }
    
    std::vector<uint8_t> result(data_.begin() + pos_, data_.begin() + pos_ + length);
    pos_ += length;
    return result;
}

std::vector<double> ProtobufDecoder::DecodePackedDouble() {
    uint64_t length = DecodeVarint();
    if (pos_ + length > data_.size()) {
        throw std::runtime_error("DecodePackedDouble: Truncated data");
    }
    
    if (length % 8 != 0) {
        throw std::runtime_error("DecodePackedDouble: Invalid length");
    }
    
    size_t numDoubles = length / 8;
    std::vector<double> result;
    result.reserve(numDoubles);
    
    for (size_t i = 0; i < numDoubles; ++i) {
        result.push_back(DecodeDouble());
    }
    
    return result;
}

// ============================================================================
// Google Earth specific helpers
// ============================================================================

namespace ge {

std::vector<uint8_t> BuildElevationRequest(const std::vector<double>& lats,
                                           const std::vector<double>& lons,
                                           int elevationType) {
    if (lats.size() != lons.size()) {
        throw std::invalid_argument("BuildElevationRequest: lat/lon size mismatch");
    }
    
    ProtobufEncoder encoder;
    
    // Encode latitudes (field 1, packed double)
    encoder.EncodePackedDouble(1, lats);
    
    // Encode longitudes (field 2, packed double)
    encoder.EncodePackedDouble(2, lons);
    
    // Encode elevation type (field 3, varint)
    encoder.EncodeTag(3, WireType::Varint);
    encoder.EncodeVarint(static_cast<uint64_t>(elevationType));
    
    return encoder.TakeBytes();
}

std::vector<double> ParseElevationResponse(const std::vector<uint8_t>& responseData) {
    std::vector<double> elevations;
    
    ProtobufDecoder decoder(responseData);
    
    while (!decoder.AtEnd()) {
        uint32_t fieldNumber;
        WireType wireType;
        
        if (!decoder.ReadTag(fieldNumber, wireType)) break;
        
        if (fieldNumber == 1 && wireType == WireType::LengthDelimited) {
            // Field 1 is elevations (packed double)
            elevations = decoder.DecodePackedDouble();
        } else {
            // Skip unknown fields
            decoder.SkipField(wireType);
        }
    }
    
    return elevations;
}

} // namespace ge

} // namespace protobuf
} // namespace globe
