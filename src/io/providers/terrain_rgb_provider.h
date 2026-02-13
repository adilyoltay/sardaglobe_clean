#pragma once

#include "i_terrain_dem_provider.h"
#include <string>
#include <atomic>

struct CURL_slist;

namespace globe {

// Configuration for TerrainRGBProvider
struct TerrainRGBConfig {
    std::string baseUrl = "https://api.maptiler.com/tiles/terrain-rgb-v2/{z}/{x}/{y}.png";
    std::string apiKey;                    // Optional API key appended to URL
    std::string basicAuthUserPwd;          // Optional HTTP basic auth
    long timeoutSec = 30;
    long connectTimeoutSec = 10;
    int meshN = 17;                        // Grid resolution per tile
    int maxZoom = 15;                      // Max zoom level for requests
    bool debug = false;
};

// Terrain-RGB tile provider implementation.
// Fetches PNG tiles and decodes RGB values to elevation meters.
class TerrainRGBProvider : public ITerrainDemProvider {
public:
    explicit TerrainRGBProvider(const TerrainRGBConfig& config);
    ~TerrainRGBProvider() override;

    // ITerrainDemProvider interface
    bool FetchDemTile(const TileKey& key, DemGridData& outData) override;
    DemHealthStatus CheckHealth() override;
    DemHealthStatus GetHealthStatus() const override { return healthStatus_.load(); }
    bool IsTerminalError() const override { return false; }  // Terrain-RGB never terminal
    const char* GetProviderName() const override { return "terrain-rgb"; }

private:
    TerrainRGBConfig config_;
    std::atomic<DemHealthStatus> healthStatus_{DemHealthStatus::Unknown};
    
    // Build URL for a tile
    std::string BuildUrl(const TileKey& key, int effectiveLevel) const;
    
    // Perform HTTP fetch
    bool HttpFetch(const std::string& url, std::vector<uint8_t>& outData);
    
    // Decode PNG to DemGridData
    bool DecodeTile(const std::vector<uint8_t>& pngData, DemGridData& outData);
};

} // namespace globe
