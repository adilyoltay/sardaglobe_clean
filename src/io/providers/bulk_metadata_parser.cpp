// BulkMetadata Parser Implementation

#include "bulk_metadata_parser.h"
#include "protobuf_wire.h"

namespace globe {

namespace {

// Parse a single NodeMetadata entry from BulkMetadata
// Wire format per entry (LengthDelimited):
//   Field 1 (Varint): flags — encodes hasNodeData and child availability
//   Field 2 (Varint): epoch
//   Field 3 (LengthDelimited): bounding box (optional, skipped)
//   Field 4 (Varint): additional flags (hasBulkMetadata indicator)
OctreeNodeMeta ParseNodeEntry(const std::vector<uint8_t>& entryData) {
    OctreeNodeMeta meta;

    protobuf::ProtobufDecoder decoder(entryData);

    while (!decoder.AtEnd()) {
        uint32_t fieldNumber;
        protobuf::WireType wireType;
        if (!decoder.ReadTag(fieldNumber, wireType)) break;

        switch (fieldNumber) {
            case 1:
                if (wireType == protobuf::WireType::Varint) {
                    meta.flags = static_cast<uint32_t>(decoder.DecodeVarint());
                    // Flags encoding (verified via GE server testing):
                    // Bit 0: hasNodeData (mesh available)
                    // Bit 1: hasBulkMetadata (deeper hierarchy available)
                    // Bits 2+: texture format availability (not used for tree structure)
                    // Tree is a DENSE QUADTREE (4 children per node, digits 0-3).
                    meta.hasNodeData = (meta.flags & 0x01) != 0;
                    meta.hasBulkMetadata = (meta.flags & 0x02) != 0;
                    meta.availableChildren = 0x0F;  // Dense quadtree: all 4 children implicit
                } else {
                    decoder.SkipField(wireType);
                }
                break;
            case 2:
                if (wireType == protobuf::WireType::Varint) {
                    meta.epoch = static_cast<uint32_t>(decoder.DecodeVarint());
                } else {
                    decoder.SkipField(wireType);
                }
                break;
            default:
                decoder.SkipField(wireType);
                break;
        }
    }

    return meta;
}

} // anonymous namespace

BulkMetadataResult BulkMetadataParser::Parse(const std::vector<uint8_t>& data,
                                              const std::string& basePath) {
    BulkMetadataResult result;
    result.basePath = basePath;

    if (data.empty()) {
        result.error = "Empty BulkMetadata response";
        return result;
    }

    try {
        protobuf::ProtobufDecoder decoder(data);

        while (!decoder.AtEnd()) {
            uint32_t fieldNumber;
            protobuf::WireType wireType;
            if (!decoder.ReadTag(fieldNumber, wireType)) break;

            if (fieldNumber == 1 && wireType == protobuf::WireType::LengthDelimited) {
                // Field 1 (repeated): NodeMetadata entry
                auto entryBytes = decoder.DecodeBytes();
                OctreeNodeMeta meta = ParseNodeEntry(entryBytes);
                result.nodes.push_back(meta);
            } else {
                decoder.SkipField(wireType);
            }
        }

        if (result.nodes.empty()) {
            result.error = "BulkMetadata contains no node entries";
            return result;
        }

        result.valid = true;

    } catch (const std::exception& e) {
        result.error = std::string("BulkMetadata parse error: ") + e.what();
    }

    return result;
}

} // namespace globe
