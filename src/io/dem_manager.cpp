#include "dem_manager.h"
#include "../debug/network_panel.h"
#include <curl/curl.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace globe {

namespace {

std::string ExtractOrigin(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "";
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) return url;
    return url.substr(0, pathStart);
}

} // namespace

const char* DemHealthStatusToString(DemHealthStatus s) {
    switch (s) {
        case DemHealthStatus::Unknown:     return "Unknown";
        case DemHealthStatus::Healthy:     return "Healthy";
        case DemHealthStatus::AuthFailed:  return "AuthFailed";
        case DemHealthStatus::Unreachable: return "Unreachable";
        case DemHealthStatus::BadResponse: return "BadResponse";
        case DemHealthStatus::Disabled:    return "Disabled";
    }
    return "Unknown";
}

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<std::string*>(userp);
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DemManager::DemManager(const Config& config) : config_(config) {
    // Start worker threads
    int numWorkers = 4;
    workers_.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

DemManager::~DemManager() {
    Shutdown();
}

void DemManager::Shutdown() {
    running_ = false;
    queueCv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void DemManager::Request(const TileKey& key, int priority, double score) {
    // Auth backoff check - skip all DEM requests during backoff period
    if (authBackoff_.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now < backoffUntil_) {
            return;  // Still in backoff period
        }
        authBackoff_.store(false);  // Backoff expired
        consecutiveAuthFails_.store(0);
    }
    
    // Check if already cached or in fail TTL
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(key) != cache_.end()) {
            return;  // Already have data
        }
        // Check fail TTL - retry if expired
        auto failIt = failedUntil_.find(key);
        if (failIt != failedUntil_.end()) {
            auto now = std::chrono::steady_clock::now();
            if (now < failIt->second) {
                return;  // Still in fail TTL, don't retry yet
            }
            // TTL expired, remove from fail cache and allow retry
            failedUntil_.erase(failIt);
        }
    }
    
    priority = std::clamp(priority, 0, 2);

    // Pending/in-flight dedupe with priority/score upgrade
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto it = pendingRanks_.find(key);
        if (it != pendingRanks_.end()) {
            bool better = (priority > it->second.priority) ||
                          (priority == it->second.priority && score > it->second.score);
            if (!better) {
                return;  // Already pending with equal/better rank
            }
        }
        PendingRank rank;
        rank.priority = priority;
        rank.score = score;
        rank.seq = ++requestSeq_;
        requestQueue_.push(DemRequest{key, rank.priority, rank.score, rank.seq});
        if (it == pendingRanks_.end()) {
            pendingCount_++;
        }
        pendingRanks_[key] = rank;
    }
    queueCv_.notify_one();
}

bool DemManager::HasData(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid) {
        // Update access time for LRU eviction
        auto now = std::chrono::steady_clock::now();
        it->second.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
        return true;
    }
    return false;
}

double DemManager::Tile2Lon(int x, int z) {
    return x / static_cast<double>(1 << z) * 360.0 - 180.0;
}

double DemManager::Tile2Lat(int y, int z) {
    double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << z);
    return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

bool DemManager::SampleHeight(double lonDeg, double latDeg, int level, double& heightMeters) const {
    // Find the tile that contains this lat/lon at the given level
    int n = 1 << level;
    double lonRad = lonDeg * M_PI / 180.0;
    double latRad = latDeg * M_PI / 180.0;
    
    int tileX = static_cast<int>((lonDeg + 180.0) / 360.0 * n);
    int tileY = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);
    
    // Clamp
    tileX = std::clamp(tileX, 0, n - 1);
    tileY = std::clamp(tileY, 0, n - 1);
    
    TileKey key(level, tileX, tileY);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it == cache_.end() || !it->second.valid) {
        return false;
    }
    
    const DemGridData& data = it->second;
    
    // Update access time for LRU eviction
    auto now = std::chrono::steady_clock::now();
    data.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    
    // Calculate UV within tile
    double lonLeft = Tile2Lon(tileX, level);
    double lonRight = Tile2Lon(tileX + 1, level);
    double latTop = Tile2Lat(tileY, level);
    double latBottom = Tile2Lat(tileY + 1, level);
    
    double u = (lonDeg - lonLeft) / (lonRight - lonLeft);
    double v = (latDeg - latTop) / (latBottom - latTop);
    
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    
    heightMeters = SampleBilinear(data, u, v);
    return true;
}

bool DemManager::GetGridData(const TileKey& key, DemGridData& outData) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it == cache_.end() || !it->second.valid) {
        return false;
    }
    outData = it->second;
    return true;
}

HeightSampler DemManager::GetHeightSampler() const {
    return [this](double lonDeg, double latDeg, int level, double& heightMeters) {
        return SampleHeight(lonDeg, latDeg, level, heightMeters);
    };
}

void DemManager::Update() {
    // Process completed requests - cache cleanup
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // Evict least recently used entries if cache is too large
    while (cache_.size() > config_.cacheSize) {
        // True LRU: remove entry with oldest lastAccessTime
        double oldestTime = std::numeric_limits<double>::max();
        TileKey oldestKey;
        for (const auto& [key, data] : cache_) {
            if (data.lastAccessTime < oldestTime) {
                oldestTime = data.lastAccessTime;
                oldestKey = key;
            }
        }
        cache_.erase(oldestKey);
    }
}

int DemManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return static_cast<int>(cache_.size());
}

void DemManager::WorkerLoop() {
    while (running_) {
        TileKey key;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !requestQueue_.empty();
            });
            
            if (!running_) break;
            if (requestQueue_.empty()) continue;

            bool found = false;
            while (!requestQueue_.empty()) {
                DemRequest req = requestQueue_.top();
                requestQueue_.pop();

                auto it = pendingRanks_.find(req.key);
                if (it == pendingRanks_.end()) {
                    continue;  // Already processed
                }

                // Skip stale queued entries after rank upgrade.
                if (it->second.seq != req.seq) {
                    continue;
                }

                key = req.key;
                pendingRanks_.erase(it);
                pendingCount_--;
                found = true;
                break;
            }

            if (!found) {
                continue;
            }
        }
        
        // Fetch DEM data
        DemGridData data;
        if (FetchDem(key, data)) {
            // Set initial access time for LRU eviction
            auto now = std::chrono::steady_clock::now();
            data.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
            
            std::lock_guard<std::mutex> lock(cacheMutex_);
            cache_[key] = std::move(data);
        }
        
    }
}

std::string DemManager::BuildDemUrl(const DemCell& cell) const {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(6);
    
    oss << config_.baseUrl
        << "?MESHN=" << config_.meshN
        << "&CN=1"
        << "&FLOAT=1"
        << "&C1z=" << cell.level
        << "&C1x=" << cell.tileX
        << "&C1y=" << cell.tileY
        << "&C1LLX=" << cell.llx
        << "&C1LLY=" << cell.lly
        << "&C1URX=" << cell.urx
        << "&C1URY=" << cell.ury;
    
    return oss.str();
}

DemCell DemManager::BuildDemCell(const TileKey& key) const {
    DemCell cell;
    cell.level = key.level;
    cell.tileX = key.x;
    cell.tileY = key.y;

    // WGS84 geographic coordinates only (service doesn't support WebMercator)
    cell.llx = Tile2Lon(key.x, key.level);
    cell.urx = Tile2Lon(key.x + 1, key.level);
    cell.ury = Tile2Lat(key.y, key.level);      // Top (north)
    cell.lly = Tile2Lat(key.y + 1, key.level);  // Bottom (south)

    return cell;
}

bool DemManager::FetchDem(const TileKey& key, DemGridData& outData) {
    auto performRequest = [&](const std::string& url, std::string& response, long& responseCode) -> bool {
        // Network panel: record start
        NetworkPanel::Instance().RecordStart(key, RequestType::DemMesh, url);
        auto startTime = std::chrono::high_resolution_clock::now();
        
        if (config_.debug) {
            std::cerr << "[DEM] Fetching: " << url << std::endl;
        }
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            NetworkPanel::Instance().RecordComplete(key, RequestType::DemMesh, false, 0, 0, 0.0, false, "curl init failed");
            return false;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeoutSec);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSec);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36");
        
        struct curl_slist* headers = nullptr;
        std::string origin = ExtractOrigin(url);
        if (!origin.empty()) {
            headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
            headers = curl_slist_append(headers, ("Referer: " + origin + "/").c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(curl);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        bool ok = (res == CURLE_OK && responseCode == 200);
        
        // Telemetry
        if (ok) {
            stats_.fetchSuccess++;
            stats_.totalFetchMs.store(stats_.totalFetchMs.load() + elapsedMs);
        } else {
            stats_.fetchFail++;
            if (res == CURLE_OPERATION_TIMEDOUT) stats_.fetchTimeout++;
            if (responseCode == 401 || responseCode == 403) stats_.fetchAuth++;
        }
        
        NetworkPanel::Instance().RecordComplete(key, RequestType::DemMesh, ok, responseCode, 
                                                 response.size(), elapsedMs, false, ok ? "" : curl_easy_strerror(res));
        return ok;
    };

    DemCell cell = BuildDemCell(key);
    std::string url = BuildDemUrl(cell);

    std::string response;
    long responseCode = 0;
    bool ok = performRequest(url, response, responseCode);

    if (!ok) {
        if (config_.debug) {
            std::cerr << "[DEM] Fetch failed (code: " << responseCode << ")" << std::endl;
        }
        
        // 401/403 auth failure - trigger backoff
        if (responseCode == 401 || responseCode == 403) {
            int fails = consecutiveAuthFails_.fetch_add(1) + 1;
            if (fails >= config_.authBackoffThreshold) {
                authBackoff_.store(true);
                backoffUntil_ = std::chrono::steady_clock::now() + 
                                std::chrono::seconds(static_cast<int>(config_.authBackoffSec));
                healthStatus_.store(DemHealthStatus::AuthFailed);
                std::cerr << "[DEM] Auth failed " << fails << " times, backoff " 
                          << config_.authBackoffSec << "s" << std::endl;
            }
        } else {
            // Non-auth failure: add to TTL cache for retry
            std::lock_guard<std::mutex> lock(cacheMutex_);
            failedUntil_[key] = std::chrono::steady_clock::now() + 
                                std::chrono::seconds(static_cast<int>(config_.failRetryDelaySec));
        }
        return false;
    }
    
    // Success - reset auth fail counter
    consecutiveAuthFails_.store(0);
    
    bool parsed = ParseDemGrid(response, outData);
    if (parsed) {
        stats_.parseSuccess++;
    } else {
        stats_.parseFail++;
    }
    return parsed;
}

DemHealthStatus DemManager::CheckHealth() {
    std::cerr << "[DEM] Health check: " << config_.baseUrl << std::endl;
    
    // Use a known tile (z=1, x=1, y=0 = eastern hemisphere, north) as probe
    TileKey probeKey(1, 1, 0);
    DemGridData probeData;
    
    bool ok = FetchDem(probeKey, probeData);
    
    DemHealthStatus status;
    if (ok && probeData.valid) {
        status = DemHealthStatus::Healthy;
        // Cache probe result so it's not wasted
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto now = std::chrono::steady_clock::now();
        probeData.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
        cache_[probeKey] = std::move(probeData);
        std::cerr << "[DEM] Health: OK (" << cache_[probeKey].heights.size() << " samples, "
                  << "min=" << cache_[probeKey].minHeight << "m, max=" << cache_[probeKey].maxHeight << "m)" << std::endl;
    } else if (stats_.fetchAuth.load() > 0) {
        status = DemHealthStatus::AuthFailed;
        std::cerr << "[DEM] Health: AUTH FAILED (401/403) - check Origin/Referer headers" << std::endl;
    } else if (stats_.fetchFail.load() > 0) {
        status = DemHealthStatus::Unreachable;
        std::cerr << "[DEM] Health: UNREACHABLE - network/DNS/timeout error" << std::endl;
    } else {
        status = DemHealthStatus::BadResponse;
        std::cerr << "[DEM] Health: BAD RESPONSE - 200 but data unparseable" << std::endl;
    }
    
    healthStatus_.store(status);
    return status;
}

bool DemManager::ParseDemGrid(const std::string& payload, DemGridData& outData) const {
    outData.heights.clear();
    outData.meshN = config_.meshN;
    outData.minHeight = std::numeric_limits<double>::max();
    outData.maxHeight = std::numeric_limits<double>::lowest();
    
    // Find the 2D array in the response: [[...], [...], ...]
    const char* arrayStart = std::strstr(payload.c_str(), "[[");
    if (!arrayStart) {
        if (config_.debug) {
            std::cerr << "[DEM] Parse error: No 2D array found" << std::endl;
        }
        return false;
    }
    
    // Parse numbers from the JSON array
    const char* p = arrayStart;
    while (*p) {
        // Skip non-numeric characters
        while (*p && !std::isdigit(*p) && *p != '-' && *p != '.') {
            if (*p == ']' && *(p+1) == ']') break;  // End of array
            p++;
        }
        
        if (*p == ']' && *(p+1) == ']') break;
        if (!*p) break;
        
        // Parse number
        char* end;
        double val = std::strtod(p, &end);
        if (end > p) {
            outData.heights.push_back(val);
            outData.minHeight = std::min(outData.minHeight, val);
            outData.maxHeight = std::max(outData.maxHeight, val);
            p = end;
        } else {
            p++;
        }
    }
    
    size_t expected = static_cast<size_t>(config_.meshN * config_.meshN);
    outData.valid = outData.heights.size() >= expected;
    // fetchTime will be set by WorkerLoop when caching
    
    if (config_.debug) {
        std::cerr << "[DEM] Parsed " << outData.heights.size() << " values"
                  << " (expected " << expected << ")"
                  << " min=" << outData.minHeight << " max=" << outData.maxHeight << std::endl;
    }
    
    return outData.valid;
}

double DemManager::SampleBilinear(const DemGridData& data, double u, double v) const {
    if (!data.valid || data.heights.empty()) return 0.0;
    
    int meshN = data.meshN;
    
    // Convert UV to grid coordinates
    double gx = u * (meshN - 1);
    double gy = v * (meshN - 1);
    
    int x0 = static_cast<int>(std::floor(gx));
    int y0 = static_cast<int>(std::floor(gy));
    int x1 = std::min(x0 + 1, meshN - 1);
    int y1 = std::min(y0 + 1, meshN - 1);
    
    x0 = std::clamp(x0, 0, meshN - 1);
    y0 = std::clamp(y0, 0, meshN - 1);
    
    double fx = gx - x0;
    double fy = gy - y0;
    
    // Sample 4 corners
    double h00 = data.heights[y0 * meshN + x0];
    double h10 = data.heights[y0 * meshN + x1];
    double h01 = data.heights[y1 * meshN + x0];
    double h11 = data.heights[y1 * meshN + x1];
    
    // Bilinear interpolation
    double h0 = h00 + fx * (h10 - h00);
    double h1 = h01 + fx * (h11 - h01);
    
    return h0 + fy * (h1 - h0);
}

} // namespace globe
