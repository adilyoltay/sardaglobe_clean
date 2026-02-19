// BulkMetadata Protobuf Parser
// Decodes Google Earth BulkMetadata response (dense quadtree node metadata).
// GE uses a dense 4-ary quadtree (digits 0-3). Flags field 1: bit 0 = hasNodeData,
// bit 1 = hasBulkMetadata, bits 2+ = texture format availability.
// Response: https://kh.google.com/rt/earth/BulkMetadata/pb=!1m2!1s{path}!2u{epoch}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace globe {

// Metadata for a single quadtree node within BulkMetadata
struct OctreeNodeMeta {
    uint32_t epoch = 0;              // Node's epoch version (0 = inherit from ancestor)
    uint32_t flags = 0;              // Raw flags field (available_texture_formats_mask)
    uint8_t availableChildren = 0;   // Dense quadtree: always 0x0F (all 4 children implicit)
    bool hasNodeData = false;        // Whether this node has mesh data (flags bit 0)
    bool hasBulkMetadata = false;    // Whether this node has further BulkMetadata (flags bit 1)

    // Check if specific child (0-3) exists (always true in dense quadtree)
    bool HasChild(int childIndex) const {
        return (availableChildren & (1 << childIndex)) != 0;
    }

    // Get count of available children (4 in dense quadtree)
    int ChildCount() const {
        int count = 0;
        uint8_t mask = availableChildren;
        while (mask) { count += mask & 1; mask >>= 1; }
        return count;
    }
};

// Parsed BulkMetadata result
struct BulkMetadataResult {
    std::string basePath;                    // Quadtree path prefix this covers
    std::vector<OctreeNodeMeta> nodes;       // Node metadata entries
    bool valid = false;
    std::string error;

    // Total nodes with mesh data
    int NodeDataCount() const {
        int count = 0;
        for (const auto& n : nodes) {
            if (n.hasNodeData) count++;
        }
        return count;
    }
};

// Parser for BulkMetadata protobuf response
// Decodes repeated NodeMetadata entries (dense quadtree, 4 children per node)
class BulkMetadataParser {
public:
    static BulkMetadataResult Parse(const std::vector<uint8_t>& data,
                                    const std::string& basePath);
};

} // namespace globe
