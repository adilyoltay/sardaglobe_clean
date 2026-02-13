#pragma once

#include <vector>
#include <string>
#include <optional>
#include "dem_fetch_result.h"

namespace globe {

// Geographic point in WGS84
struct GeoPoint {
    double lonDeg;
    double latDeg;
};

// Elevation query options
struct ElevationOptions {
    int targetLevel = 15;        // Target tile level for elevation resolution
    int elevationType = 0;       // Provider-specific elevation type (e.g., mean sea level)
};

// Batch elevation query result
struct ElevationBatchResult {
    bool ok = false;
    std::vector<double> heights;  // Heights in meters, same order as input points
    std::string error;            // Error message if ok == false
    DemFetchResult fetch;         // Transport/HTTP metadata for telemetry/backoff
};

// Interface for point elevation providers.
// Used for height sampling at arbitrary lat/lon coordinates.
// Primary use case: Google Earth elevation batch queries (Phase 4).
class IElevationProvider {
public:
    virtual ~IElevationProvider() = default;

    // Batch query elevations for multiple points.
    // Returns ElevationBatchResult with heights vector same size as input points.
    // On failure, ok == false and error contains description.
    virtual ElevationBatchResult BatchQuery(const std::vector<GeoPoint>& points,
                                            const ElevationOptions& opt) = 0;

    // Check if provider supports batch queries (may be false for some providers).
    virtual bool SupportsBatchQuery() const = 0;

    // Get provider type identifier.
    virtual const char* GetProviderName() const = 0;
};

} // namespace globe
