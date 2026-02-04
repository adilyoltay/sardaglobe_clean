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
    double fetchTime = 0.0;       // Time when data was fetched
};

// Height sampler callback type (for mesh builder)
using HeightSampler = std::function<bool(double lonDeg, double latDeg, int level, double& heightMeters)>;

// DEM Manager configuration
struct DemManagerConfig {
    std::string baseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
    int meshN = 5;                    // Grid resolution per tile
    size_t cacheSize = 256;           // Max cached tiles
    double heightScale = 0.001;       // Meters to world units (km)
    bool debug = false;
};

// DEM Manager - handles elevation data fetching and caching
class DemManager {
public:
    using Config = DemManagerConfig;
    
    explicit DemManager(const Config& config);
    ~DemManager();
    
    // Request DEM data for a tile
    void Request(const TileKey& key);
    
    // Check if DEM data is available for a tile
    bool HasData(const TileKey& key) const;
    
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
    
    // Request queue
    std::queue<TileKey> requestQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    std::atomic<int> pendingCount_{0};
    
    // Helper functions
    void WorkerLoop();
    bool FetchDem(const TileKey& key, DemGridData& outData);
    std::string BuildDemUrl(const DemCell& cell) const;
    bool ParseDemGrid(const std::string& payload, DemGridData& outData) const;
    double SampleBilinear(const DemGridData& data, double u, double v) const;
    
    // Tile bounds calculation
    static double Tile2Lon(int x, int z);
    static double Tile2Lat(int y, int z);
};

} // namespace globe
