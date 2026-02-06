#pragma once

#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <unordered_set>
#include <chrono>
#include <cstdint>

namespace globe {

// DEM cell for batch request
struct DemCell {
    int tileX = 0;
    int tileY = 0;
    int level = 0;
    double llx = 0.0;  // Lower-left X (lon)
    double lly = 0.0;  // Lower-left Y (lat)
    double urx = 0.0;  // Upper-right X (lon)
    double ury = 0.0;  // Upper-right Y (lat)
};

// DEM grid data for a tile
struct DemGridData {
    std::vector<double> heights;  // meshN x meshN grid of heights (meters)
    int meshN = 0;                // Grid resolution (e.g., 5 = 5x5 grid)
    double minHeight = 0.0;
    double maxHeight = 0.0;
    bool valid = false;
    mutable double lastAccessTime = 0.0;  // LRU: updated on every access
};

// Height sampler callback type (for mesh builder)
using HeightSampler = std::function<bool(double lonDeg, double latDeg, int level, double& heightMeters)>;

// DEM health status
enum class DemHealthStatus {
    Unknown,      // Not yet checked
    Healthy,      // Endpoint reachable, data valid
    AuthFailed,   // 401/403 - credentials/origin issue
    Unreachable,  // Network/DNS/timeout error
    BadResponse,  // 200 but unparseable data
    Disabled      // DEM explicitly disabled
};

const char* DemHealthStatusToString(DemHealthStatus s);

// DEM telemetry statistics
struct DemStats {
    std::atomic<int> fetchSuccess{0};
    std::atomic<int> fetchFail{0};
    std::atomic<int> fetchTimeout{0};
    std::atomic<int> fetchAuth{0};     // 401/403 count
    std::atomic<int> parseSuccess{0};
    std::atomic<int> parseFail{0};
    std::atomic<int> cacheHits{0};
    std::atomic<int> cacheMisses{0};
    std::atomic<double> totalFetchMs{0.0};
    
    int GetTotalFetches() const { return fetchSuccess.load() + fetchFail.load(); }
    double GetAvgFetchMs() const {
        int total = fetchSuccess.load();
        return total > 0 ? totalFetchMs.load() / total : 0.0;
    }
    double GetSuccessRate() const {
        int total = GetTotalFetches();
        return total > 0 ? 100.0 * fetchSuccess.load() / total : 0.0;
    }
};

// DEM Manager configuration
struct DemManagerConfig {
    std::string baseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
    int meshN = 5;                    // Grid resolution per tile
    size_t cacheSize = 256;           // Max cached tiles
    double heightScale = 0.001;       // Meters to world units (km)
    bool debug = false;
    
    // Network settings (consolidated)
    long timeoutSec = 30;             // CURL total timeout
    long connectTimeoutSec = 10;      // CURL connect timeout
    int maxRetries = 1;               // Max retries per tile on transient error
    double failRetryDelaySec = 30.0;  // Retry failed tiles after this delay
    int authBackoffThreshold = 3;     // Consecutive auth fails before backoff
    double authBackoffSec = 30.0;     // Backoff duration after auth failures
};

// DEM Manager - handles elevation data fetching and caching
class DemManager {
public:
    using Config = DemManagerConfig;
    
    explicit DemManager(const Config& config);
    ~DemManager();
    
    // Startup health check - tests endpoint accessibility
    // Returns health status and logs result
    DemHealthStatus CheckHealth();
    
    // Get current health status
    DemHealthStatus GetHealthStatus() const { return healthStatus_.load(); }
    
    // Get telemetry stats
    const DemStats& GetStats() const { return stats_; }
    
    // Request DEM data for a tile
    // priority: 0=low, 1=normal, 2=urgent. score: higher = sooner.
    void Request(const TileKey& key, int priority = 1, double score = 0.0);
    
    // Check if DEM data is available for a tile
    bool HasData(const TileKey& key) const;
    
    // Get raw DEM grid data for heightmap upload (returns false if not cached)
    bool GetGridData(const TileKey& key, DemGridData& outData) const;
    
    // Get height at a specific lat/lon for a tile
    bool SampleHeight(double lonDeg, double latDeg, int level, double& heightMeters) const;
    
    // Get a height sampler callback for mesh building
    HeightSampler GetHeightSampler() const;
    
    // Process pending requests (call from main thread)
    void Update();
    
    // Stats
    int GetPendingCount() const { return pendingCount_.load(); }
    int GetCacheSize() const;
    
    // Shutdown
    void Shutdown();

private:
    Config config_;
    
    // Cache: TileKey -> DemGridData
    mutable std::mutex cacheMutex_;
    std::unordered_map<TileKey, DemGridData> cache_;
    
    struct DemRequest {
        TileKey key;
        int priority = 1;
        double score = 0.0;
        uint64_t seq = 0;  // Monotonic sequence for deterministic tie-break
    };

    struct PendingRank {
        int priority = 1;
        double score = 0.0;
        uint64_t seq = 0;
    };

    struct DemRequestCompare {
        bool operator()(const DemRequest& a, const DemRequest& b) const {
            if (a.priority != b.priority) return a.priority < b.priority;
            if (a.score != b.score) return a.score < b.score;
            return a.seq > b.seq;  // Earlier request first when rank ties
        }
    };

    // Priority queue with lazy stale-entry skipping.
    std::priority_queue<DemRequest, std::vector<DemRequest>, DemRequestCompare> requestQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    std::atomic<int> pendingCount_{0};
    
    // Pending/in-flight dedupe + rank upgrades
    std::unordered_map<TileKey, PendingRank> pendingRanks_;
    uint64_t requestSeq_ = 0;
    
    // Failed tile tracking with TTL (retry after timeout)
    std::unordered_map<TileKey, std::chrono::steady_clock::time_point> failedUntil_;  // TTL cache
    static constexpr double FAIL_RETRY_SEC = 30.0;  // Retry failed tiles after 30s
    
    // Auth backoff (401/403)
    std::atomic<int> consecutiveAuthFails_{0};
    std::atomic<bool> authBackoff_{false};
    std::chrono::steady_clock::time_point backoffUntil_;
    
    // Health status and telemetry
    std::atomic<DemHealthStatus> healthStatus_{DemHealthStatus::Unknown};
    DemStats stats_;
    
    // Helper functions
    void WorkerLoop();
    bool FetchDem(const TileKey& key, DemGridData& outData);
    std::string BuildDemUrl(const DemCell& cell) const;
    DemCell BuildDemCell(const TileKey& key) const;
    bool ParseDemGrid(const std::string& payload, DemGridData& outData) const;
    double SampleBilinear(const DemGridData& data, double u, double v) const;
    
    // Tile bounds calculation (WGS84)
    static double Tile2Lon(int x, int z);
    static double Tile2Lat(int y, int z);
};

} // namespace globe
