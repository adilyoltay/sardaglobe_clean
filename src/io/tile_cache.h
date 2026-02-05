#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <filesystem>

namespace globe {

// Disk cache for tiles
class TileCache {
public:
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
    
    // Enable/disable
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    std::filesystem::path GetPath(const TileKey& key, const std::string& urlTemplate) const;
    std::string HashUrl(const std::string& url) const;
    
    std::filesystem::path cacheDir_;
    bool enabled_ = true;
};

} // namespace globe
