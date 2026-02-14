// PlanetoidMetadata Protobuf Parser
// Decodes Google Earth PlanetoidMetadata response (epoch + earth radius)
// Response: https://kh.google.com/rt/earth/PlanetoidMetadata

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace globe {

// Parsed PlanetoidMetadata result
struct PlanetoidMetadata {
    uint32_t epoch = 0;          // Current dataset epoch (e.g. 1008)
    float earthRadiusM = 0.0f;   // Earth radius in meters (e.g. 6371010.0)
    bool valid = false;
    std::string error;
};

// Parser for PlanetoidMetadata protobuf response
// Wire format (13 bytes):
//   Field 1 (LengthDelimited): inner message
//     Field 2 (Varint): epoch
//     Field 5 (Varint): epoch (duplicate)
//   Field 2 (Fixed32): earth radius as float
class PlanetoidMetadataParser {
public:
    static PlanetoidMetadata Parse(const std::vector<uint8_t>& data);
};

} // namespace globe
