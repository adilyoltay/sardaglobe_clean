#pragma once

#include "i_elevation_provider.h"
#include "http_transport.h"
#include <memory>
#include <string>
#include <vector>

namespace globe {

// Configuration for Google Earth elevation provider
struct GoogleEarthElevationConfig {
    std::string endpoint;  // Elevation service endpoint (e.g., "https://kh.google.com/rpc/eh")
    std::vector<std::pair<std::string, std::string>> headers;  // Custom headers
    std::string authToken;  // Auth token (from env var)
    int elevationType = 0;  // 0=ELLIPSOID, 1=TERRAIN, 2=SEA_LEVEL
    int timeoutSec = 30;
};

// Google Earth elevation provider implementation
// Uses protobuf wire format for batch point elevation queries
class GoogleEarthElevationProvider : public IElevationProvider {
public:
    explicit GoogleEarthElevationProvider(const GoogleEarthElevationConfig& config);
    
    // Allow injection of custom transport for testing
    GoogleEarthElevationProvider(const GoogleEarthElevationConfig& config,
                                 std::unique_ptr<IHttpTransport> transport);
    
    ~GoogleEarthElevationProvider() override;

    // IElevationProvider interface
    ElevationBatchResult BatchQuery(const std::vector<GeoPoint>& points,
                                    const ElevationOptions& opt) override;
    
    bool SupportsBatchQuery() const override { return true; }
    const char* GetProviderName() const override { return "google-earth-elevation"; }

private:
    GoogleEarthElevationConfig config_;
    std::unique_ptr<IHttpTransport> transport_;
    
    // Build request headers including auth
    std::vector<std::pair<std::string, std::string>> BuildRequestHeaders() const;
    
    // Validate response (size check, etc.)
    bool ValidateResponse(const std::vector<double>& elevations, 
                          size_t expectedCount,
                          std::string& errorMessage) const;
};

} // namespace globe
