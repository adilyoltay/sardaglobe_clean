#pragma once

#include "i_terrain_dem_provider.h"
#include "google_earth_elevation_provider.h"
#include <memory>
#include <atomic>

namespace globe {

// Configuration for Google Earth DEM tile provider
struct GoogleEarthDemConfig {
    std::string elevationEndpoint;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string authToken;
    int elevationType = 0;  // 0=ELLIPSOID, 1=TERRAIN, 2=SEA_LEVEL
    int meshN = 17;         // Grid resolution per tile
    int maxZoom = 22;       // Provider max zoom cap
    int timeoutSec = 30;
};

// Google Earth DEM tile provider
// Uses batch point elevation queries to build tile DEM grids
class GoogleEarthDemProvider : public ITerrainDemProvider {
public:
    explicit GoogleEarthDemProvider(const GoogleEarthDemConfig& config);
    
    // Allow injection of elevation provider for testing
    explicit GoogleEarthDemProvider(std::unique_ptr<IElevationProvider> elevationProvider,
                                    int meshN = 17,
                                    const std::string& elevationEndpoint = "");
    
    ~GoogleEarthDemProvider() override;

    // ITerrainDemProvider interface
    bool FetchDemTile(const TileKey& key, DemGridData& outData, 
                      DemFetchResult& outResult) override;
    
    DemHealthStatus CheckHealth() override;
    DemHealthStatus GetHealthStatus() const override { return healthStatus_.load(); }
    bool IsTerminalError() const override { return false; }
    const char* GetProviderName() const override { return "google-earth-dem"; }

private:
    std::unique_ptr<IElevationProvider> elevationProvider_;
    int meshN_;
    int maxZoom_ = 22;
    std::string elevationEndpoint_;  // For NetworkPanel logging
    std::atomic<DemHealthStatus> healthStatus_{DemHealthStatus::Unknown};
    
    // Generate grid of points for a tile
    // Returns points in row-major order: south->north, west->east
    // This matches DemGridData expectations
    std::vector<GeoPoint> GenerateTileGrid(const TileKey& key) const;
    
    // Convert elevation batch result to DemGridData
    // Input heights must be in same order as GenerateTileGrid output
    bool ConvertToDemGridData(const std::vector<double>& heights,
                              DemGridData& outData,
                              DemFetchResult& outResult) const;
    
    // Tile bounds calculation (WGS84)
    static double Tile2Lon(int x, int z);
    static double Tile2Lat(int y, int z);
};

} // namespace globe
