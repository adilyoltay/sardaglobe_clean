// Google Earth NodeData Client Implementation

#include "google_earth_nodedata_client.h"
#include "../ge_mesh_url_template.h"
#include "../../debug/network_panel.h"
#include <cstdlib>
#include <iostream>
#include <chrono>

namespace globe {

GoogleEarthNodeDataClient::GoogleEarthNodeDataClient(
    const Config& config,
    std::unique_ptr<IHttpTransport> transport)
    : endpointTemplate_(config.geMeshEndpoint),
      epoch_(config.geEpoch),
      headers_(config.geHeaders),
      transport_(std::move(transport)),
      ownsTransport_(transport_ == nullptr) {
    
    // Get auth token from environment if configured
    if (const char* token = std::getenv(config.geTokenEnv.c_str())) {
        authToken_ = token;
    }
    
    // Create default transport if not provided
    // Sprint 3: Use HTTP/2 config from main Config
    if (ownsTransport_) {
        HttpTransportConfig httpConfig;
        httpConfig.verifySsl = true;  // Default: secure
        httpConfig.enableHttp2 = config.geMeshEnableHttp2;
        httpConfig.allowHttp1Fallback = config.geMeshAllowHttp1Fallback;
        httpConfig.tcpKeepAliveSec = config.geMeshTcpKeepAliveSec;
        httpConfig.tcpKeepAliveIdleSec = config.geMeshTcpKeepAliveIdleSec;
        httpConfig.enableConnectionReuse = config.geMeshEnableConnectionReuse;
        transport_ = std::make_unique<CurlHttpTransport>(httpConfig);
    }
}

GoogleEarthNodeDataClient::~GoogleEarthNodeDataClient() = default;

std::vector<std::pair<std::string, std::string>> 
GoogleEarthNodeDataClient::BuildHeaders() const {
    std::vector<std::pair<std::string, std::string>> result;
    
    // Accept protobuf format
    result.push_back({"Accept", "application/x-protobuf"});

    // Add Google Earth spoofing headers (Required for access)
    result.push_back({"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36"});
    result.push_back({"Referer", "https://earth.google.com/"});
    result.push_back({"Origin", "https://earth.google.com"});
    
    // Check if custom Authorization header is provided
    bool hasCustomAuth = false;
    for (const auto& [key, value] : headers_) {
        if (key == "Authorization") {
            hasCustomAuth = true;
            break;
        }
    }
    
    // Add custom headers from config (allowlist already validated)
    for (const auto& [key, value] : headers_) {
        result.push_back({key, value});
    }
    
    // Add Bearer token only if:
    // 1. Token is available, AND
    // 2. No custom Authorization header was provided
    if (!authToken_.empty() && !hasCustomAuth) {
        result.push_back({"Authorization", "Bearer " + authToken_});
    }
    
    return result;
}

NodeDataResult GoogleEarthNodeDataClient::FetchNodeData(const std::string& nodeKey) {
    NodeDataResult result;
    
    if (endpointTemplate_.empty()) {
        result.errorMessage = "NodeData endpoint not configured";
        return result;
    }
    
    // Resolve {epoch} placeholder in template before building URL
    std::string tmpl = endpointTemplate_;
    if (!epoch_.empty()) {
        size_t epochPos = tmpl.find("{epoch}");
        if (epochPos != std::string::npos) {
            tmpl.replace(epochPos, 7, epoch_);
        }
    }

    // Build URL from template
    std::string url = BuildGeMeshUrl(tmpl, nodeKey);
    
    // Build headers
    auto headers = BuildHeaders();
    
    // Record start in NetworkPanel (using nodeKey as string id)
    NetworkPanel::Instance().RecordStart(nodeKey, RequestType::TerrainMesh, url);
    
    // Perform GET request
    auto startTime = std::chrono::steady_clock::now();
    HttpResponse response = transport_->Get(url, headers);
    auto endTime = std::chrono::steady_clock::now();
    
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    // Populate result
    result.success = response.success;
    result.httpCode = response.httpCode;
    result.curlResult = response.curlResult;
    result.data = std::move(response.body);
    result.errorMessage = response.errorMessage;
    result.elapsedMs = elapsedMs;
    result.bytesReceived = result.data.size();
    
    // Record completion in NetworkPanel
    // Phase 6: Pass fromCache flag for observability
    NetworkPanel::Instance().RecordComplete(
        nodeKey, 
        RequestType::TerrainMesh,
        result.success,
        result.httpCode,
        result.bytesReceived,
        elapsedMs,
        result.fromCache,  // cacheHit from disk cache
        result.errorMessage
    );
    
    return result;
}

} // namespace globe
