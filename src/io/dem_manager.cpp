#include "dem_manager.h"
#include "providers/terrain_rgb_provider.h"
#include "providers/google_earth_dem_provider.h"
#include <curl/curl.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>

namespace globe {

namespace {

// P3: Compute terrain variance from DEM grid heights
// Uses population variance (not sample) for consistent threshold behavior
float ComputeTerrainVariance(const std::vector<double>& heights, int meshN) {
    if (heights.empty() || meshN <= 1) return 0.0f;
    
    // Calculate mean
    double sum = 0.0;
    for (double h : heights) sum += h;
    double mean = sum / heights.size();
    
    // Calculate variance
    double varSum = 0.0;
    for (double h : heights) {
        double diff = h - mean;
        varSum += diff * diff;
    }
    
    return static_cast<float>(varSum / heights.size());
}

std::string ResolveGeElevationEndpoint(const std::string& endpointTemplate,
                                      const std::string& epoch,
                                      const std::string& path = "Elevation") {
    std::string endpoint = endpointTemplate;
    const auto replaceToken = [](std::string& source, const std::string& token,
                                const std::string& value) {
        size_t pos = source.find(token);
        if (pos != std::string::npos) {
            source.replace(pos, token.size(), value);
        }
    };

    const std::string pathValue = path.empty() ? std::string("Elevation") : path;
    replaceToken(endpoint, "{path}", pathValue);

    const std::string epochValue = epoch.empty() ? std::string("latest") : epoch;
    replaceToken(endpoint, "{epoch}", epochValue);

    return endpoint;
}

} // namespace

const char* DemHealthStatusToString(DemHealthStatus s) {
    switch (s) {
        case DemHealthStatus::Unknown:     return "Unknown";
        case DemHealthStatus::Healthy:     return "Healthy";
        case DemHealthStatus::AuthFailed:  return "AuthFailed";
        case DemHealthStatus::Blocked:     return "Blocked";
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

DemManager::DemManager(const Config& config) : config_(config) {
    // Create provider based on config
    switch (config_.providerType) {
        case DemProviderType::TerrainRGB: {
            TerrainRGBConfig trConfig;
            trConfig.baseUrl = config_.baseUrl;
            trConfig.encoding = config_.terrainRgbEncoding;
            trConfig.basicAuthUserPwd = config_.basicAuthUserPwd;
            trConfig.apiKey = config_.apiKey;
            if (trConfig.apiKey.empty() && !config_.apiKeyEnv.empty()) {
                if (const char* env = std::getenv(config_.apiKeyEnv.c_str())) {
                    trConfig.apiKey = env;
                }
            }
            trConfig.timeoutSec = config_.timeoutSec;
            trConfig.connectTimeoutSec = config_.connectTimeoutSec;
            trConfig.meshN = config_.meshN;
            trConfig.maxZoom = config_.maxZoom;
            trConfig.debug = config_.debug;
            trConfig.demNoDataMinHeightM = config_.demNoDataMinHeightM;
            trConfig.demNoDataReplacementM = config_.demNoDataReplacementM;
            trConfig.forceClampTerrainNoData = config_.forceClampTerrainNoData;
            provider_ = std::make_unique<TerrainRGBProvider>(trConfig);
            break;
        }
        case DemProviderType::GoogleEarth: {
            // Create Google Earth DEM provider
            GoogleEarthDemConfig geConfig;
            geConfig.elevationEndpoint = ResolveGeElevationEndpoint(
                config_.geElevationEndpoint, config_.geEpoch, config_.geElevationPath);
            geConfig.headers = config_.geHeaders;
            geConfig.authToken = std::getenv(config_.geTokenEnv.c_str()) ? 
                                 std::getenv(config_.geTokenEnv.c_str()) : "";
            geConfig.elevationType = config_.geElevationType;
            geConfig.meshN = config_.meshN;
            geConfig.maxZoom = config_.maxZoom;
            geConfig.timeoutSec = config_.timeoutSec;
            provider_ = std::make_unique<GoogleEarthDemProvider>(geConfig);
            break;
        }
    }

    // Start worker threads
    int numWorkers = 4;
    workers_.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

#ifdef NATIVE_GLOBE_TESTING
DemManager::DemManager(const Config& config, std::unique_ptr<ITerrainDemProvider> testProvider) 
    : config_(config), provider_(std::move(testProvider)) {
    // Test constructor - uses injected provider instead of creating one
    // Start worker threads
    int numWorkers = 4;
    workers_.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

bool DemManager::TestFetchDirect(const TileKey& key, DemGridData& outData) {
    return FetchTile(key, outData);
}

int DemManager::GetConsecutiveAuthFailsForTest() const {
    return consecutiveAuthFails_.load();
}
#endif

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
    auto it = lruIterMap_.find(key);
    if (it != lruIterMap_.end()) {
        lruOrder_.splice(lruOrder_.begin(), lruOrder_, it->second);
    }
}

void DemManager::Request(const TileKey& key, int priority, double score) {
    // Terminal error gate
    if (terminalError_.load()) {
        return;
    }
    
    // No provider gate
    if (!provider_) {
        return;
    }
    
    // Auth backoff check
    if (authBackoff_.load()) {
        auto now = std::chrono::steady_clock::now();
        bool stillBackingOff = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (now < backoffUntil_) {
                stillBackingOff = true;
            } else {
                authBackoff_.store(false);
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

    // Check cache and fail TTL
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        coEvictedKeys_.erase(requestKey);
        auto it = cache_.find(requestKey);
        if (it != cache_.end()) {
            if (it->second.valid) {
                return;
            }
            auto lruIt = lruIterMap_.find(requestKey);
            if (lruIt != lruIterMap_.end()) { 
                lruOrder_.erase(lruIt->second); 
                lruIterMap_.erase(lruIt); 
            }
            cache_.erase(it);
        }
        auto failIt = failedUntil_.find(requestKey);
        if (failIt != failedUntil_.end()) {
            auto now = std::chrono::steady_clock::now();
            if (now < failIt->second) {
                return;
            }
            failedUntil_.erase(failIt);
        }
    }
    
    priority = std::clamp(priority, 0, 2);

    // Queue request
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (inFlightKeys_.count(requestKey) > 0) {
            return;
        }
        auto it = pendingRanks_.find(requestKey);
        if (it != pendingRanks_.end()) {
            bool better = (priority > it->second.priority) ||
                          (priority == it->second.priority && score > it->second.score);
            if (!better) {
                return;
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
            if (probe.level == 0) break;
            probe = probe.Parent();
            continue;
        }
        auto it = cache_.find(probe);
        if (it != cache_.end() && it->second.valid) {
            return true;
        }
        if (probe.level == 0) break;
        probe = probe.Parent();
    }
    return false;
}

bool DemManager::GetBestAvailableLevel(const TileKey& key, int& outLevel) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    TileKey probe = key;
    while (probe.level >= 0) {
        if (coEvictedKeys_.count(probe) > 0) {
            if (probe.level == 0) break;
            probe = probe.Parent();
            continue;
        }
        auto it = cache_.find(probe);
        if (it != cache_.end() && it->second.valid) {
            outLevel = probe.level;
            return true;
        }
        if (probe.level == 0) break;
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

    int n = 1 << level;
    double latClamped = std::clamp(latDeg, -85.05112878, 85.05112878);
    double latRad = latClamped * M_PI / 180.0;

    int tileX = static_cast<int>((lonDeg + 180.0) / 360.0 * n);
    int tileY = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);
    
    tileX = std::clamp(tileX, 0, n - 1);
    tileY = std::clamp(tileY, 0, n - 1);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);

    int sampleX = tileX;
    int sampleY = tileY;
    for (int sampleLevel = level; sampleLevel >= 0; --sampleLevel) {
        TileKey key(sampleLevel, sampleX, sampleY);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.valid) {
            const DemGridData& data = it->second;
            TouchLru(key);

            double lonLeft = Tile2Lon(sampleX, sampleLevel);
            double lonRight = Tile2Lon(sampleX + 1, sampleLevel);
            double latTop = Tile2Lat(sampleY, sampleLevel);
            double latBottom = Tile2Lat(sampleY + 1, sampleLevel);

            double u = (lonDeg - lonLeft) / (lonRight - lonLeft);
            double v = (latClamped - latBottom) / (latTop - latBottom);
            u = std::clamp(u, 0.0, 1.0);
            v = std::clamp(v, 0.0, 1.0);

            out.ok = true;
            out.heightMeters = SampleBilinear(data, u, v);
            out.sourceLevel = sampleLevel;
            out.usedAncestor = sampleLevel != level;
            return true;
        }

        if (sampleLevel == 0) break;
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
    auto lruIt = lruIterMap_.find(key);
    if (lruIt != lruIterMap_.end()) {
        lruOrder_.erase(lruIt->second);
    }
    lruOrder_.push_front(key);
    lruIterMap_[key] = lruOrder_.begin();
}

bool DemManager::GetTerrainVariance(const TileKey& key, float& outVariance) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it == cache_.end() || !it->second.valid) {
        return false;
    }
    TouchLru(key);
    outVariance = it->second.terrainVariance;
    return true;
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
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    while (cache_.size() > config_.cacheSize && !lruOrder_.empty()) {
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
            break;
        }
    }
}

int DemManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return static_cast<int>(cache_.size());
}

DemHealthStatus DemManager::GetHealthStatus() const {
    if (provider_) {
        return provider_->GetHealthStatus();
    }
    return DemHealthStatus::Unknown;
}

bool DemManager::IsTerminalError() const {
    if (terminalError_.load()) {
        return true;
    }
    if (provider_) {
        return provider_->IsTerminalError();
    }
    return false;
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
            
            const int batchLimit = std::max(1, config_.maxBatchSize);

            while (!requestQueue_.empty() && static_cast<int>(batch.size()) < batchLimit) {
                DemRequest req = requestQueue_.top();
                requestQueue_.pop();

                auto it = pendingRanks_.find(req.key);
                if (it == pendingRanks_.end()) {
                    continue;
                }

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
        
        // P1-4: Batch rate limiting (backoff) - shared across all workers
        if (config_.batchBackoffMs > 0) {
            std::unique_lock<std::mutex> backoffLock(batchBackoffMutex_);
            auto now = std::chrono::steady_clock::now();
            if (now < nextBatchAllowedAt_) {
                auto waitTime = nextBatchAllowedAt_ - now;
                auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitTime).count();
                
                stats_.batchBackoffDelays.fetch_add(1);
                // P1-4: Atomic double update (load-add-store pattern for compatibility)
                double currentTotal = stats_.batchBackoffMsTotal.load();
                while (!stats_.batchBackoffMsTotal.compare_exchange_weak(currentTotal, currentTotal + waitMs)) {}
                
                backoffLock.unlock();
                std::this_thread::sleep_for(waitTime);
                backoffLock.lock();
            }
            // Schedule next batch slot
            nextBatchAllowedAt_ = std::chrono::steady_clock::now() + 
                                  std::chrono::milliseconds(config_.batchBackoffMs);
        }
        
        // P1-4: Increment batch counter
        stats_.batchCount.fetch_add(1);
        
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
                    auto lruIt = lruIterMap_.find(key);
                    if (lruIt != lruIterMap_.end()) { 
                        lruOrder_.erase(lruIt->second); 
                    }
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
    if (!provider_) {
        return false;
    }
    
    DemFetchResult result;
    bool success = provider_->FetchDemTile(key, outData, result);
    
    // Update telemetry
    if (success) {
        stats_.fetchSuccess++;
        stats_.parseSuccess++;
        // P3: Compute terrain variance for adaptive LOD
        if (outData.valid && !outData.heights.empty()) {
            outData.terrainVariance = ComputeTerrainVariance(outData.heights, outData.meshN);
        }
        // Accumulate timing
        double prev = stats_.totalFetchMs.load(std::memory_order_relaxed);
        while (!stats_.totalFetchMs.compare_exchange_weak(
                   prev, prev + result.elapsedMs, std::memory_order_relaxed, std::memory_order_relaxed)) {}
        // Reset auth fail counter on successful fetch
        consecutiveAuthFails_.store(0);
    } else {
        stats_.fetchFail++;
        
        // Categorize failure
        if (result.IsAuthFailure()) {
            stats_.fetchAuth++;
            // Trigger auth backoff
            int fails = consecutiveAuthFails_.fetch_add(1) + 1;
            if (fails >= config_.authBackoffThreshold) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                authBackoff_.store(true);
                backoffUntil_ = std::chrono::steady_clock::now() +
                                std::chrono::seconds(static_cast<int>(config_.authBackoffSec));
                std::cerr << "[DEM] Auth failed " << fails << " times, backoff " 
                          << config_.authBackoffSec << "s" << std::endl;
            }
        } else if (result.errorType == DemFetchResult::ErrorType::Blocked) {
            if (!terminalError_.load()) {
                std::cerr << "[DEM] Elevation requests blocked by provider anti-automation policy. "
                          << "Disabling DEM fetch queue and using available fallbacks."
                          << std::endl;
                terminalError_.store(true);
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    // Clear queue by popping all elements
                    while (!requestQueue_.empty()) {
                        requestQueue_.pop();
                    }
                    pendingRanks_.clear();
                    pendingCount_.store(0);
                }
            }
            consecutiveAuthFails_.store(0);
        } else if (result.IsTimeout()) {
            stats_.fetchTimeout++;
        }
        
        if (result.errorType == DemFetchResult::ErrorType::Decode) {
            stats_.parseFail++;
        }
    }
    
    return success;
}

DemHealthStatus DemManager::CheckHealth() {
    if (!provider_) {
        if (config_.providerType == DemProviderType::GoogleEarth) {
            std::cerr << "[DEM] Health check: Google Earth provider not implemented" << std::endl;
            return DemHealthStatus::BadResponse;
        }
        return DemHealthStatus::Unknown;
    }
    
    return provider_->CheckHealth();
}

double DemManager::SampleBilinear(const DemGridData& data, double u, double v) const {
    const int n = data.meshN;
    if (n < 2) return data.heights.empty() ? 0.0 : data.heights[0];
    
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    
    const double fx = u * (n - 1);
    const double fy = v * (n - 1);
    
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, n - 1);
    const int y1 = std::min(y0 + 1, n - 1);
    
    const double dx = fx - x0;
    const double dy = fy - y0;
    
    const double h00 = data.heights[y0 * n + x0];
    const double h10 = data.heights[y0 * n + x1];
    const double h01 = data.heights[y1 * n + x0];
    const double h11 = data.heights[y1 * n + x1];
    if (!std::isfinite(h00) || !std::isfinite(h10) ||
        !std::isfinite(h01) || !std::isfinite(h11)) {
        stats_.bilinearNonFinite.fetch_add(1, std::memory_order_relaxed);
        return 0.0;
    }
    
    const double sample = h00 * (1 - dx) * (1 - dy) +
           h10 * dx * (1 - dy) +
           h01 * (1 - dx) * dy +
           h11 * dx * dy;
    if (!std::isfinite(sample)) {
        stats_.bilinearNonFinite.fetch_add(1, std::memory_order_relaxed);
        return 0.0;
    }
    return sample;
}

} // namespace globe
