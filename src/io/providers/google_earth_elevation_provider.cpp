#include "google_earth_elevation_provider.h"
#include "protobuf_wire.h"
#include <iostream>
#include <algorithm>

namespace globe {

GoogleEarthElevationProvider::GoogleEarthElevationProvider(const GoogleEarthElevationConfig& config)
    : config_(config) {
    // Create default CURL transport
    HttpTransportConfig transportConfig;
    transportConfig.timeoutSec = config.timeoutSec;
    transport_ = std::make_unique<CurlHttpTransport>(transportConfig);
}

GoogleEarthElevationProvider::GoogleEarthElevationProvider(
    const GoogleEarthElevationConfig& config,
    std::unique_ptr<IHttpTransport> transport)
    : config_(config), transport_(std::move(transport)) {
}

GoogleEarthElevationProvider::~GoogleEarthElevationProvider() = default;

std::vector<std::pair<std::string, std::string>> 
GoogleEarthElevationProvider::BuildRequestHeaders() const {
    auto headers = config_.headers;
    
    // Add auth token if present
    if (!config_.authToken.empty()) {
        headers.push_back({"Authorization", "Bearer " + config_.authToken});
    }
    
    // Add content type for protobuf
    headers.push_back({"Content-Type", "application/x-protobuf"});
    headers.push_back({"Accept", "application/x-protobuf"});
    
    return headers;
}

bool GoogleEarthElevationProvider::ValidateResponse(const std::vector<double>& elevations,
                                                     size_t expectedCount,
                                                     std::string& errorMessage) const {
    if (elevations.size() != expectedCount) {
        errorMessage = "Elevation count mismatch: expected " + std::to_string(expectedCount) +
                       ", got " + std::to_string(elevations.size());
        return false;
    }
    
    // Check for invalid values (NaN, extreme values)
    for (size_t i = 0; i < elevations.size(); ++i) {
        double elev = elevations[i];
        if (std::isnan(elev)) {
            errorMessage = "Invalid elevation (NaN) at index " + std::to_string(i);
            return false;
        }
        // Allow -1000m (Dead Sea) to +9000m (Everest)
        if (elev < -2000.0 || elev > 10000.0) {
            errorMessage = "Elevation out of range at index " + std::to_string(i) +
                           ": " + std::to_string(elev);
            return false;
        }
    }
    
    return true;
}

ElevationBatchResult GoogleEarthElevationProvider::BatchQuery(const std::vector<GeoPoint>& points,
                                                               const ElevationOptions& opt) {
    ElevationBatchResult result;
    
    if (points.empty()) {
        result.ok = true;
        return result;
    }
    
    // Validate endpoint
    if (config_.endpoint.empty()) {
        result.error = "Google Earth elevation endpoint not configured";
        return result;
    }
    
    // Extract lat/lon arrays
    std::vector<double> lats;
    std::vector<double> lons;
    lats.reserve(points.size());
    lons.reserve(points.size());
    
    for (const auto& point : points) {
        lats.push_back(point.latDeg);
        lons.push_back(point.lonDeg);
    }
    
    // Build protobuf request
    std::vector<uint8_t> requestBody;
    try {
        requestBody = protobuf::ge::BuildElevationRequest(lats, lons, config_.elevationType);
    } catch (const std::exception& e) {
        result.error = std::string("Failed to build request: ") + e.what();
        return result;
    }
    
    // Perform HTTP POST
    auto headers = BuildRequestHeaders();
    HttpResponse httpResponse = transport_->Post(config_.endpoint, requestBody, headers);
    
    // Check HTTP result
    if (!httpResponse.success) {
        result.error = "HTTP request failed: " + httpResponse.errorMessage;
        
        // Map HTTP errors to specific error messages
        if (httpResponse.httpCode == 401) {
            result.error = "Authentication failed (401). Check GE auth token.";
        } else if (httpResponse.httpCode == 403) {
            result.error = "Access denied (403). Check GE permissions.";
        } else if (httpResponse.httpCode == 0) {
            result.error = "Network error: " + httpResponse.errorMessage;
        }
        
        return result;
    }
    
    // Parse protobuf response
    std::vector<double> elevations;
    try {
        elevations = protobuf::ge::ParseElevationResponse(httpResponse.body);
    } catch (const std::exception& e) {
        result.error = std::string("Failed to parse response: ") + e.what();
        return result;
    }
    
    // Validate response
    std::string validationError;
    if (!ValidateResponse(elevations, points.size(), validationError)) {
        result.error = validationError;
        return result;
    }
    
    // Success
    result.ok = true;
    result.heights = std::move(elevations);
    return result;
}

} // namespace globe
