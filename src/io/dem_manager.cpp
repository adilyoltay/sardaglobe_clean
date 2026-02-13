#include "dem_manager.h"
#include "providers/terrain_rgb_provider.h"
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

DemManager::DemManager(const Config& config) : config_(config) {
    // Create provider based on config
    switch (config_.providerType) {
        case DemProviderType::TerrainRGB: {
            TerrainRGBConfig trConfig;
            trConfig.baseUrl = config_.baseUrl;
            trConfig.basicAuthUserPwd = config_.basicAuthUserPwd;
            trConfig.timeoutSec = config_.timeoutSec;
            trConfig.connectTimeoutSec = config_.connectTimeoutSec;
            trConfig.meshN = config_.meshN;
            trConfig.maxZoom = config_.maxZoom;
            trConfig.debug = config_.debug;
            provider_ = std::make_unique<TerrainRGBProvider>(trConfig);
            break;
        }
        case DemProviderType::GoogleEarth:
            // Phase 4/5: Will create GoogleEarthProvider here
            // For now, provider_ remains null and FetchTile will handle
            break;
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
            
            while (!requestQueue_.empty() && static_cast<int>(batch.size()) < 1) {
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
        // google-earth not yet implemented
        if (config_.providerType == DemProviderType::GoogleEarth) {
            std::cerr << "[DEM] ERROR: Google Earth provider not yet implemented (Phase 4/5). "
                      << "Use --dem-provider terrain-rgb or check back later." << std::endl;
            terminalError_.store(true);
        }
        return false;
    }
    
    bool success = provider_->FetchDemTile(key, outData);
    
    // Update telemetry
    if (success) {
        stats_.fetchSuccess++;
    } else {
        stats_.fetchFail++;
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
    
    return h00 * (1 - dx) * (1 - dy) +
           h10 * dx * (1 - dy) +
           h01 * (1 - dx) * dy +
           h11 * dx * dy;
}

} // namespace globe
