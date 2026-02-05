#pragma once

#include "../core/tile_key.h"
#include "../io/dem_manager.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>

namespace globe {

struct HeightmapTexture {
    uint32_t textureId = 0;
    int width = 0;
    int height = 0;
    float minHeight = 0.0f;  // Min height in km (for shader normalization)
    float maxHeight = 0.0f;  // Max height in km
    bool valid = false;
};

struct HeightmapUploadRequest {
    TileKey key;
    std::vector<float> normalizedHeights;  // [0,1] normalized
    int gridSize = 0;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
};

class HeightmapManager {
public:
    HeightmapManager() = default;
    ~HeightmapManager();
    
    // Queue a DEM tile for GPU upload
    void QueueUpload(const TileKey& key, const DemGridData& demData, float heightScale);
    
    // Process pending uploads (call from main thread with GL context)
    // Returns number of uploads processed
    int ProcessUploads(double budgetMs);
    
    // Get heightmap texture for a tile (returns false if not uploaded)
    bool GetTexture(const TileKey& key, HeightmapTexture& out) const;
    
    // Check if heightmap exists for tile
    bool HasTexture(const TileKey& key) const;
    
    // Release heightmap texture
    void Release(const TileKey& key);
    
    // Release all textures
    void Clear();
    
    // Stats
    int GetCacheSize() const;
    int GetPendingCount() const;

private:
    // Create R16F texture from normalized heights
    HeightmapTexture CreateTexture(const HeightmapUploadRequest& req);
    
    // Cache: TileKey -> HeightmapTexture
    mutable std::mutex cacheMutex_;
    std::unordered_map<TileKey, HeightmapTexture> cache_;
    
    // Upload queue (processed on main thread)
    mutable std::mutex queueMutex_;
    std::queue<HeightmapUploadRequest> uploadQueue_;
    std::unordered_set<TileKey> pendingKeys_;  // Dedupe: keys currently in queue
    std::unordered_set<TileKey> canceledKeys_; // Keys canceled while queued/in-flight
};

} // namespace globe
