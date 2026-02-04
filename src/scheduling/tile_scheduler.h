#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include "../io/tile_fetcher.h"
#include "../io/tile_decoder.h"
#include "../io/tile_cache.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <functional>

namespace globe {

// Manages tile lifecycle: fetch -> decode -> upload
class TileScheduler {
public:
    using TileMap = std::unordered_map<TileKey, Tile>;
    using UploadCallback = std::function<void(Tile& tile)>;
    
    TileScheduler(const Config& config);
    ~TileScheduler();
    
    // Request a tile to be loaded
    void Request(const TileKey& key, Priority priority, float score = 0.0f);
    
    // Cancel a pending request
    void Cancel(const TileKey& key);
    
    // Update scheduler - process completed fetches/decodes
    void Update(TileMap& tiles, double currentTime);
    
    // Set callback for tiles ready for GPU upload
    void SetUploadCallback(UploadCallback callback);
    
    // Build URL from template
    std::string BuildUrl(const TileKey& key) const;
    
    // Stats
    int GetPendingFetches() const;
    int GetPendingDecodes() const;
    int GetActiveFetches() const;

private:
    void OnFetchComplete(FetchResult result);
    void OnDecodeComplete(DecodeResult result);
    
    const Config& config_;
    
    std::unique_ptr<TileFetcher> fetcher_;
    std::unique_ptr<TileDecoder> decoder_;
    std::unique_ptr<TileCache> cache_;
    
    // Pending results (thread-safe queues)
    std::queue<FetchResult> fetchResults_;
    std::mutex fetchResultsMutex_;
    
    std::queue<DecodeResult> decodeResults_;
    std::mutex decodeResultsMutex_;
    
    // Tracking
    std::unordered_set<TileKey> pendingFetches_;
    std::unordered_set<TileKey> pendingDecodes_;
    std::mutex trackingMutex_;
    
    UploadCallback uploadCallback_;
};

} // namespace globe
