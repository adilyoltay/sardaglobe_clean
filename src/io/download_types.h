#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>    // P0: For std::atomic_bool in CancelToken
#include <memory>    // P0: For std::shared_ptr in DecodeRequest

namespace globe {

// Priority levels for tile requests
enum class Priority {
    Low = 0,
    Normal = 1,
    Urgent = 2
};

// Callback type for fetch completion
using FetchCompleteCallback = std::function<void(std::vector<uint8_t>, bool)>;

// Fetch request with priority
struct FetchRequest {
    TileKey key;
    std::string url;
    Priority priority = Priority::Normal;
    double score = 0.0;  // SSE score for prioritization
    // Monotonic sequence used internally for lazy stale-skip on upgrades.
    // Filled by TileFetcher when enqueuing.
    uint64_t seq = 0;
    FetchCompleteCallback onComplete;  // Optional per-request callback
    // Optional cache callbacks (executed in worker thread)
    std::function<bool(const TileKey&, std::vector<uint8_t>&)> tryReadCache;
    std::function<void(const TileKey&, const std::vector<uint8_t>&)> writeCache;
};

// Comparison for priority queue (higher priority first, then higher score)
struct FetchRequestCompare {
    bool operator()(const FetchRequest& a, const FetchRequest& b) const {
        // Primary: priority (higher first, so reversed for max-heap)
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        // Secondary: score (higher score = more important, so reversed)
        if (a.score != b.score) {
            return a.score < b.score;
        }
        // Tertiary: FIFO for identical rank (lower seq first)
        return a.seq > b.seq;
    }
};

// Fetch result
struct FetchResult {
    TileKey key;
    std::vector<uint8_t> data;
    bool success = false;
    bool canceled = false;
    std::string error;
    long httpStatus = 0;
    Priority priority = Priority::Normal;
    double score = 0.0;
};

// Cancel token for pre-emptible operations
struct CancelToken {
    std::atomic_bool cancelled{false};
    uint64_t ticketId{0};
    
    void Cancel() { cancelled.store(true); }
    bool IsCancelled() const { return cancelled.load(); }
};

// Decode request
struct DecodeRequest {
    TileKey key;
    std::vector<uint8_t> data;
    Priority priority = Priority::Normal;
    double score = 0.0;
    std::shared_ptr<CancelToken> cancelToken;  // P0: Pre-emptible cancellation support
};

// Decode result  
struct DecodeResult {
    TileKey key;
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    bool hasTransparency = false;
    bool mostlyBlackOpaque = false;
    bool success = false;
    std::string error;
};

} // namespace globe
