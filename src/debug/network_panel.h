#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>

namespace globe {

// Network request type
enum class RequestType {
    RasterTile,   // Image tile fetch
    DemMesh       // DEM/elevation mesh fetch
};

// Single network request record
struct NetworkRequest {
    TileKey key;
    RequestType type = RequestType::RasterTile;
    std::string url;
    
    // Timing
    double startTimeMs = 0.0;      // When request started (relative to app start)
    double durationMs = 0.0;       // Total duration
    
    // Result
    bool success = false;
    long httpStatus = 0;
    size_t bytesReceived = 0;
    std::string error;
    
    // Cache
    bool cacheHit = false;
    
    // State
    bool complete = false;
};

// Network debug panel - browser-style network inspector
class NetworkPanel {
public:
    static constexpr size_t MAX_REQUESTS = 500;  // Ring buffer size
    
    static NetworkPanel& Instance();
    
    // Record a new request (call when starting fetch)
    void RecordStart(const TileKey& key, RequestType type, const std::string& url);
    
    // Record completion (call when fetch completes)
    void RecordComplete(const TileKey& key, RequestType type, bool success, 
                        long httpStatus, size_t bytes, double durationMs,
                        bool cacheHit = false, const std::string& error = "");
    
    // UI rendering (call from ImGui frame)
    void Render();
    
    // Clear all records
    void Clear();
    
    // Stats
    struct Stats {
        int totalRequests = 0;
        int successCount = 0;
        int failCount = 0;
        int cacheHits = 0;
        double avgDurationMs = 0.0;
        size_t totalBytes = 0;
    };
    Stats GetStats() const;
    
    // Filter state
    bool showRaster = true;
    bool showDem = true;
    bool showOnlyFailed = false;
    bool autoscroll = true;
    bool panelOpen = false;

private:
    NetworkPanel() = default;
    
    mutable std::mutex mutex_;
    std::deque<NetworkRequest> requests_;
    double appStartTime_ = 0.0;
    bool initialized_ = false;
    
    // Find pending request by key+type
    NetworkRequest* FindPending(const TileKey& key, RequestType type);
};

} // namespace globe
