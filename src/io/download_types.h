#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <functional>

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
    FetchCompleteCallback onComplete;  // Optional per-request callback
};

// Comparison for priority queue (higher priority first)
struct FetchRequestCompare {
    bool operator()(const FetchRequest& a, const FetchRequest& b) const {
        return a.priority < b.priority;  // Reversed for max-heap
    }
};

// Fetch result
struct FetchResult {
    TileKey key;
    std::vector<uint8_t> data;
    bool success = false;
    std::string error;
    long httpStatus = 0;
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
    std::string error;
};

} // namespace globe
