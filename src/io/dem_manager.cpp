#include "dem_manager.h"
#include "../debug/network_panel.h"
#include <curl/curl.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <limits>

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
        if (inFlightKeys_.count(key) > 0) {
            return;  // Worker already fetching this tile.
        }
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

bool DemManager::HasPendingRequest(const TileKey& key) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return pendingRanks_.count(key) > 0 || inFlightKeys_.count(key) > 0;
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

bool DemManager::HasDataOrAncestor(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    TileKey probe = key;
    while (probe.level >= 0) {
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

            // Update access time for LRU eviction.
            auto now = std::chrono::steady_clock::now();
            data.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();

            // Compute UV inside the sampled tile (exact or ancestor).
            double lonLeft = Tile2Lon(sampleX, sampleLevel);
            double lonRight = Tile2Lon(sampleX + 1, sampleLevel);
            double latTop = Tile2Lat(sampleY, sampleLevel);
            double latBottom = Tile2Lat(sampleY + 1, sampleLevel);

            double u = (lonDeg - lonLeft) / (lonRight - lonLeft);
            double v = (latClamped - latTop) / (latBottom - latTop);
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
    auto it = cache_.find(key);
    if (it == cache_.end() || !it->second.valid) {
        return false;
    }
    outData = it->second;
    return true;
}

void DemManager::PutGridData(const TileKey& key, const DemGridData& data) {
    if (!data.valid || data.heights.empty() || data.meshN <= 1) {
        return;
    }
    DemGridData copy = data;
    auto now = std::chrono::steady_clock::now();
    copy.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_[key] = std::move(copy);
}

void DemManager::SetPinnedTiles(const std::vector<TileKey>& keys) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    pinnedKeys_.clear();
    pinnedKeys_.reserve(keys.size());
    for (const TileKey& key : keys) {
        pinnedKeys_.insert(key);
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
    
    // Evict least recently used entries if cache is too large
    while (cache_.size() > config_.cacheSize) {
        // True LRU among unpinned entries: remove entry with oldest lastAccessTime.
        double oldestTime = std::numeric_limits<double>::max();
        TileKey oldestKey;
        bool foundVictim = false;
        for (const auto& [key, data] : cache_) {
            if (pinnedKeys_.count(key) > 0) {
                continue;
            }
            if (data.lastAccessTime < oldestTime) {
                oldestTime = data.lastAccessTime;
                oldestKey = key;
                foundVictim = true;
            }
        }
        if (!foundVictim) {
            break;  // Everything is pinned; postpone eviction for this frame.
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
                results[i].lastAccessTime = accessTime;
                cache_[batch[i]] = std::move(results[i]);
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
    
    // Cells indexed from CN down to 1 (webglobe iterates in reverse: for(o=length;o--;))
    for (size_t i = 0; i < cells.size(); ++i) {
        int idx = static_cast<int>(cells.size() - i);  // CN, CN-1, ..., 1
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

bool DemManager::FetchBatch(const std::vector<TileKey>& keys, std::vector<DemGridData>& outDataVec) {
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
        stats_.totalFetchMs.store(stats_.totalFetchMs.load() + elapsedMs);
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
                authBackoff_.store(true);
                backoffUntil_ = std::chrono::steady_clock::now() + 
                                std::chrono::seconds(static_cast<int>(config_.authBackoffSec));
                healthStatus_.store(DemHealthStatus::AuthFailed);
                std::cerr << "[DEM] Auth failed " << fails << " times, backoff " 
                          << config_.authBackoffSec << "s" << std::endl;
            }
        }
        return false;
    }
    
    // Success - reset auth fail counter
    consecutiveAuthFails_.store(0);
    
    bool parsed = ParseBatchResponse(response, static_cast<int>(keys.size()), outDataVec);
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
    std::vector<TileKey> probeKeys = {probeKey};
    std::vector<DemGridData> probeResults;
    
    bool ok = FetchBatch(probeKeys, probeResults);
    
    DemHealthStatus status;
    if (ok && !probeResults.empty() && probeResults[0].valid) {
        status = DemHealthStatus::Healthy;
        // Cache probe result so it's not wasted
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto now = std::chrono::steady_clock::now();
        probeResults[0].lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();
        cache_[probeKey] = std::move(probeResults[0]);
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
            for (size_t col = 0; col < std::min(row.size(), valuesPerRow); ++col) {
                double val = row[col];
                data.heights.push_back(val);
                data.minHeight = std::min(data.minHeight, val);
                data.maxHeight = std::max(data.maxHeight, val);
            }
            // Pad with zeros if row is short
            for (size_t col = row.size(); col < valuesPerRow; ++col) {
                data.heights.push_back(0.0);
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
