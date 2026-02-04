#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <functional>

namespace globe {

// Download priority levels (lower = higher priority)
enum class Priority {
    Urgent = 0,    // Visible leaf tiles
    High = 1,      // Visible parent/fallback tiles
    Normal = 2,    // Prefetch tiles
    Low = 3        // Background loading
};

// Download request
struct FetchRequest {
    TileKey key;
    std::string url;
    Priority priority = Priority::Normal;
    float score = 0.0f;           // For fine-grained ordering
    double queueTime = 0.0;
    int retryCount = 0;
    
    // Callback when complete
    using Callback = std::function<void(std::vector<uint8_t> data, bool success)>;
    Callback onComplete;
};

// Download result
struct FetchResult {
    TileKey key;
    std::vector<uint8_t> data;
    bool success = false;
    int httpStatus = 0;
    std::string error;
};

// Decode request
struct DecodeRequest {
    TileKey key;
    std::vector<uint8_t> data;
};

// Decode result
struct DecodeResult {
    TileKey key;
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    bool success = false;
};

// Request comparator for priority queue
struct FetchRequestCompare {
    bool operator()(const FetchRequest& a, const FetchRequest& b) const {
        // Lower priority value = higher priority
        if (static_cast<int>(a.priority) != static_cast<int>(b.priority)) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        }
        // Same priority: higher score wins
        if (a.score != b.score) {
            return a.score < b.score;
        }
        // Tie-break: prefer lower zoom (parents first for fallback)
        if (a.key.level != b.key.level) {
            return a.key.level > b.key.level;
        }
        // FIFO
        return a.queueTime > b.queueTime;
    }
};

} // namespace globe
