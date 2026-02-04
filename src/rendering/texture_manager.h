#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace globe {

// Manages GPU textures with LRU eviction and pin/unpin support (GE-style)
class TextureManager {
public:
    explicit TextureManager(const Config& config);
    ~TextureManager();
    
    // Upload pending textures (time-budgeted)
    // Returns number of uploads performed
    int ProcessUploads(std::unordered_map<TileKey, Tile>& tiles, double budgetMs);
    
    // Queue a tile for upload
    void QueueUpload(Tile& tile);
    
    // Delete a texture
    void DeleteTexture(uint32_t textureId);
    
    // Pin/Unpin API (GE-style cache policy)
    // Pinned tiles are protected from eviction
    void Pin(const TileKey& key);
    void Unpin(const TileKey& key);
    void SetPinnedSet(const std::unordered_set<TileKey>& keys);
    void ClearPinned();
    bool IsPinned(const TileKey& key) const;
    int GetPinnedCount() const { return static_cast<int>(pinnedKeys_.size()); }
    
    // Evict least recently used textures if over limit
    // Respects pinned tiles - they won't be evicted
    void EvictIfNeeded(std::unordered_map<TileKey, Tile>& tiles, int maxTiles);
    
    // Create loading placeholder texture
    uint32_t CreateLoadingTexture();
    
    // Stats
    int GetTextureCount() const { return textureCount_; }
    int GetPendingUploads() const { return static_cast<int>(uploadQueue_.size()); }
    int GetLastEvictedCount() const { return lastEvictedCount_; }

private:
    uint32_t CreateTexture(const uint8_t* pixels, int width, int height);
    
    const Config& config_;
    std::queue<TileKey> uploadQueue_;
    int textureCount_ = 0;
    uint32_t loadingTexture_ = 0;
    
    // Pin/unpin support (GE-style cache policy)
    std::unordered_set<TileKey> pinnedKeys_;
    int lastEvictedCount_ = 0;
};

} // namespace globe
