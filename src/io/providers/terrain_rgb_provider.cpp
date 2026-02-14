#include "terrain_rgb_provider.h"
#include "../terrain_rgb_decoder.h"
#include "../../core/config.h"
#include "../../debug/network_panel.h"
#include <curl/curl.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <chrono>

namespace globe {

namespace {

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<std::vector<uint8_t>*>(userp);
    size_t totalSize = size * nmemb;
    data->insert(data->end(), 
                 static_cast<uint8_t*>(contents), 
                 static_cast<uint8_t*>(contents) + totalSize);
    return totalSize;
}

std::string ExtractOrigin(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "";
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) return url;
    return url.substr(0, pathStart);
}

void ReplaceAllInPlace(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool HasQueryParam(const std::string& url, const std::string& key) {
    if (url.find("?" + key + "=") != std::string::npos) {
        return true;
    }
    if (url.find("&" + key + "=") != std::string::npos) {
        return true;
    }
    return false;
}

} // anonymous namespace

TerrainRGBProvider::TerrainRGBProvider(const TerrainRGBConfig& config) 
    : config_(config) {
}

TerrainRGBProvider::~TerrainRGBProvider() = default;

std::string TerrainRGBProvider::BuildUrl(const TileKey& key, int effectiveLevel) const {
    int x = key.x;
    int y = key.y;
    if (effectiveLevel < key.level) {
        const int shift = key.level - effectiveLevel;
        x >>= shift;
        y >>= shift;
    }

    std::string url = config_.baseUrl;
    ReplaceAllInPlace(url, "{z}", std::to_string(effectiveLevel));
    ReplaceAllInPlace(url, "{x}", std::to_string(x));
    ReplaceAllInPlace(url, "{y}", std::to_string(y));
    
    // Append API key/token if present and not already in URL.
    if (!config_.apiKey.empty() &&
        !HasQueryParam(url, "access_token") &&
        !HasQueryParam(url, "key")) {
        const bool isMapbox = url.find("api.mapbox.com") != std::string::npos;
        const char separator = (url.find('?') == std::string::npos) ? '?' : '&';
        url += separator;
        url += isMapbox ? "access_token=" : "key=";
        url += config_.apiKey;
    }
    
    return url;
}

bool TerrainRGBProvider::HttpFetch(const std::string& url, std::vector<uint8_t>& outData, 
                                   DemFetchResult& outResult) {
    outData.clear();
    
    const auto startTime = std::chrono::high_resolution_clock::now();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        outResult = DemFetchResult::NetworkError(0, "curl_easy_init failed", 0.0);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36");

    if (!config_.basicAuthUserPwd.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, config_.basicAuthUserPwd.c_str());
    }

    struct curl_slist* headers = nullptr;
    const std::string origin = ExtractOrigin(url);
    if (!origin.empty()) {
        headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
        headers = curl_slist_append(headers, ("Referer: " + origin + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode res = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Determine result type
    if (res != CURLE_OK) {
        // CURL error
        std::string errorMsg = curl_easy_strerror(res);
        outResult = DemFetchResult::NetworkError(static_cast<int>(res), errorMsg, elapsedMs);
        outResult.bytesReceived = outData.size();
        return false;
    }
    
    if (responseCode == 401 || responseCode == 403) {
        outResult = DemFetchResult::AuthError(responseCode, "Authentication failed");
        outResult.elapsedMs = elapsedMs;
        outResult.bytesReceived = outData.size();
        return false;
    }
    
    if (responseCode != 200) {
        outResult = DemFetchResult::HttpError(responseCode, "HTTP error " + std::to_string(responseCode));
        outResult.elapsedMs = elapsedMs;
        outResult.bytesReceived = outData.size();
        return false;
    }
    
    // Success
    outResult = DemFetchResult::Success(responseCode, outData.size(), elapsedMs);
    return true;
}

bool TerrainRGBProvider::DecodeTile(const std::vector<uint8_t>& pngData, DemGridData& outData) {
    std::string decodeError;
    // Build a minimal Config to pass no-data sanitization parameters to the decoder.
    Config sanitizeConfig;
    sanitizeConfig.demNoDataMinHeightM = config_.demNoDataMinHeightM;
    sanitizeConfig.demNoDataReplacementM = config_.demNoDataReplacementM;
    sanitizeConfig.forceClampTerrainNoData = config_.forceClampTerrainNoData;
    return DecodeTerrainRGBFromImage(
        pngData,
        std::max(2, config_.meshN),
        TerrainRGBEncoding::Mapbox,
        outData,
        &decodeError,
        &sanitizeConfig);
}

bool TerrainRGBProvider::FetchDemTile(const TileKey& key, DemGridData& outData, 
                                      DemFetchResult& outResult) {
    const int effectiveLevel = std::min(key.level, std::max(0, config_.maxZoom));
    const std::string url = BuildUrl(key, effectiveLevel);

    if (config_.debug) {
        std::cerr << "[TerrainRGB] Fetch: " << url << std::endl;
    }

    // Record start in network panel
    NetworkPanel::Instance().RecordStart(key, RequestType::DemMesh, url);

    std::vector<uint8_t> pngData;
    bool fetchOk = HttpFetch(url, pngData, outResult);
    
    if (!fetchOk) {
        // Update health status based on error type
        if (outResult.IsAuthFailure()) {
            healthStatus_.store(DemHealthStatus::AuthFailed);
        } else if (outResult.IsTimeout()) {
            healthStatus_.store(DemHealthStatus::Unreachable);
        } else if (outResult.errorType == DemFetchResult::ErrorType::Network) {
            healthStatus_.store(DemHealthStatus::Unreachable);
        } else {
            healthStatus_.store(DemHealthStatus::BadResponse);
        }
        
        // Record failure in network panel
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false, 
            outResult.httpStatusCode, outResult.bytesReceived, 
            outResult.elapsedMs, false, outResult.errorMessage);
        
        return false;
    }

    if (!DecodeTile(pngData, outData)) {
        // Preserve network fetch metadata, only update error-specific fields
        outResult.success = false;
        outResult.errorType = DemFetchResult::ErrorType::Decode;
        outResult.errorMessage = "Failed to decode PNG/terrain-rgb";
        // Note: httpStatusCode, bytesReceived, elapsedMs preserved from successful fetch
        healthStatus_.store(DemHealthStatus::BadResponse);
        
        // Record decode failure in network panel
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false, 
            outResult.httpStatusCode, outResult.bytesReceived, 
            outResult.elapsedMs, false, outResult.errorMessage);
        
        return false;
    }

    // Success
    healthStatus_.store(DemHealthStatus::Healthy);
    
    // Record success in network panel
    NetworkPanel::Instance().RecordComplete(
        key, RequestType::DemMesh, true, 
        outResult.httpStatusCode, outResult.bytesReceived, 
        outResult.elapsedMs, false, "");
    
    return true;
}

DemHealthStatus TerrainRGBProvider::CheckHealth() {
    // Use a known tile (z=1, x=1, y=0) as probe
    TileKey probeKey(1, 1, 0);
    DemGridData probeData;
    DemFetchResult result;
    
    if (FetchDemTile(probeKey, probeData, result) && probeData.valid) {
        healthStatus_.store(DemHealthStatus::Healthy);
        return DemHealthStatus::Healthy;
    }
    
    return healthStatus_.load();
}

} // namespace globe
