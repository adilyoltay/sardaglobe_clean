#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>
#include <optional>

namespace globe {

// Cache entry metadata
struct NodeDataCacheEntry {
    std::string nodeKey;
    std::string endpointHash;
    uint64_t timestampMs = 0;
    uint64_t sizeBytes = 0;
    bool valid = false;
};

// Disk cache for RockTree NodeData responses
// Caches raw protobuf responses keyed by endpoint+nodeKey hash
class NodeDataDiskCache {
public:
    explicit NodeDataDiskCache(const std::string& cacheDir);
    ~NodeDataDiskCache() = default;
    
    // Initialize cache (create directories if needed)
    bool Init();
    
    // Check if cache contains entry for given key
    bool Contains(const std::string& endpoint, const std::string& nodeKey) const;
    
    // Read cached data. Returns empty vector if not found or invalid.
    std::vector<uint8_t> Read(const std::string& endpoint, const std::string& nodeKey);
    
    // Write data to cache
    bool Write(const std::string& endpoint, const std::string& nodeKey, 
               const std::vector<uint8_t>& data);
    
    // Remove entry from cache
    bool Remove(const std::string& endpoint, const std::string& nodeKey);
    
    // Clear all cached entries
    bool Clear();
    
    // Get cache stats
    struct Stats {
        size_t hitCount = 0;
        size_t missCount = 0;
        size_t writeCount = 0;
        size_t errorCount = 0;
        uint64_t totalBytesStored = 0;
    };
    Stats GetStats() const;
    
    // Get cache directory path
    std::string GetCachePath() const { return cacheDir_; }

private:
    std::string cacheDir_;
    mutable Stats stats_;
    mutable std::mutex statsMutex_;
    
    // Generate cache file path from endpoint and nodeKey
    std::string GetCacheFilePath(const std::string& endpoint, 
                                  const std::string& nodeKey) const;
    
    // Generate hash key from endpoint+nodeKey
    std::string GenerateCacheKey(const std::string& endpoint, 
                                  const std::string& nodeKey) const;
    
    // Ensure cache directory exists
    bool EnsureDirectory(const std::string& path) const;
    
    // Write metadata file alongside cached data
    bool WriteMetadata(const std::string& path, const NodeDataCacheEntry& entry);
    
    // Read metadata file
    std::optional<NodeDataCacheEntry> ReadMetadata(const std::string& path);
};

} // namespace globe
