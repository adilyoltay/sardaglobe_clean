// RockTree Octree Index
// Lazy octree discovery for Google Earth RockTree mesh data
// Fetches PlanetoidMetadata (epoch) + BulkMetadata (child availability)
// and provides octree path lookups for mesh loading.

#pragma once

#include "http_transport.h"
#include "planetoid_metadata_parser.h"
#include "bulk_metadata_parser.h"
#include "ge_rate_limiter.h"
#include "../../core/config.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>
#include <atomic>

namespace globe {

// Octree node info cached in the index
struct OctreeNodeInfo {
    bool hasNodeData = false;         // This node has mesh data
    bool hasBulkMetadata = false;     // Deeper hierarchy available
    uint8_t availableChildren = 0;    // Bitmask: which children (0-7) have data
    uint32_t epoch = 0;               // Node's epoch
    bool bulkMetadataFetched = false; // BulkMetadata already fetched for this subtree
};

// RockTree Octree Index — lazy discovery of Google Earth mesh hierarchy
class RockTreeOctreeIndex {
public:
    explicit RockTreeOctreeIndex(const Config& config,
                                 GeRateLimiter* rateLimiter = nullptr);
    ~RockTreeOctreeIndex();

    // Initialize: fetch PlanetoidMetadata + root BulkMetadata
    // Blocking call — run from worker thread
    // Returns true if epoch and root hierarchy are available
    bool Init();

    // Get current epoch (from PlanetoidMetadata)
    uint32_t GetEpoch() const { return epoch_; }
    std::string GetEpochString() const { return std::to_string(epoch_); }

    // Get earth radius in meters
    double GetEarthRadiusM() const { return earthRadiusM_; }

    // Check if a specific octree path has mesh data
    bool HasNodeData(const std::string& path) const;

    // Get available children for a path (bitmask 0-7)
    uint8_t GetAvailableChildren(const std::string& path) const;

    // Get all children paths that have data for a given parent
    std::vector<std::string> GetChildrenWithData(const std::string& parentPath) const;

    // Get renderable node paths within a depth range.
    // Returns paths with mesh data in [minDepth, maxDepth], ordered
    // deterministically by increasing depth then lexicographic path.
    std::vector<std::string> GetRenderableNodes(int minDepth, int maxDepth) const;

    // Request BulkMetadata fetch for a subtree prefix (async)
    // Returns true if request was queued
    bool RequestBulkMetadata(const std::string& prefix);

    // Process pending BulkMetadata fetches (blocking, call from worker)
    // Returns number of fetches completed
    int ProcessPendingFetches();

    // Check if BulkMetadata has been fetched for a path's containing subtree
    bool IsBulkMetadataFetched(const std::string& path) const;

    // Build URL for NodeData fetch
    std::string BuildNodeDataUrl(const std::string& path) const;

    // Build URL for BulkMetadata fetch
    std::string BuildBulkMetadataUrl(const std::string& prefix) const;

    // Is initialized?
    bool IsInitialized() const { return initialized_.load(); }

    // Get total known node count
    size_t GetKnownNodeCount() const;

    // Get count of nodes with mesh data
    size_t GetMeshNodeCount() const;

    // Convert a TileKey-style quadkey (digits 0-3) to deterministic candidate octree paths.
    // Returns candidate octree paths in deterministic ascending order:
    // depth (shorter first), then lexicographic path.
    std::vector<std::string> TileQuadKeyToOctreePaths(const std::string& tileQuadKey) const;

#ifdef NATIVE_GLOBE_TESTING
    void TEST_SetNodesForUnitTests(std::unordered_map<std::string, OctreeNodeInfo> nodes) {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        nodes_ = std::move(nodes);
    }
#endif

private:
    const Config& config_;
    GeRateLimiter* rateLimiter_;  // Not owned
    std::unique_ptr<IHttpTransport> transport_;

    // Epoch and earth radius from PlanetoidMetadata
    uint32_t epoch_ = 0;
    double earthRadiusM_ = 0.0;
    std::atomic<bool> initialized_{false};

    // Octree node index: path -> node info
    mutable std::mutex nodesMutex_;
    std::unordered_map<std::string, OctreeNodeInfo> nodes_;

    // Pending BulkMetadata fetch requests
    mutable std::mutex pendingMutex_;
    std::unordered_set<std::string> pendingBulkFetches_;
    std::unordered_set<std::string> fetchedBulkPrefixes_;

    // URL templates
    std::string planetoidMetadataUrl_;
    std::string bulkMetadataTemplate_;
    std::string nodeDataTemplate_;

    // Build standard GE headers (Origin, Referer, User-Agent)
    std::vector<std::pair<std::string, std::string>> BuildHeaders() const;

    // Fetch and parse PlanetoidMetadata
    bool FetchPlanetoidMetadata();

    // Fetch and parse BulkMetadata for a given path prefix
    bool FetchBulkMetadata(const std::string& prefix);

    // Populate nodes_ from parsed BulkMetadata
    void PopulateNodes(const BulkMetadataResult& bulk);

    // Build octree path from BulkMetadata index
    // Root BulkMetadata covers paths at level 2-5 (approx)
    // Each entry maps to a specific octree path within the subtree
    static std::string BuildOctreePath(const std::string& basePath, int nodeIndex, int level);
};

} // namespace globe
