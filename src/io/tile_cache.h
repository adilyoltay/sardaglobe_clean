#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>

namespace globe {

// Disk cache for tiles
class TileCache {
public:
    struct Stats {
        size_t readHits = 0;
        size_t readMisses = 0;
        size_t writeSuccess = 0;
        size_t writeFail = 0;
        size_t bytesRead = 0;
        size_t bytesWritten = 0;
    };

    explicit TileCache(const std::string& cacheDir = "tile_cache");
    
    // Check if tile is cached
    bool Has(const TileKey& key, const std::string& urlTemplate) const;
    
    // Read from cache
    bool Read(const TileKey& key, const std::string& urlTemplate, std::vector<uint8_t>& out) const;
    
    // Write to cache
    bool Write(const TileKey& key, const std::string& urlTemplate, const std::vector<uint8_t>& data);

    // Remove a cached tile
    bool Remove(const TileKey& key, const std::string& urlTemplate);
    
    // Clear all cache
    void Clear();
    
    // Get cache size in bytes
    size_t GetSize() const;

    // Telemetry (P3.1): disk cache layer hit/miss/write counters
    Stats GetStats() const;
    void ResetStats();
    
    // Enable/disable
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    std::filesystem::path GetPath(const TileKey& key, const std::string& urlTemplate) const;
    std::string HashUrl(const std::string& url) const;
    
    std::filesystem::path cacheDir_;
    bool enabled_ = true;

    mutable std::atomic<size_t> readHits_{0};
    mutable std::atomic<size_t> readMisses_{0};
    mutable std::atomic<size_t> writeSuccess_{0};
    mutable std::atomic<size_t> writeFail_{0};
    mutable std::atomic<size_t> bytesRead_{0};
    mutable std::atomic<size_t> bytesWritten_{0};
};

} // namespace globe
