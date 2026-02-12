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

bool StartsWith(const std::string& s, const char* prefix) {
    if (!prefix) return false;
    size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
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

// CURL write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<std::string*>(userp);
    data->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DemManager::DemManager(const Config& config) : config_(config) {
    sourceType_ = ResolveSourceType(config_);
    if (sourceType_ != DemSourceType::PirireisBatch) {
        config_.maxBatchSize = 1;
    }

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
    if (sourceType_ != DemSourceType::PirireisBatch &&
        config_.maxZoom >= 0 &&
        requestKey.level > config_.maxZoom) {
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
            
            // Collect up to maxBatchSize tiles from priority queue
            int maxBatch = std::max(1, config_.maxBatchSize);
            while (!requestQueue_.empty() && static_cast<int>(batch.size()) < maxBatch) {
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
        
        // Fetch batch DEM data
        std::vector<DemGridData> results;
        if (FetchBatch(batch, results)) {
            auto now = std::chrono::steady_clock::now();
            double accessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
            
            std::lock_guard<std::mutex> lock(cacheMutex_);
            for (size_t i = 0; i < batch.size() && i < results.size(); ++i) {
                if (coEvictedKeys_.count(batch[i]) > 0) {
                    continue;
                }
                // Only cache valid grids; invalid entries must not permanently block retries.
                if (results[i].valid && !results[i].heights.empty() && results[i].meshN > 1) {
                    cache_[batch[i]] = std::move(results[i]);
                    // Insert into LRU list (move-to-front if exists).
                    auto lruIt = lruIterMap_.find(batch[i]);
                    if (lruIt != lruIterMap_.end()) { lruOrder_.erase(lruIt->second); }
                    lruOrder_.push_front(batch[i]);
                    lruIterMap_[batch[i]] = lruOrder_.begin();
                } else {
                    auto failUntil = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(static_cast<int>(config_.failRetryDelaySec));
                    failedUntil_[batch[i]] = failUntil;
                }
            }
        } else {
            // Batch failed — add all tiles to fail TTL
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto failUntil = std::chrono::steady_clock::now() + 
                             std::chrono::seconds(static_cast<int>(config_.failRetryDelaySec));
            for (const auto& key : batch) {
                failedUntil_[key] = failUntil;
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

DemSourceType DemManager::ResolveSourceType(const Config& config) {
    if (config.sourceType != DemSourceType::Auto) {
        return config.sourceType;
    }

    const std::string lowerUrl = ToLowerAscii(config.baseUrl);
    const bool hasTileTemplate = lowerUrl.find("{z}") != std::string::npos &&
                                 lowerUrl.find("{x}") != std::string::npos &&
                                 lowerUrl.find("{y}") != std::string::npos;
    if (!hasTileTemplate) {
        return DemSourceType::PirireisBatch;
    }

    if (lowerUrl.find("terrarium") != std::string::npos) {
        return DemSourceType::TerrainRGBTerrarium;
    }

    return DemSourceType::TerrainRGBMapbox;
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

std::string DemManager::BuildBatchUrl(const std::vector<DemCell>& cells) const {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(12);
    
    // WGS84 bbox format matching webglobe.js GenerateURL (WGS84 mode)
    // Format: ?FLOAT=1&MESHN=5&CN=N&C1LLX=lon&C1LLY=lat&C1URX=lon&C1URY=lat&C2...
    oss << config_.baseUrl
        << "?FLOAT=1"
        << "&MESHN=" << config_.meshN
        << "&CN=" << cells.size();
    
    // IMPORTANT: Service returns results in numeric cell order (C1..CN).
    // So we must emit C1 for cells[0], C2 for cells[1], etc.
    for (size_t i = 0; i < cells.size(); ++i) {
        int idx = static_cast<int>(i + 1);  // 1, 2, ..., CN
        const DemCell& c = cells[i];
        oss << "&C" << idx << "LLX=" << c.llx
            << "&C" << idx << "LLY=" << c.lly
            << "&C" << idx << "URX=" << c.urx
            << "&C" << idx << "URY=" << c.ury;
    }
    
    return oss.str();
}

DemCell DemManager::BuildDemCell(const TileKey& key) const {
    DemCell cell;
    cell.level = key.level;
    cell.tileX = key.x;
    cell.tileY = key.y;

    // WGS84 geographic coordinates (matching webglobe.js MercatorToLonLat)
    cell.llx = Tile2Lon(key.x, key.level);
    cell.urx = Tile2Lon(key.x + 1, key.level);
    cell.ury = Tile2Lat(key.y, key.level);      // Top (north)
    cell.lly = Tile2Lat(key.y + 1, key.level);  // Bottom (south)

    return cell;
}

bool DemManager::FetchTerrainRGBBatch(const std::vector<TileKey>& keys, std::vector<DemGridData>& outDataVec) {
    outDataVec.clear();
    outDataVec.resize(keys.size());

    for (size_t i = 0; i < keys.size(); ++i) {
        DemGridData data;
        if (!FetchSingleTerrainRGB(keys[i], data)) {
            data.valid = false;
            data.meshN = std::max(2, config_.meshN);
        }
        outDataVec[i] = std::move(data);
    }

    // Keep batch-level contract "successful" and let per-tile valid flags drive retry TTL.
    return true;
}

bool DemManager::FetchSingleTerrainRGB(const TileKey& key, DemGridData& outData) {
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

    const TerrainRGBEncoding encoding =
        (sourceType_ == DemSourceType::TerrainRGBTerrarium)
            ? TerrainRGBEncoding::Terrarium
            : TerrainRGBEncoding::Mapbox;

    std::vector<uint8_t> payload(response.begin(), response.end());
    std::string decodeError;
    const bool parsed = DecodeTerrainRGBFromImage(
        payload, std::max(2, config_.meshN), encoding, outData, &decodeError);
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

bool DemManager::FetchBatch(const std::vector<TileKey>& keys, std::vector<DemGridData>& outDataVec) {
    if (keys.empty()) {
        outDataVec.clear();
        return true;
    }

    if (sourceType_ != DemSourceType::PirireisBatch) {
        return FetchTerrainRGBBatch(keys, outDataVec);
    }

    // Build cells and URL
    std::vector<DemCell> cells;
    cells.reserve(keys.size());
    for (const auto& key : keys) {
        cells.push_back(BuildDemCell(key));
    }
    std::string url = BuildBatchUrl(cells);
    
    // Network panel: record start (use first tile key for tracking)
    NetworkPanel::Instance().RecordStart(keys[0], RequestType::DemMesh, url);
    auto startTime = std::chrono::high_resolution_clock::now();
    
    if (config_.debug) {
        std::cerr << "[DEM] Batch fetch (" << keys.size() << " tiles): " << url << std::endl;
    }

    // Synthetic/offline DEM source: "synthetic://"
    // Generates a continuous heightfield based on lon/lat (meters) to exercise terrain pipeline.
    if (StartsWith(config_.baseUrl, "synthetic://")) {
        outDataVec.clear();
        outDataVec.reserve(keys.size());

        constexpr double kPi = 3.14159265358979323846;
        constexpr double kMaxMercatorLatDeg = 85.05112878;
        const int meshN = std::max(2, config_.meshN);

        for (const TileKey& key : keys) {
            DemCell cell = BuildDemCell(key);
            DemGridData data;
            data.meshN = meshN;
            data.minHeight = std::numeric_limits<double>::max();
            data.maxHeight = std::numeric_limits<double>::lowest();
            data.heights.reserve(static_cast<size_t>(meshN * meshN));

            // Row order must match service semantics (south->north).
            for (int r = 0; r < meshN; ++r) {
                double v = (meshN == 1) ? 0.0 : static_cast<double>(r) / static_cast<double>(meshN - 1);
                double latDeg = cell.lly + (cell.ury - cell.lly) * v;
                latDeg = std::clamp(latDeg, -kMaxMercatorLatDeg, kMaxMercatorLatDeg);
                for (int c = 0; c < meshN; ++c) {
                    double u = (meshN == 1) ? 0.0 : static_cast<double>(c) / static_cast<double>(meshN - 1);
                    double lonDeg = cell.llx + (cell.urx - cell.llx) * u;

                    // Smooth, low-frequency, longitude-periodic heightfield (meters).
                    // This is intentionally "well-behaved" so seams/cliffs indicate
                    // mapping/LOD bugs rather than synthetic data pathology.
                    double lonRad = lonDeg * kPi / 180.0;
                    double latRad = latDeg * kPi / 180.0;
                    double h =
                        60.0 * std::sin(lonRad) * std::cos(latRad) +
                        30.0 * std::sin(latRad * 2.0);

                    data.heights.push_back(h);
                    data.minHeight = std::min(data.minHeight, h);
                    data.maxHeight = std::max(data.maxHeight, h);
                }
            }

            data.valid = (data.heights.size() == static_cast<size_t>(meshN * meshN));
            outDataVec.push_back(std::move(data));
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        stats_.fetchSuccess++;
        stats_.parseSuccess++;
        { // CAS loop for atomic double accumulation (no fetch_add for double in C++17).
            double prev = stats_.totalFetchMs.load(std::memory_order_relaxed);
            while (!stats_.totalFetchMs.compare_exchange_weak(prev, prev + elapsedMs,
                       std::memory_order_relaxed, std::memory_order_relaxed)) {}
        }
        healthStatus_.store(DemHealthStatus::Healthy);

        NetworkPanel::Instance().RecordComplete(keys[0], RequestType::DemMesh, true, 200,
                                               /*bytes=*/0, elapsedMs, false, "");
        return true;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        NetworkPanel::Instance().RecordComplete(keys[0], RequestType::DemMesh, false, 0, 0, 0.0, false, "curl init failed");
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
    std::string origin = ExtractOrigin(url);
    if (!origin.empty()) {
        headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
        headers = curl_slist_append(headers, ("Referer: " + origin + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    CURLcode res = curl_easy_perform(curl);
    long responseCode = 0;
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
        { // CAS loop for atomic double accumulation.
            double prev = stats_.totalFetchMs.load(std::memory_order_relaxed);
            while (!stats_.totalFetchMs.compare_exchange_weak(prev, prev + elapsedMs,
                       std::memory_order_relaxed, std::memory_order_relaxed)) {}
        }
    } else {
        stats_.fetchFail++;
        if (res == CURLE_OPERATION_TIMEDOUT) stats_.fetchTimeout++;
        if (responseCode == 401 || responseCode == 403) stats_.fetchAuth++;
    }
    
    NetworkPanel::Instance().RecordComplete(keys[0], RequestType::DemMesh, ok, responseCode, 
                                             response.size(), elapsedMs, false, ok ? "" : curl_easy_strerror(res));
    
    if (!ok) {
        if (config_.debug) {
            std::cerr << "[DEM] Batch fetch failed (code: " << responseCode << ")" << std::endl;
        }
        
        // 401/403 auth failure - trigger backoff
        if (responseCode == 401 || responseCode == 403) {
            int fails = consecutiveAuthFails_.fetch_add(1) + 1;
            if (fails >= config_.authBackoffThreshold) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                authBackoff_.store(true);
                backoffUntil_ = std::chrono::steady_clock::now() +
                                std::chrono::seconds(static_cast<int>(config_.authBackoffSec));
                healthStatus_.store(DemHealthStatus::AuthFailed);
                std::cerr << "[DEM] Auth failed " << fails << " times, backoff " 
                          << config_.authBackoffSec << "s" << std::endl;
            }
        } else {
            // Only mark unreachable when we were never healthy. Once we have a valid DEM response,
            // keep health stable so terrain gating does not flicker due to transient network issues.
            if (healthStatus_.load() != DemHealthStatus::Healthy) {
                healthStatus_.store(DemHealthStatus::Unreachable);
            }
        }
        return false;
    }
    
    // Success - reset auth fail counter
    consecutiveAuthFails_.store(0);
    
    bool parsed = ParseBatchResponse(response, static_cast<int>(keys.size()), outDataVec);
    if (parsed) {
        stats_.parseSuccess++;
        // Recover from startup health-check failures once we have a valid parsed response.
        // This is critical for terrain-gated rendering: we must not keep mixing flat+terrain
        // leaves after DEM becomes available.
        healthStatus_.store(DemHealthStatus::Healthy);
    } else {
        stats_.parseFail++;
        if (healthStatus_.load() != DemHealthStatus::Healthy) {
            healthStatus_.store(DemHealthStatus::BadResponse);
        }
    }
    return parsed;
}

DemHealthStatus DemManager::CheckHealth() {
    std::cerr << "[DEM] Health check: " << config_.baseUrl << std::endl;
    
    // Use a known tile (z=1, x=1, y=0 = eastern hemisphere, north) as probe
    TileKey probeKey(1, 1, 0);
    std::vector<TileKey> probeKeys = {probeKey};
    std::vector<DemGridData> probeResults;
    
    bool ok = FetchBatch(probeKeys, probeResults);
    
    DemHealthStatus status;
    if (ok && !probeResults.empty() && probeResults[0].valid) {
        status = DemHealthStatus::Healthy;
        // Cache probe result so it's not wasted
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto now = std::chrono::steady_clock::now();
        cache_[probeKey] = std::move(probeResults[0]);
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

bool DemManager::ParseBatchResponse(const std::string& payload, int cellCount,
                                     std::vector<DemGridData>& outDataVec) const {
    // Service returns a 2D array: [[row0], [row1], ...]
    // For batch CN=N with MESHN=M, response has N*M rows of M values each.
    // Cell i gets rows [i*M .. (i+1)*M - 1] (matching webglobe.js GetMeshData).
    
    const int meshN = config_.meshN;
    const size_t totalRows = static_cast<size_t>(cellCount * meshN);
    const size_t valuesPerRow = static_cast<size_t>(meshN);
    
    // Parse all rows from the JSON 2D array
    std::vector<std::vector<double>> rows;
    rows.reserve(totalRows);
    
    const char* arrayStart = std::strstr(payload.c_str(), "[[");
    if (!arrayStart) {
        if (config_.debug) {
            std::cerr << "[DEM] Parse error: No 2D array found in batch response" << std::endl;
        }
        return false;
    }
    
    // Parse row by row: find each [...] sub-array
    const char* p = arrayStart + 1;  // Skip outer '['
    while (*p) {
        // Find start of next row '['
        while (*p && *p != '[') {
            if (*p == ']' && (*(p+1) == '\0' || *(p+1) == '\n' || *(p+1) == '\r')) break;
            p++;
        }
        if (*p != '[') break;
        p++;  // Skip '['
        
        // Parse values until ']'
        std::vector<double> row;
        row.reserve(valuesPerRow);
        while (*p && *p != ']') {
            // Skip whitespace and commas
            while (*p && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (*p == ']') break;
            
            char* end;
            double val = std::strtod(p, &end);
            if (end > p) {
                row.push_back(val);
                p = end;
            } else {
                p++;
            }
        }
        if (*p == ']') p++;  // Skip ']'
        
        if (!row.empty()) {
            rows.push_back(std::move(row));
        }
    }
    
    if (config_.debug) {
        std::cerr << "[DEM] Parsed " << rows.size() << " rows"
                  << " (expected " << totalRows << " for " << cellCount << " cells)" << std::endl;
    }
    
    if (rows.size() < totalRows) {
        if (config_.debug) {
            std::cerr << "[DEM] Parse error: insufficient rows (" << rows.size() 
                      << " < " << totalRows << ")" << std::endl;
        }
        return false;
    }
    
    // Slice rows into per-cell DemGridData
    // webglobe.js: result[cellIdx][rowIdx] = response[cellIdx * meshN + rowIdx]
    outDataVec.resize(cellCount);
    for (int c = 0; c < cellCount; ++c) {
        DemGridData& data = outDataVec[c];
        data.meshN = meshN;
        data.minHeight = std::numeric_limits<double>::max();
        data.maxHeight = std::numeric_limits<double>::lowest();
        data.heights.clear();
        data.heights.reserve(static_cast<size_t>(meshN * meshN));
        
        for (int r = 0; r < meshN; ++r) {
            const std::vector<double>& row = rows[static_cast<size_t>(c * meshN + r)];
            if (row.size() < valuesPerRow) {
                if (config_.debug) {
                    std::cerr << "[DEM] Parse error: short row (cell=" << c << ", row=" << r
                              << ", cols=" << row.size() << " < " << valuesPerRow << ")" << std::endl;
                }
                return false;
            }
            for (size_t col = 0; col < valuesPerRow; ++col) {
                double val = row[col];
                data.heights.push_back(val);
                data.minHeight = std::min(data.minHeight, val);
                data.maxHeight = std::max(data.maxHeight, val);
            }
        }
        
        data.valid = (data.heights.size() == static_cast<size_t>(meshN * meshN));
    }
    
    return true;
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
