#include "terrain_rgb_provider.h"
#include "../terrain_rgb_decoder.h"
#include <curl/curl.h>
#include <algorithm>
#include <iostream>
#include <sstream>

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
    
    // Append API key if present
    if (!config_.apiKey.empty()) {
        url += (url.find('?') == std::string::npos ? "?" : "&");
        url += "key=" + config_.apiKey;
    }
    
    return url;
}

bool TerrainRGBProvider::HttpFetch(const std::string& url, std::vector<uint8_t>& outData) {
    outData.clear();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
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

    if (res != CURLE_OK || responseCode != 200) {
        return false;
    }
    
    return true;
}

bool TerrainRGBProvider::DecodeTile(const std::vector<uint8_t>& pngData, DemGridData& outData) {
    std::string decodeError;
    return DecodeTerrainRGBFromImage(
        pngData, 
        std::max(2, config_.meshN), 
        TerrainRGBEncoding::Mapbox, 
        outData, 
        &decodeError);
}

bool TerrainRGBProvider::FetchDemTile(const TileKey& key, DemGridData& outData) {
    const int effectiveLevel = std::min(key.level, std::max(0, config_.maxZoom));
    const std::string url = BuildUrl(key, effectiveLevel);

    if (config_.debug) {
        std::cerr << "[TerrainRGB] Fetch: " << url << std::endl;
    }

    std::vector<uint8_t> pngData;
    if (!HttpFetch(url, pngData)) {
        healthStatus_.store(DemHealthStatus::Unreachable);
        return false;
    }

    if (!DecodeTile(pngData, outData)) {
        healthStatus_.store(DemHealthStatus::BadResponse);
        return false;
    }

    healthStatus_.store(DemHealthStatus::Healthy);
    return true;
}

DemHealthStatus TerrainRGBProvider::CheckHealth() {
    // Use a known tile (z=1, x=1, y=0) as probe
    TileKey probeKey(1, 1, 0);
    DemGridData probeData;
    
    if (FetchDemTile(probeKey, probeData) && probeData.valid) {
        healthStatus_.store(DemHealthStatus::Healthy);
        return DemHealthStatus::Healthy;
    }
    
    return healthStatus_.load();
}

} // namespace globe
