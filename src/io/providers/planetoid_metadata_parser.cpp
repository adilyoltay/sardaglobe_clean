// PlanetoidMetadata Parser Implementation

#include "planetoid_metadata_parser.h"
#include "protobuf_wire.h"

namespace globe {

PlanetoidMetadata PlanetoidMetadataParser::Parse(const std::vector<uint8_t>& data) {
    PlanetoidMetadata result;

    if (data.empty()) {
        result.error = "Empty PlanetoidMetadata response";
        return result;
    }

    try {
        protobuf::ProtobufDecoder decoder(data);
        bool hasEpoch = false;
        bool hasRadius = false;

        while (!decoder.AtEnd()) {
            uint32_t fieldNumber;
            protobuf::WireType wireType;
            if (!decoder.ReadTag(fieldNumber, wireType)) break;

            if (fieldNumber == 1 && wireType == protobuf::WireType::LengthDelimited) {
                // Field 1: inner message containing epoch
                auto innerBytes = decoder.DecodeBytes();
                protobuf::ProtobufDecoder inner(innerBytes);

                while (!inner.AtEnd()) {
                    uint32_t innerField;
                    protobuf::WireType innerWire;
                    if (!inner.ReadTag(innerField, innerWire)) break;

                    if (innerField == 2 && innerWire == protobuf::WireType::Varint) {
                        // Field 2: epoch (primary)
                        result.epoch = static_cast<uint32_t>(inner.DecodeVarint());
                        hasEpoch = true;
                    } else {
                        inner.SkipField(innerWire);
                    }
                }
            } else if (fieldNumber == 2 && wireType == protobuf::WireType::Fixed32) {
                // Field 2: earth radius as float
                result.earthRadiusM = decoder.DecodeFloat();
                hasRadius = true;
            } else {
                decoder.SkipField(wireType);
            }
        }

        if (!hasEpoch) {
            result.error = "PlanetoidMetadata missing epoch field";
            return result;
        }

        if (!hasRadius) {
            result.error = "PlanetoidMetadata missing earth radius field";
            return result;
        }

        // Sanity checks
        if (result.epoch == 0) {
            result.error = "PlanetoidMetadata epoch is zero";
            return result;
        }
        if (result.earthRadiusM < 6000000.0f || result.earthRadiusM > 7000000.0f) {
            result.error = "PlanetoidMetadata earth radius out of range: " +
                           std::to_string(result.earthRadiusM);
            return result;
        }

        result.valid = true;

    } catch (const std::exception& e) {
        result.error = std::string("PlanetoidMetadata parse error: ") + e.what();
    }

    return result;
}

} // namespace globe
