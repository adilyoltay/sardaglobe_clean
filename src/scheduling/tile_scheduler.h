#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include "../io/tile_fetcher.h"
#include "../io/tile_decoder.h"
#include "../io/tile_cache.h"
#include "../io/memory_tile_cache.h"
#include "../io/tile_url_template.h"
#include "../core/bounded_queue.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <functional>
#include <atomic>

namespace globe {

// Manages tile lifecycle: fetch -> decode -> upload
class TileScheduler {
public:
    using TileMap = std::unordered_map<TileKey, Tile>;
    using UploadCallback = std::function<void(Tile& tile)>;

    struct SchedulerStats {
        int pendingFetches = 0;
        int pendingDecodes = 0;
        int activeFetches = 0;
        size_t fetchResultQueue = 0;
        size_t decodeResultQueue = 0;
        size_t droppedFetchResults = 0;
        size_t droppedDecodeResults = 0;
        size_t decodedCacheReadHits = 0;
        size_t decodedCacheReadMisses = 0;
        size_t decodedCacheWrites = 0;
        size_t decodedCacheWriteRejects = 0;
        size_t decodedCacheEvictions = 0;
        size_t decodedCacheEntries = 0;
        size_t decodedCacheBytesUsed = 0;
        size_t decodeBypassHits = 0;
        size_t memoryCacheReadHits = 0;
        size_t memoryCacheReadMisses = 0;
        size_t memoryCacheWrites = 0;
        size_t memoryCacheWriteRejects = 0;
        size_t memoryCacheEvictions = 0;
        size_t memoryCacheEntries = 0;
        size_t memoryCacheBytesUsed = 0;
        // P3.1 cache telemetry (Disk layer + derived network usage)
        size_t diskCacheReadHits = 0;
        size_t diskCacheReadMisses = 0;
        size_t diskCacheWrites = 0;
        size_t diskCacheWriteFails = 0;
        size_t totalFetchRequests = 0;
        size_t networkFetches = 0;
        double avgFetchMs = 0.0;
        double avgDecodeMs = 0.0;
        // HTTP failure breakdown (raster)
        size_t fetchAuthFails = 0;     // 401/403
        size_t fetchRateLimited = 0;   // 429
        size_t fetchHttpErrors = 0;    // non-200 HTTP responses
    };
    
    // Queue limits to prevent unbounded memory growth
    static constexpr size_t MAX_RESULT_QUEUE = 256;
    
    TileScheduler(const Config& config);
    ~TileScheduler();
    
    // Request a tile to be loaded
    // Returns true if the request was enqueued, false if it was skipped due to
    // backpressure or because the tile is already in-flight.
    bool Request(const TileKey& key, Priority priority, float score = 0.0f);
    
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
    SchedulerStats GetStats() const;
    
    // Drop metrics (queue overflow)
    size_t GetDroppedFetchResults() const { return droppedFetchResults_; }
    size_t GetDroppedDecodeResults() const { return droppedDecodeResults_; }
    
    // Fetch fail tracking (for debug/telemetry)
    size_t GetRecentFetchFails() const { return recentFetchFails_; }
    void ResetRecentFetchFails() { recentFetchFails_ = 0; }
    
    // Cache pinning: Görünür tile'ların eviction'dan korunması
    void PinCacheEntry(const TileKey& key);
    void UnpinAllCacheEntries();

private:
    void OnFetchComplete(FetchResult result);
    void OnDecodeComplete(DecodeResult result);
    
    const Config& config_;
    
    std::unique_ptr<TileFetcher> fetcher_;
    std::unique_ptr<TileDecoder> decoder_;
    std::unique_ptr<MemoryTileCache> decodedCache_;
    std::unique_ptr<MemoryTileCache> memoryCache_;
    std::unique_ptr<TileCache> cache_;
    
    // Pending results (thread-safe queues)
    BoundedQueue<FetchResult> fetchResults_{MAX_RESULT_QUEUE};
    BoundedQueue<DecodeResult> decodeResults_{MAX_RESULT_QUEUE};
    
    // Tracking
    std::unordered_set<TileKey> pendingFetches_;
    struct PendingFetchRank {
        Priority priority = Priority::Normal;
        double score = 0.0;
    };
    std::unordered_map<TileKey, PendingFetchRank> pendingFetchRanks_;
    std::unordered_set<TileKey> pendingDecodes_;
    std::unordered_set<TileKey> canceledKeys_;
    std::queue<TileKey> canceledTransitions_;
    std::mutex trackingMutex_;
    
    UploadCallback uploadCallback_;
    
    // Drop counters (queue overflow metrics) - atomic for thread-safe reads
    std::atomic<size_t> droppedFetchResults_{0};
    std::atomic<size_t> droppedDecodeResults_{0};
    
    // Dropped keys queue - Update() marks these tiles as Failed for retry
    std::queue<TileKey> droppedKeys_;
    std::mutex droppedKeysMutex_;
    
    // Recent fetch fail counter (reset periodically)
    std::atomic<size_t> recentFetchFails_{0};

    // Raster HTTP telemetry (cumulative)
    std::atomic<size_t> fetchAuthFails_{0};
    std::atomic<size_t> fetchRateLimited_{0};
    std::atomic<size_t> fetchHttpErrors_{0};

    // URL template (regex-free)
    TileUrlTemplate urlTemplate_;
};

} // namespace globe
