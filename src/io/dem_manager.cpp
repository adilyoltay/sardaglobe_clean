#include "dem_manager.h"
#include <curl/curl.h>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace globe {

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

void DemManager::Request(const TileKey& key) {
    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(key) != cache_.end()) {
            return;  // Already have data
        }
    }
    
    // Add to request queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        requestQueue_.push(key);
        pendingCount_++;
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
            
            key = requestQueue_.front();
            requestQueue_.pop();
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
        
        pendingCount_--;
    }
}

std::string DemManager::BuildDemUrl(const DemCell& cell) const {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(6);
    
    oss << config_.baseUrl
        << "?llx=" << cell.llx
        << "&lly=" << cell.lly
        << "&urx=" << cell.urx
        << "&ury=" << cell.ury
        << "&rows=" << config_.meshN
        << "&cols=" << config_.meshN;
    
    return oss.str();
}

bool DemManager::FetchDem(const TileKey& key, DemGridData& outData) {
    // Calculate tile bounds
    DemCell cell;
    cell.level = key.level;
    cell.tileX = key.x;
    cell.tileY = key.y;
    cell.llx = Tile2Lon(key.x, key.level);
    cell.urx = Tile2Lon(key.x + 1, key.level);
    cell.ury = Tile2Lat(key.y, key.level);      // Top (north)
    cell.lly = Tile2Lat(key.y + 1, key.level);  // Bottom (south)
    
    std::string url = BuildDemUrl(cell);
    
    if (config_.debug) {
        std::cerr << "[DEM] Fetching: " << url << std::endl;
    }
    
    // HTTP request
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || responseCode != 200) {
        if (config_.debug) {
            std::cerr << "[DEM] Fetch failed: " << curl_easy_strerror(res) 
                      << " (code: " << responseCode << ")" << std::endl;
        }
        return false;
    }
    
    // Parse response
    return ParseDemGrid(response, outData);
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
