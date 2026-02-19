// Google Earth NodeData Client
// Fetches RockTree NodeData meshes from GE endpoint
// Sprint 1: Single quadkey vertical slice

#pragma once

#include "http_transport.h"
#include "../../core/config.h"
#include <memory>
#include <string>
#include <vector>

namespace globe {

// NodeData fetch result
struct NodeDataResult {
    bool success = false;
    long httpCode = 0;
    int curlResult = 0;  // CURLcode (e.g., 0=OK, 28=timeout)
    std::vector<uint8_t> data;
    std::string errorMessage;
    double elapsedMs = 0.0;
    size_t bytesReceived = 0;
    bool fromCache = false;  // Sprint 2.2: Served from disk cache
    
    bool IsTimeout() const {
        return curlResult == 28 ||  // CURLE_OPERATION_TIMEDOUT
               errorMessage.find("timeout") != std::string::npos ||
               errorMessage.find("timed out") != std::string::npos;
    }
};

// Client for fetching NodeData from Google Earth RockTree API
class GoogleEarthNodeDataClient {
public:
    // Constructor with config and optional transport (nullptr = create default)
    explicit GoogleEarthNodeDataClient(const Config& config,
                                       std::unique_ptr<IHttpTransport> transport = nullptr);
    ~GoogleEarthNodeDataClient();
    
    // Fetch NodeData for a given node key (quadkey string)
    // This is a synchronous blocking call - caller should run in worker thread
    // epochOverride: if non-empty, use this epoch instead of the default
    NodeDataResult FetchNodeData(const std::string& nodeKey,
                                 const std::string& epochOverride = "");

    // Set epoch dynamically (from PlanetoidMetadata via octree index)
    void SetEpoch(const std::string& epoch) { epoch_ = epoch; }

    // Get current epoch
    const std::string& GetEpoch() const { return epoch_; }

    // Check if client is properly configured
    bool IsEnabled() const { return !endpointTemplate_.empty(); }
    
private:
    std::string endpointTemplate_;
    std::string epoch_;  // Dynamic epoch (from PlanetoidMetadata)
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string authToken_;
    std::unique_ptr<IHttpTransport> transport_;
    bool ownsTransport_ = false;
    
    // Build request headers (Accept, Authorization, custom)
    std::vector<std::pair<std::string, std::string>> BuildHeaders() const;
};

} // namespace globe
