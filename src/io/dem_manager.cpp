#include "dem_manager.h"
#include "terrain_rgb_decoder.h"
#include "../debug/network_panel.h"
#include <curl/curl.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void ReplaceAllInPlace(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
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

const char* DemProviderTypeToString(DemProviderType t) {
    switch (t) {
        case DemProviderType::TerrainRGB:   return "terrain-rgb";
        case DemProviderType::GoogleEarth:  return "google-earth";
    }
    return "unknown";
}

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<std::string*>(userp);
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DemManager::DemManager(const Config& config) : config_(config) {
    providerType_ = config_.providerType;
    
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

void DemManager::TouchLru(const TileKey& key) const {
    // Move key to front of LRU list (most recently used). Caller must hold cacheMutex_.
    auto it = lruIterMap_.find(key);
    if (it != lruIterMap_.end()) {
        lruOrder_.splice(lruOrder_.begin(), lruOrder_, it->second);
    }
}

void DemManager::Request(const TileKey& key, int priority, double score) {
    // Terminal error gate - silently drop requests when provider is not functional
    // This prevents log spam and queue churn for unimplemented providers
    if (terminalError_.load()) {
        return;
    }
    
    // Auth backoff check - skip all DEM requests during backoff period
    if (authBackoff_.load()) {
        auto now = std::chrono::steady_clock::now();
        bool stillBackingOff = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (now < backoffUntil_) {
                stillBackingOff = true;
            } else {
                authBackoff_.store(false);  // Backoff expired
                consecutiveAuthFails_.store(0);
            }
        }
        if (stillBackingOff) {
            return;
        }
    }
    
    TileKey requestKey = key;
    if (config_.maxZoom >= 0 && requestKey.level > config_.maxZoom) {
        const int shift = requestKey.level - config_.maxZoom;
        requestKey.level = config_.maxZoom;
        requestKey.x >>= shift;
        requestKey.y >>= shift;
    }

    // Check if already cached or in fail TTL
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        coEvictedKeys_.erase(requestKey);
        auto it = cache_.find(requestKey);
        if (it != cache_.end()) {
            if (it->second.valid) {
                return;  // Already have valid data
            }
            // Invalid cached entry must not permanently block retries.
            auto lruIt = lruIterMap_.find(requestKey);
            if (lruIt != lruIterMap_.end()) { lruOrder_.erase(lruIt->second); lruIterMap_.erase(lruIt); }
            cache_.erase(it);
        }
        // Check fail TTL - retry if expired
        auto failIt = failedUntil_.find(requestKey);
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
        if (inFlightKeys_.count(requestKey) > 0) {
            return;  // Worker already fetching this tile.
        }
        auto it = pendingRanks_.find(requestKey);
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
        requestQueue_.push(DemRequest{requestKey, rank.priority, rank.score, rank.seq});
        if (it == pendingRanks_.end()) {
            pendingCount_++;
        }
        pendingRanks_[requestKey] = rank;
    }
    queueCv_.notify_one();
}

bool DemManager::HasPendingRequest(const TileKey& key) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return pendingRanks_.count(key) > 0 || inFlightKeys_.count(key) > 0;
}

bool DemManager::HasData(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (coEvictedKeys_.count(key) > 0) {
        return false;
    }
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid) {
        TouchLru(key);
        return true;
    }
    return false;
}

bool DemManager::HasDataOrAncestor(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    TileKey probe = key;
    while (probe.level >= 0) {
        if (coEvictedKeys_.count(probe) > 0) {
            if (probe.level == 0) {
                break;
            }
            probe = probe.Parent();
            continue;
        }
        auto it = cache_.find(probe);
        if (it != cache_.end() && it->second.valid) {
            return true;
        }
        if (probe.level == 0) {
            break;
        }
        probe = probe.Parent();
    }
    return false;
}

bool DemManager::GetBestAvailableLevel(const TileKey& key, int& outLevel) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    TileKey probe = key;
    while (probe.level >= 0) {
        if (coEvictedKeys_.count(probe) > 0) {
            if (probe.level == 0) {
                break;
            }
            probe = probe.Parent();
            continue;
        }
        auto it = cache_.find(probe);
        if (it != cache_.end() && it->second.valid) {
            outLevel = probe.level;
            return true;
        }
        if (probe.level == 0) {
            break;
        }
        probe = probe.Parent();
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
    DemSampleResult detailed;
    if (!SampleHeightDetailed(lonDeg, latDeg, level, detailed)) {
        return false;
    }
    heightMeters = detailed.heightMeters;
    return true;
}

bool DemManager::SampleHeightDetailed(double lonDeg, double latDeg, int level, DemSampleResult& out) const {
    out = DemSampleResult{};
    level = std::clamp(level, 0, 22);

    // Find the tile that contains this lat/lon at the given level
    int n = 1 << level;
    double latClamped = std::clamp(latDeg, -85.05112878, 85.05112878);
    double latRad = latClamped * M_PI / 180.0;

    int tileX = static_cast<int>((lonDeg + 180.0) / 360.0 * n);
    int tileY = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);
    
    // Clamp
    tileX = std::clamp(tileX, 0, n - 1);
    tileY = std::clamp(tileY, 0, n - 1);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);

    // Parent fallback chain:
    // First try exact tile at requested level, then walk to ancestors.
    // This reduces terrain pop when child DEM is pending but parent DEM is already cached.
    int sampleX = tileX;
    int sampleY = tileY;
    for (int sampleLevel = level; sampleLevel >= 0; --sampleLevel) {
        TileKey key(sampleLevel, sampleX, sampleY);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.valid) {
            const DemGridData& data = it->second;

            TouchLru(key);

            // Compute UV inside the sampled tile (exact or ancestor).
            double lonLeft = Tile2Lon(sampleX, sampleLevel);
            double lonRight = Tile2Lon(sampleX + 1, sampleLevel);
            double latTop = Tile2Lat(sampleY, sampleLevel);
            double latBottom = Tile2Lat(sampleY + 1, sampleLevel);

            double u = (lonDeg - lonLeft) / (lonRight - lonLeft);
            // Service row order is south->north (bottom->top). So v=0 must map to latBottom.
            // (If we use the usual north->south mapping, N/S tile seams become kilometer-scale cliffs.)
            double v = (latClamped - latBottom) / (latTop - latBottom);
            u = std::clamp(u, 0.0, 1.0);
            v = std::clamp(v, 0.0, 1.0);

            out.ok = true;
            out.heightMeters = SampleBilinear(data, u, v);
            out.sourceLevel = sampleLevel;
            out.usedAncestor = sampleLevel != level;
            return true;
        }

        if (sampleLevel == 0) {
            break;
        }
        sampleX >>= 1;
        sampleY >>= 1;
    }

    return false;
}

bool DemManager::GetGridData(const TileKey& key, DemGridData& outData) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (coEvictedKeys_.count(key) > 0) {
        return false;
    }
    auto it = cache_.find(key);
    if (it == cache_.end() || !it->second.valid) {
        return false;
    }
    TouchLru(key);
    outData = it->second;
    return true;
}

void DemManager::PutGridData(const TileKey& key, const DemGridData& data) {
    if (!data.valid || data.heights.empty() || data.meshN <= 1) {
        return;
    }
    DemGridData copy = data;

    std::lock_guard<std::mutex> lock(cacheMutex_);
    coEvictedKeys_.erase(key);
    cache_[key] = std::move(copy);
    // Insert or move-to-front in LRU list.
    auto lruIt = lruIterMap_.find(key);
    if (lruIt != lruIterMap_.end()) {
        lruOrder_.erase(lruIt->second);
    }
    lruOrder_.push_front(key);
    lruIterMap_[key] = lruOrder_.begin();
}

void DemManager::SetPinnedTiles(const std::vector<TileKey>& keys) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    pinnedKeys_.clear();
    pinnedKeys_.reserve(keys.size());
    for (const TileKey& key : keys) {
        pinnedKeys_.insert(key);
    }
}

void DemManager::UnpinAndEvict(const TileKey& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    pinnedKeys_.erase(key);
    coEvictedKeys_.insert(key);
    cache_.erase(key);
    failedUntil_.erase(key);
    auto lruIt = lruIterMap_.find(key);
    if (lruIt != lruIterMap_.end()) {
        lruOrder_.erase(lruIt->second);
        lruIterMap_.erase(lruIt);
    }
}

HeightSampler DemManager::GetHeightSampler() const {
    return [this](double lonDeg, double latDeg, int level, double& heightMeters) {
        return SampleHeight(lonDeg, latDeg, level, heightMeters);
    };
}

void DemManager::Update() {
    // Process completed requests - cache cleanup
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // O(1) LRU eviction: pop from back of LRU list, skip pinned entries.
    while (cache_.size() > config_.cacheSize && !lruOrder_.empty()) {
        // Walk from back (oldest) to find first unpinned victim.
        bool foundVictim = false;
        for (auto rit = lruOrder_.rbegin(); rit != lruOrder_.rend(); ++rit) {
            if (pinnedKeys_.count(*rit) > 0) {
                continue;
            }
            TileKey victim = *rit;
            cache_.erase(victim);
            lruIterMap_.erase(victim);
            lruOrder_.erase(std::next(rit).base());
            foundVictim = true;
            break;
        }
        if (!foundVictim) {
            break;  // Everything is pinned; postpone eviction.
        }
    }
}

int DemManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return static_cast<int>(cache_.size());
}

void DemManager::WorkerLoop() {
    while (running_) {
        std::vector<TileKey> batch;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !requestQueue_.empty();
            });
            
            if (!running_) break;
            
            // Collect tiles from priority queue (single tile at a time for TerrainRGB)
            while (!requestQueue_.empty() && static_cast<int>(batch.size()) < 1) {
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

                batch.push_back(req.key);
                pendingRanks_.erase(it);
                inFlightKeys_.insert(req.key);
                pendingCount_--;
            }
        }
        
        if (batch.empty()) continue;
        
        // Fetch DEM data for each tile in batch
        for (const auto& key : batch) {
            DemGridData data;
            bool success = FetchTile(key, data);
            
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                if (coEvictedKeys_.count(key) > 0) {
                    continue;
                }
                
                if (success && data.valid && !data.heights.empty() && data.meshN > 1) {
                    cache_[key] = std::move(data);
                    // Insert into LRU list (move-to-front if exists).
                    auto lruIt = lruIterMap_.find(key);
                    if (lruIt != lruIterMap_.end()) { lruOrder_.erase(lruIt->second); }
                    lruOrder_.push_front(key);
                    lruIterMap_[key] = lruOrder_.begin();
                } else {
                    auto failUntil = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(static_cast<int>(config_.failRetryDelaySec));
                    failedUntil_[key] = failUntil;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (const auto& key : batch) {
                inFlightKeys_.erase(key);
            }
        }
    }
}

bool DemManager::FetchTile(const TileKey& key, DemGridData& outData) {
    switch (providerType_) {
        case DemProviderType::TerrainRGB:
            return FetchTerrainRGB(key, outData);
        case DemProviderType::GoogleEarth:
            // TODO(Phase 4/5): Implement Google Earth provider
            // For now, fail-fast with clear error (no silent fallback)
            std::cerr << "[DEM] ERROR: Google Earth provider not yet implemented (Phase 4/5). "
                      << "Use --dem-provider terrain-rgb or check back later." << std::endl;
            healthStatus_.store(DemHealthStatus::BadResponse);
            terminalError_.store(true);  // Gate future requests to prevent log spam
            return false;
    }
    return false;
}

std::string DemManager::BuildTerrainRGBUrl(const TileKey& key, int effectiveLevel) const {
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
    return url;
}

bool DemManager::FetchTerrainRGB(const TileKey& key, DemGridData& outData) {
    outData = DemGridData{};
    const int effectiveLevel = std::min(key.level, std::max(0, config_.maxZoom));
    const std::string url = BuildTerrainRGBUrl(key, effectiveLevel);

    NetworkPanel::Instance().RecordStart(key, RequestType::DemMesh, url);
    const auto startTime = std::chrono::high_resolution_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        stats_.fetchFail++;
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false, 0, 0, 0.0, false, "curl init failed");
        return false;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
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

    const bool fetchOk = (res == CURLE_OK && responseCode == 200);
    if (fetchOk) {
        stats_.fetchSuccess++;
        double prev = stats_.totalFetchMs.load(std::memory_order_relaxed);
        while (!stats_.totalFetchMs.compare_exchange_weak(
                   prev, prev + elapsedMs, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    } else {
        stats_.fetchFail++;
        if (res == CURLE_OPERATION_TIMEDOUT) stats_.fetchTimeout++;
        if (responseCode == 401 || responseCode == 403) stats_.fetchAuth++;
    }

    if (!fetchOk) {
        if (responseCode == 401 || responseCode == 403) {
            const int fails = consecutiveAuthFails_.fetch_add(1) + 1;
            if (fails >= config_.authBackoffThreshold) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                authBackoff_.store(true);
                backoffUntil_ = std::chrono::steady_clock::now() +
                                std::chrono::seconds(static_cast<int>(config_.authBackoffSec));
                healthStatus_.store(DemHealthStatus::AuthFailed);
            }
        } else if (healthStatus_.load() != DemHealthStatus::Healthy) {
            healthStatus_.store(DemHealthStatus::Unreachable);
        }

        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, false, responseCode, response.size(), elapsedMs, false,
            curl_easy_strerror(res));
        return false;
    }

    consecutiveAuthFails_.store(0);

    std::vector<uint8_t> payload(response.begin(), response.end());
    std::string decodeError;
    const bool parsed = DecodeTerrainRGBFromImage(
        payload, std::max(2, config_.meshN), TerrainRGBEncoding::Mapbox, outData, &decodeError);
    if (parsed) {
        stats_.parseSuccess++;
        healthStatus_.store(DemHealthStatus::Healthy);
        NetworkPanel::Instance().RecordComplete(
            key, RequestType::DemMesh, true, responseCode, payload.size(), elapsedMs, false, "");
        return true;
    }

    stats_.parseFail++;
    if (healthStatus_.load() != DemHealthStatus::Healthy) {
        healthStatus_.store(DemHealthStatus::BadResponse);
    }
    NetworkPanel::Instance().RecordComplete(
        key, RequestType::DemMesh, false, responseCode, payload.size(), elapsedMs, false, decodeError);
    return false;
}

DemHealthStatus DemManager::CheckHealth() {
    std::cerr << "[DEM] Health check: " << config_.baseUrl << " (provider: " 
              << DemProviderTypeToString(providerType_) << ")" << std::endl;
    
    // Use a known tile (z=1, x=1, y=0 = eastern hemisphere, north) as probe
    TileKey probeKey(1, 1, 0);
    DemGridData probeResult;
    
    bool ok = FetchTile(probeKey, probeResult);
    
    DemHealthStatus status;
    if (ok && probeResult.valid) {
        status = DemHealthStatus::Healthy;
        // Cache probe result so it's not wasted
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto now = std::chrono::steady_clock::now();
        cache_[probeKey] = std::move(probeResult);
        // Insert into LRU.
        auto lruIt = lruIterMap_.find(probeKey);
        if (lruIt != lruIterMap_.end()) { lruOrder_.erase(lruIt->second); }
        lruOrder_.push_front(probeKey);
        lruIterMap_[probeKey] = lruOrder_.begin();
        std::cerr << "[DEM] Health: OK (" << cache_[probeKey].heights.size() << " samples, "
                  << "min=" << cache_[probeKey].minHeight << "m, max=" << cache_[probeKey].maxHeight << "m)" << std::endl;
    } else if (stats_.fetchAuth.load() > 0) {
        status = DemHealthStatus::AuthFailed;
        std::cerr << "[DEM] Health: AUTH FAILED (401/403) - provide basic auth (--dem-auth or NATIVE_GLOBE_DEM_AUTH) and check Origin/Referer" << std::endl;
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

double DemManager::SampleBilinear(const DemGridData& data, double u, double v) const {
    const int n = data.meshN;
    if (n < 2) return data.heights.empty() ? 0.0 : data.heights[0];
    
    // Clamp to valid range
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    
    // Map to grid coordinates
    const double fx = u * (n - 1);
    const double fy = v * (n - 1);
    
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, n - 1);
    const int y1 = std::min(y0 + 1, n - 1);
    
    const double dx = fx - x0;
    const double dy = fy - y0;
    
    // Get corner values
    const double h00 = data.heights[y0 * n + x0];
    const double h10 = data.heights[y0 * n + x1];
    const double h01 = data.heights[y1 * n + x0];
    const double h11 = data.heights[y1 * n + x1];
    
    // Bilinear interpolation
    return h00 * (1 - dx) * (1 - dy) +
           h10 * dx * (1 - dy) +
           h01 * (1 - dx) * dy +
           h11 * dx * dy;
}

} // namespace globe
