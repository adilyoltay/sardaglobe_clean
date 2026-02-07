#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include "texture_atlas_allocator.h"
#include <queue>
#include <unordered_map>
#include <vector>
#include <functional>

namespace globe {

// Callback for tile eviction notification (used by HeightmapManager)
using TileEvictionCallback = std::function<void(const TileKey&)>;

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

    // Release tile GPU resources (owned texture and/or atlas slot).
    void ReleaseTileResources(Tile& tile);
    
    // Pin/Unpin API (GE-style cache policy)
    // Pinned tiles are protected from eviction
    void BeginPinEpoch();
    void PinTile(Tile& tile);
    bool IsPinned(const Tile& tile) const;
    int GetPinnedCount() const { return pinnedCount_; }
    
    // Evict least recently used textures if over limit
    // Respects pinned tiles - they won't be evicted
    void EvictIfNeeded(std::unordered_map<TileKey, Tile>& tiles, int maxTiles);
    
    // Set callback for tile eviction notification (e.g., to release heightmap textures)
    void SetEvictionCallback(TileEvictionCallback callback) { evictionCallback_ = std::move(callback); }
    
    // Create loading placeholder texture
    uint32_t CreateLoadingTexture();
    
    // Get loading placeholder texture (creates if needed)
    uint32_t GetLoadingTexture();
    
    // Stats
    int GetTextureCount() const { return textureCount_; }
    int GetPendingUploads() const { return static_cast<int>(uploadQueue_.size()); }
    int GetLastEvictedCount() const { return lastEvictedCount_; }
    bool IsAtlasEnabled() const { return atlasEnabled_; }
    int GetAtlasPageCount() const { return atlasAllocator_.GetPageCount(); }
    int GetAtlasUsedSlots() const { return atlasAllocator_.GetUsedSlots(); }
    int GetAtlasCapacitySlots() const { return atlasAllocator_.GetTotalCapacity(); }

private:
    uint32_t CreateTexture(const uint8_t* pixels, int width, int height);
    uint32_t CreateAtlasPageTexture();
    bool EnsureAtlasPageTexture(int pageIndex);
    bool UploadToAtlas(Tile& tile);
    void ReleaseAtlasAllocation(Tile& tile);
    void TrimTrailingEmptyAtlasPages();
    bool CopyAtlasRegion(uint32_t srcTextureId, int srcX, int srcY,
                         uint32_t dstTextureId, int dstX, int dstY,
                         int width, int height);
    int CompactAtlas(std::unordered_map<TileKey, Tile>& tiles, int maxMoves, double budgetMs);
    
    const Config& config_;
    struct UploadJob {
        TileKey key;
        uint8_t priority = 1;
        double score = 0.0;
        uint64_t sequence = 0;
    };
    struct UploadJobCompare {
        bool operator()(const UploadJob& a, const UploadJob& b) const {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            if (a.score != b.score) {
                return a.score < b.score;
            }
            return a.sequence > b.sequence;
        }
    };
    std::priority_queue<UploadJob, std::vector<UploadJob>, UploadJobCompare> uploadQueue_;
    uint64_t uploadSequence_ = 0;
    int textureCount_ = 0;
    uint32_t loadingTexture_ = 0;

    // Shared texture atlas (P3.2 slice).
    bool atlasEnabled_ = false;
    bool atlasMipmaps_ = false;
    TextureAtlasAllocator atlasAllocator_;
    std::vector<uint32_t> atlasPageTextures_;
    
    // Pin/unpin support (GE-style cache policy)
    uint32_t pinEpoch_ = 1;
    int pinnedCount_ = 0;
    int lastEvictedCount_ = 0;
    
    // Eviction callback (for heightmap cleanup)
    TileEvictionCallback evictionCallback_;
};

} // namespace globe
