#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include <queue>
#include <unordered_map>
#include <vector>

namespace globe {

// Manages GPU textures with LRU eviction
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
    
    // Evict least recently used textures if over limit
    void EvictIfNeeded(std::unordered_map<TileKey, Tile>& tiles, int maxTiles);
    
    // Create loading placeholder texture
    uint32_t CreateLoadingTexture();
    
    // Stats
    int GetTextureCount() const { return textureCount_; }
    int GetPendingUploads() const { return static_cast<int>(uploadQueue_.size()); }

private:
    uint32_t CreateTexture(const uint8_t* pixels, int width, int height);
    
    const Config& config_;
    std::queue<TileKey> uploadQueue_;
    int textureCount_ = 0;
    uint32_t loadingTexture_ = 0;
};

} // namespace globe
