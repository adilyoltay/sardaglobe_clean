#pragma once

#include "../../core/tile_key.h"
#include "../dem_manager.h"
#include "dem_fetch_result.h"
#include <string>

namespace globe {

// Interface for terrain DEM tile providers.
// Implementations fetch elevation data for a specific tile and populate
// DemGridData with height samples.
class ITerrainDemProvider {
public:
    virtual ~ITerrainDemProvider() = default;

    // Fetch DEM data for a tile. Returns true on success, false on failure.
    // On success, outData is populated with valid height grid.
    // On failure, outData should be left unchanged or marked invalid.
    // outResult is populated with detailed outcome for telemetry/backoff.
    virtual bool FetchDemTile(const TileKey& key, DemGridData& outData, 
                              DemFetchResult& outResult) = 0;

    // Perform a startup health check. Returns Healthy if the provider
    // is functional and can serve requests.
    virtual DemHealthStatus CheckHealth() = 0;

    // Get current health status (cached from last check or operation).
    virtual DemHealthStatus GetHealthStatus() const = 0;

    // True if provider is in terminal error state (unrecoverable).
    // When true, FetchDemTile may be skipped to prevent log spam.
    virtual bool IsTerminalError() const = 0;

    // Get provider type identifier for telemetry/logging.
    virtual const char* GetProviderName() const = 0;
};

} // namespace globe
