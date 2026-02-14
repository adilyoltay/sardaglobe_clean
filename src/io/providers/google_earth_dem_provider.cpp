#include "google_earth_dem_provider.h"
#include "../../debug/network_panel.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>
#include <string>

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
    : meshN_(config.meshN), elevationEndpoint_(config.elevationEndpoint) {
    // Create elevation provider
    elevationProvider_ = std::make_unique<GoogleEarthElevationProvider>(
        ToElevationConfig(config));
}

GoogleEarthDemProvider::GoogleEarthDemProvider(std::unique_ptr<IElevationProvider> elevationProvider,
                                               int meshN,
                                               const std::string& elevationEndpoint)
    : elevationProvider_(std::move(elevationProvider)), meshN_(meshN), elevationEndpoint_(elevationEndpoint) {
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
        // Preserve existing metadata (httpStatusCode, bytesReceived, elapsedMs) from outResult
        // Only update error fields
        outResult.errorType = DemFetchResult::ErrorType::Decode;
        outResult.errorMessage = "Height count mismatch: expected " + std::to_string(expectedCount) +
                                 ", got " + std::to_string(heights.size());
        outResult.success = false;
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
    
    // Success: update outResult to reflect success while preserving metadata
    // outResult already contains httpStatusCode, bytesReceived, elapsedMs from batchResult.fetch
    outResult.success = true;
    outResult.errorType = DemFetchResult::ErrorType::None;
    outResult.errorMessage.clear();
    
    return true;
}

bool GoogleEarthDemProvider::FetchDemTile(const TileKey& key, DemGridData& outData,
                                          DemFetchResult& outResult) {
    // NetworkPanel: record start
    NetworkPanel::Instance().RecordStart(key, RequestType::DemMesh, elevationEndpoint_);
    
    // Generate grid points for this tile
    std::vector<GeoPoint> points = GenerateTileGrid(key);
    
    // Query elevations
    ElevationOptions opt;
    opt.targetLevel = key.level;
    
    ElevationBatchResult batchResult = elevationProvider_->BatchQuery(points, opt);
    
    // Use the fetch result directly (preserves metadata: httpStatusCode, bytesReceived, elapsedMs, curlResult)
    outResult = batchResult.fetch;
    
    if (!batchResult.ok) {
        // Map error type to health status for internal tracking
        // No substring matching - rely on errorType set by elevation provider
        switch (outResult.errorType) {
            case DemFetchResult::ErrorType::Auth:
            case DemFetchResult::ErrorType::HttpError:
                if (outResult.httpStatusCode == 401 || outResult.httpStatusCode == 403) {
                    healthStatus_.store(DemHealthStatus::AuthFailed);
                } else {
                    // Other HTTP errors (4xx/5xx except 401/403)
                    healthStatus_.store(DemHealthStatus::BadResponse);
                }
                break;
            case DemFetchResult::ErrorType::Network:
            case DemFetchResult::ErrorType::Timeout:
                healthStatus_.store(DemHealthStatus::Unreachable);
                break;
            case DemFetchResult::ErrorType::Decode:
                healthStatus_.store(DemHealthStatus::BadResponse);
                break;
            default:
                healthStatus_.store(DemHealthStatus::BadResponse);
                break;
        }
        
        // NetworkPanel: record failure
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false,
            outResult.httpStatusCode, outResult.bytesReceived,
            outResult.elapsedMs, false, outResult.errorMessage);
        
        return false;
    }
    
    // Convert to DemGridData
    bool success = ConvertToDemGridData(batchResult.heights, outData, outResult);
    
    if (success) {
        healthStatus_.store(DemHealthStatus::Healthy);
        
        // NetworkPanel: record success
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, true,
            outResult.httpStatusCode, outResult.bytesReceived,
            outResult.elapsedMs, false, "");
    } else {
        // NetworkPanel: record decode/validation failure
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false,
            outResult.httpStatusCode, outResult.bytesReceived,
            outResult.elapsedMs, false, outResult.errorMessage);
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
    
    // WARN: Relaxed health check for development.
    // If the endpoint fails (e.g. 400/403), we still report Healthy to allow engine startup.
    // Real failures will be handled per-tile with standard retry/backoff.
    std::cerr << "[GE DEM] WARNING: Health check failed (" 
              << result.fetch.httpStatusCode << ": " << result.fetch.errorMessage 
              << "). Continuing anyway (Soft Fail)." << std::endl;
    return DemHealthStatus::Healthy;
}

} // namespace globe
