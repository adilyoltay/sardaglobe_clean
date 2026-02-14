// BulkMetadata Protobuf Parser
// Decodes Google Earth BulkMetadata response (octree child availability)
// Response: https://kh.google.com/rt/earth/BulkMetadata/pb=!1m2!1s{path}!2u{epoch}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace globe {

// Metadata for a single octree node within BulkMetadata
struct OctreeNodeMeta {
    uint32_t epoch = 0;              // Node's epoch version
    uint32_t flags = 0;              // Raw flags field
    uint8_t availableChildren = 0;   // Bitmask: which children (0-7) have data
    bool hasNodeData = false;        // Whether this node has mesh data
    bool hasBulkMetadata = false;    // Whether this node has further BulkMetadata

    // Check if specific child (0-7) exists
    bool HasChild(int childIndex) const {
        return (availableChildren & (1 << childIndex)) != 0;
    }

    // Get count of available children
    int ChildCount() const {
        int count = 0;
        uint8_t mask = availableChildren;
        while (mask) { count += mask & 1; mask >>= 1; }
        return count;
    }
};

// Parsed BulkMetadata result
struct BulkMetadataResult {
    std::string basePath;                    // Octree path prefix this covers
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
// Decodes repeated NodeMetadata entries with child availability bitmasks
class BulkMetadataParser {
public:
    static BulkMetadataResult Parse(const std::vector<uint8_t>& data,
                                    const std::string& basePath);
};

} // namespace globe
