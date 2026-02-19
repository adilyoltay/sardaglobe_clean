// Google Earth NodeData Client Implementation

#include "google_earth_nodedata_client.h"
#include "ge_headers.h"
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
    auto result = ge_headers::BuildStandardHeaders();
    ge_headers::AppendCustomHeaders(result, headers_);
    ge_headers::AppendBearerTokenIfNeeded(result, authToken_);
    return result;
}

NodeDataResult GoogleEarthNodeDataClient::FetchNodeData(const std::string& nodeKey,
                                                         const std::string& epochOverride) {
    NodeDataResult result;
    
    if (endpointTemplate_.empty()) {
        result.errorMessage = "NodeData endpoint not configured";
        return result;
    }
    
    // Resolve {epoch} placeholder in template before building URL
    // Use epochOverride (per-node epoch from BulkMetadata) if provided
    const std::string& effectiveEpoch = epochOverride.empty() ? epoch_ : epochOverride;
    std::string tmpl = endpointTemplate_;
    if (!effectiveEpoch.empty()) {
        size_t epochPos = tmpl.find("{epoch}");
        if (epochPos != std::string::npos) {
            tmpl.replace(epochPos, 7, effectiveEpoch);
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
