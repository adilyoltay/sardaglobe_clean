#include "google_earth_dem_provider.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace globe {

// Helper to create elevation config from DEM config
static GoogleEarthElevationConfig ToElevationConfig(const GoogleEarthDemConfig& config) {
    GoogleEarthElevationConfig elevConfig;
    elevConfig.endpoint = config.elevationEndpoint;
    elevConfig.headers = config.headers;
    elevConfig.authToken = config.authToken;
    elevConfig.elevationType = config.elevationType;
    elevConfig.timeoutSec = config.timeoutSec;
    return elevConfig;
}

GoogleEarthDemProvider::GoogleEarthDemProvider(const GoogleEarthDemConfig& config)
    : meshN_(config.meshN) {
    // Create elevation provider
    elevationProvider_ = std::make_unique<GoogleEarthElevationProvider>(
        ToElevationConfig(config));
}

GoogleEarthDemProvider::GoogleEarthDemProvider(std::unique_ptr<IElevationProvider> elevationProvider,
                                               int meshN)
    : elevationProvider_(std::move(elevationProvider)), meshN_(meshN) {
}

GoogleEarthDemProvider::~GoogleEarthDemProvider() = default;

double GoogleEarthDemProvider::Tile2Lon(int x, int z) {
    return x / static_cast<double>(1 << z) * 360.0 - 180.0;
}

double GoogleEarthDemProvider::Tile2Lat(int y, int z) {
    double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << z);
    return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

std::vector<GeoPoint> GoogleEarthDemProvider::GenerateTileGrid(const TileKey& key) const {
    std::vector<GeoPoint> points;
    points.reserve(static_cast<size_t>(meshN_) * meshN_);
    
    // Calculate tile bounds
    double lonWest = Tile2Lon(key.x, key.level);
    double lonEast = Tile2Lon(key.x + 1, key.level);
    double latNorth = Tile2Lat(key.y, key.level);
    double latSouth = Tile2Lat(key.y + 1, key.level);
    
    // Generate grid points
    // Row order: south->north (gy=0 is south, gy=meshN-1 is north)
    // Col order: west->east (gx=0 is west, gx=meshN-1 is east)
    for (int gy = 0; gy < meshN_; ++gy) {
        double v = (meshN_ == 1) ? 0.0 : static_cast<double>(gy) / (meshN_ - 1);
        double lat = latSouth + (latNorth - latSouth) * v;
        
        for (int gx = 0; gx < meshN_; ++gx) {
            double u = (meshN_ == 1) ? 0.0 : static_cast<double>(gx) / (meshN_ - 1);
            double lon = lonWest + (lonEast - lonWest) * u;
            
            points.push_back({lon, lat});
        }
    }
    
    return points;
}

bool GoogleEarthDemProvider::ConvertToDemGridData(const std::vector<double>& heights,
                                                   DemGridData& outData,
                                                   DemFetchResult& outResult) const {
    size_t expectedCount = static_cast<size_t>(meshN_) * meshN_;
    
    if (heights.size() != expectedCount) {
        outResult = DemFetchResult::DecodeError(
            "Height count mismatch: expected " + std::to_string(expectedCount) +
            ", got " + std::to_string(heights.size()));
        return false;
    }
    
    outData = DemGridData{};
    outData.meshN = meshN_;
    outData.heights = heights;
    outData.valid = true;
    
    // Calculate min/max
    outData.minHeight = std::numeric_limits<double>::max();
    outData.maxHeight = std::numeric_limits<double>::lowest();
    
    for (double h : heights) {
        outData.minHeight = std::min(outData.minHeight, h);
        outData.maxHeight = std::max(outData.maxHeight, h);
    }
    
    // Success result with preserved metadata (if any)
    // Caller should set HTTP-related fields
    outResult = DemFetchResult::Success(200, 0, 0.0);
    
    return true;
}

bool GoogleEarthDemProvider::FetchDemTile(const TileKey& key, DemGridData& outData,
                                          DemFetchResult& outResult) {
    // Generate grid points for this tile
    std::vector<GeoPoint> points = GenerateTileGrid(key);
    
    // Query elevations
    ElevationOptions opt;
    opt.targetLevel = key.level;
    
    ElevationBatchResult batchResult = elevationProvider_->BatchQuery(points, opt);
    
    if (!batchResult.ok) {
        // Map error to DemFetchResult
        if (batchResult.error.find("401") != std::string::npos ||
            batchResult.error.find("Authentication") != std::string::npos) {
            outResult = DemFetchResult::AuthError(401, batchResult.error);
            healthStatus_.store(DemHealthStatus::AuthFailed);
        } else if (batchResult.error.find("403") != std::string::npos ||
                   batchResult.error.find("Access denied") != std::string::npos) {
            outResult = DemFetchResult::AuthError(403, batchResult.error);
            healthStatus_.store(DemHealthStatus::AuthFailed);
        } else if (batchResult.error.find("Network") != std::string::npos) {
            outResult = DemFetchResult::NetworkError(0, batchResult.error, 0.0);
            healthStatus_.store(DemHealthStatus::Unreachable);
        } else {
            outResult = DemFetchResult::DecodeError(batchResult.error);
            healthStatus_.store(DemHealthStatus::BadResponse);
        }
        return false;
    }
    
    // Convert to DemGridData
    bool success = ConvertToDemGridData(batchResult.heights, outData, outResult);
    
    if (success) {
        healthStatus_.store(DemHealthStatus::Healthy);
    }
    
    return success;
}

DemHealthStatus GoogleEarthDemProvider::CheckHealth() {
    // Perform a simple health check with a single point query
    std::vector<GeoPoint> testPoints = {{0.0, 0.0}};  // Equator/Prime meridian
    ElevationOptions opt;
    
    ElevationBatchResult result = elevationProvider_->BatchQuery(testPoints, opt);
    
    if (result.ok && !result.heights.empty()) {
        healthStatus_.store(DemHealthStatus::Healthy);
        return DemHealthStatus::Healthy;
    }
    
    // Map error to health status
    if (result.error.find("401") != std::string::npos ||
        result.error.find("403") != std::string::npos) {
        healthStatus_.store(DemHealthStatus::AuthFailed);
    } else if (result.error.find("Network") != std::string::npos) {
        healthStatus_.store(DemHealthStatus::Unreachable);
    } else {
        healthStatus_.store(DemHealthStatus::BadResponse);
    }
    
    return healthStatus_.load();
}

} // namespace globe
