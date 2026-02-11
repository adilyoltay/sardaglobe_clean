#include "tile_fetcher.h"
#include "decoded_tile_blob.h"
#include "../core/constants.h"
#include "../debug/network_panel.h"
#include <curl/curl.h>
#include <iostream>
#include <optional>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <thread>

namespace globe {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
    size_t offset = buffer->size();
    buffer->resize(offset + totalSize);
    std::memcpy(buffer->data() + offset, contents, totalSize);
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

bool LooksLikeImage(const std::vector<uint8_t>& data) {
    if (data.size() >= 8) {
        static const uint8_t pngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(data.data(), pngSig, sizeof(pngSig)) == 0) {
            return true;
        }
    }
    if (data.size() >= 3) {
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
            return true;
        }
    }
    if (data.size() >= 12) {
        if (std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WEBP", 4) == 0) {
            return true;
        }
    }
    return false;
}

bool StartsWith(const std::string& s, const char* prefix) {
    if (!prefix) return false;
    size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

// Parses the last 3 unsigned integers found in the URL (tolerates extra path/query segments).
bool ParseLastZxy(const std::string& url, int& outZ, int& outX, int& outY) {
    std::vector<int> nums;
    nums.reserve(8);
    const char* p = url.c_str();
    while (*p) {
        while (*p && (*p < '0' || *p > '9')) {
            ++p;
        }
        if (!*p) break;
        int v = 0;
        while (*p && (*p >= '0' && *p <= '9')) {
            int digit = (*p - '0');
            if (v > 100000000) {  // avoid overflow; values are expected small anyway
                return false;
            }
            v = v * 10 + digit;
            ++p;
        }
        nums.push_back(v);
    }
    if (nums.size() < 3) {
        return false;
    }
    outZ = nums[nums.size() - 3];
    outX = nums[nums.size() - 2];
    outY = nums[nums.size() - 1];
    return true;
}

// Deterministic, continuous debug tiles. Returns a decoded-blob payload (TileDecoder fast path).
bool GenerateNgrdTile(int z, int x, int y, std::vector<uint8_t>& outPacked) {
    constexpr int kSize = 256;
    constexpr double kMaxMercatorLatDeg = 85.05112878;
    const double n = std::ldexp(1.0, std::clamp(z, 0, 30));  // 2^z (cap to keep math stable)

    std::vector<uint8_t> rgba(static_cast<size_t>(kSize) * static_cast<size_t>(kSize) * 4u, 0);

    auto clamp255 = [](double v) -> uint8_t {
        v = std::clamp(v, 0.0, 255.0);
        return static_cast<uint8_t>(std::lround(v));
    };

    // IMPORTANT: Match the engine's texture orientation assumptions:
    // - Tile meshes use v=0 at South and v=1 at North (see TileMeshBuilder: push (u, 1-v)).
    // - stb_image decoding flips vertically on load, so decoded pixel row0 corresponds to South.
    // For decoded-blob tiles, we generate row0 as South to match.
    for (int py = 0; py < kSize; ++py) {
        // py=0 is bottom row (South)
        double yTile = static_cast<double>(y) + 1.0 - (static_cast<double>(py) + 0.5) / kSize;
        double yFrac = yTile / n;
        double mercY = M_PI * (1.0 - 2.0 * yFrac);
        double latRad = std::atan(std::sinh(mercY));
        double latDeg = latRad * 180.0 / M_PI;
        latDeg = std::clamp(latDeg, -kMaxMercatorLatDeg, kMaxMercatorLatDeg);
        double lat01 = (latDeg + kMaxMercatorLatDeg) / (2.0 * kMaxMercatorLatDeg);

        for (int px = 0; px < kSize; ++px) {
            double xTile = static_cast<double>(x) + (static_cast<double>(px) + 0.5) / kSize;
            double xFrac = xTile / n;
            double lonDeg = xFrac * 360.0 - 180.0;
            double lon01 = (lonDeg + 180.0) / 360.0;

            // Smooth global gradient across tile boundaries (no seams).
            uint8_t r = clamp255(lon01 * 255.0);
            uint8_t g = clamp255(lat01 * 255.0);
            uint8_t b = clamp255((static_cast<double>(z) / 22.0) * 255.0);

            size_t idx = (static_cast<size_t>(py) * static_cast<size_t>(kSize) + static_cast<size_t>(px)) * 4u;
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = 255;
        }
    }

    return decoded_blob::Pack(kSize, kSize, rgba, outPacked);
}

// Thread-local current key for cancel checks
static thread_local std::optional<TileKey> tls_currentKey;
static thread_local CURL* tls_curl = nullptr;
static thread_local struct curl_slist* tls_headers = nullptr;

} // anonymous namespace

TileFetcher::TileFetcher(int numWorkers, std::string basicAuthUserPwd)
    : basicAuthUserPwd_(std::move(basicAuthUserPwd)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    workers_.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

TileFetcher::~TileFetcher() {
    Shutdown();
    curl_global_cleanup();
}

void TileFetcher::SetResultCallback(ResultCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    resultCallback_ = std::move(callback);
}

void TileFetcher::Fetch(FetchRequest request) {
    bool enqueued = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto betterThan = [](Priority aPri, double aScore, Priority bPri, double bScore) -> bool {
            if (aPri != bPri) {
                return static_cast<int>(aPri) > static_cast<int>(bPri);
            }
            return aScore > bScore;
        };

        auto it = bestRanks_.find(request.key);
        bool better = false;
        if (it == bestRanks_.end()) {
            better = true;
        } else if (betterThan(request.priority, request.score, it->second.priority, it->second.score)) {
            better = true;
        }

        if (better) {
            PendingRank rank;
            rank.priority = request.priority;
            rank.score = request.score;
            rank.seq = ++enqueueSeq_;
            bestRanks_[request.key] = rank;
            request.seq = rank.seq;

            // If the key is already being fetched by a worker, don't enqueue a duplicate.
            // We still update bestRanks_ so the in-flight result can be attributed with the latest rank.
            if (inFlight_.count(request.key) == 0) {
                queue_.push(std::move(request));
                enqueued = true;
            }
        }
    }
    if (enqueued) {
        queueCv_.notify_one();
    }
}

void TileFetcher::Cancel(const TileKey& key) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        bestRanks_.erase(key);  // Prevent stale queued requests from ever running
    }
    {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        cancelled_.insert(key);
    }
}

void TileFetcher::Shutdown() {
    running_ = false;
    queueCv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

int TileFetcher::GetPendingCount() const {
    // Note: This is approximate due to no lock
    return static_cast<int>(queue_.size());
}

int TileFetcher::GetActiveCount() const {
    return activeCount_.load();
}

uint64_t TileFetcher::GetFetchCount() const {
    return fetchCount_.load();
}

uint64_t TileFetcher::GetTotalFetchTimeUs() const {
    return totalFetchTimeUs_.load();
}

static CURL* GetThreadLocalCurl() {
    if (!tls_curl) {
        tls_curl = curl_easy_init();
    }
    return tls_curl;
}

static void CleanupThreadLocalCurl() {
    if (tls_headers) {
        curl_slist_free_all(tls_headers);
        tls_headers = nullptr;
    }
    if (tls_curl) {
        curl_easy_cleanup(tls_curl);
        tls_curl = nullptr;
    }
    tls_currentKey.reset();
}

void TileFetcher::WorkerLoop() {
    while (running_) {
        FetchRequest request;
        bool staleQueuedRequest = false;
        bool duplicateInFlight = false;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !queue_.empty();
            });
            
            if (!running_) break;
            if (queue_.empty()) continue;
            
            // Copy then pop - avoids UB from const_cast + move on priority_queue::top()
            request = queue_.top();
            queue_.pop();

            // Lazy stale-skip: if a newer (higher priority/score) request exists for the same key,
            // drop this one without doing any work.
            auto it = bestRanks_.find(request.key);
            if (it == bestRanks_.end() || request.seq != it->second.seq) {
                staleQueuedRequest = true;
            } else if (inFlight_.count(request.key) != 0) {
                duplicateInFlight = true;
            } else {
                // Mark in-flight under the same lock so Fetch() can avoid enqueuing duplicates.
                inFlight_.insert(request.key);
            }
        }

        if (staleQueuedRequest) {
            // Cancel() clears bestRanks_ before queued requests are popped.
            // Consume any stale cancel marker so a fresh re-request isn't aborted.
            std::lock_guard<std::mutex> lock(cancelMutex_);
            cancelled_.erase(request.key);
            continue;
        }

        if (duplicateInFlight) {
            continue;
        }
        
        // Check if cancelled
        bool isCancelled = false;
        {
            std::lock_guard<std::mutex> lock(cancelMutex_);
            auto it = cancelled_.find(request.key);
            if (it != cancelled_.end()) {
                cancelled_.erase(it);
                isCancelled = true;
            }
        }

        if (isCancelled) {
            // Drop any pending best-rank and clear in-flight marker.
            std::lock_guard<std::mutex> qlock(queueMutex_);
            bestRanks_.erase(request.key);
            inFlight_.erase(request.key);
            continue;
        }
        
        ++activeCount_;

        // Network panel: record start
        NetworkPanel::Instance().RecordStart(request.key, RequestType::RasterTile, request.url);

        auto start = std::chrono::high_resolution_clock::now();

        FetchResult result;
        result.key = request.key;
        result.priority = request.priority;
        result.score = request.score;

        bool cacheHit = false;
        // Cache check (worker thread)
        std::vector<uint8_t> cachedData;
        if (request.tryReadCache && request.tryReadCache(request.key, cachedData)) {
            result.data = std::move(cachedData);
            result.success = true;
            result.httpStatus = 200;
            cacheHit = true;
        } else {
            result.success = DoFetch(request, result);
            if (result.success && request.writeCache) {
                request.writeCache(request.key, result.data);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        uint64_t elapsedUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        double elapsedMs = elapsedUs / 1000.0;
        fetchCount_.fetch_add(1);
        totalFetchTimeUs_.fetch_add(elapsedUs);

        // Network panel: record completion
        NetworkPanel::Instance().RecordComplete(
            request.key, RequestType::RasterTile, result.success,
            result.httpStatus, result.data.size(), elapsedMs, cacheHit, result.error);

        --activeCount_;

        // Attribute completion with latest rank (in case we were upgraded while in-flight),
        // and clear per-key bookkeeping.
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            auto it = bestRanks_.find(request.key);
            if (it != bestRanks_.end()) {
                result.priority = it->second.priority;
                result.score = it->second.score;
                bestRanks_.erase(it);
            }
            inFlight_.erase(request.key);
        }
        
        // Invoke callback
        ResultCallback callbackCopy;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            callbackCopy = resultCallback_;
        }
        if (callbackCopy) {
            callbackCopy(std::move(result));
        } else if (request.onComplete) {
            // Only call per-request callback when no global callback is set
            request.onComplete(std::move(result.data), result.success);
        }
    }

    CleanupThreadLocalCurl();
}

int TileFetcher::ProgressCallback(void* userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* fetcher = static_cast<TileFetcher*>(userp);
    if (!tls_currentKey.has_value()) return 0;
    std::lock_guard<std::mutex> lock(fetcher->cancelMutex_);
    if (fetcher->cancelled_.count(*tls_currentKey)) {
        fetcher->cancelled_.erase(*tls_currentKey);
        return 1;  // Abort transfer
    }
    return 0;
}

bool TileFetcher::DoFetch(const FetchRequest& request, FetchResult& result) {
    // Synthetic/debug tile source (offline): "ngrd://{z}/{x}/{y}"
    // Generates a decoded-blob payload so TileDecoder can skip image codecs.
    if (StartsWith(request.url, "ngrd://")) {
        int z = 0, x = 0, y = 0;
        if (!ParseLastZxy(request.url, z, x, y) || z < 0 || x < 0 || y < 0) {
            result.httpStatus = 400;
            result.error = "Invalid ngrd:// URL (expected .../{z}/{x}/{y})";
            return false;
        }

        // Optional latency injection for stress-testing streaming (ms).
        // Example: ngrd://delay=80/{z}/{x}/{y}
        int delayMs = 0;
        size_t dpos = request.url.find("delay=");
        if (dpos != std::string::npos) {
            dpos += 6;
            int v = 0;
            while (dpos < request.url.size() && request.url[dpos] >= '0' && request.url[dpos] <= '9') {
                v = v * 10 + (request.url[dpos] - '0');
                ++dpos;
                if (v > 10000) break;
            }
            delayMs = std::clamp(v, 0, 10000);
        }
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        std::vector<uint8_t> packed;
        if (!GenerateNgrdTile(z, x, y, packed)) {
            result.httpStatus = 500;
            result.error = "Failed to generate ngrd tile";
            return false;
        }
        result.data = std::move(packed);
        result.httpStatus = 200;
        return true;
    }

    CURL* curl = GetThreadLocalCurl();
    if (!curl) {
        result.error = "Failed to init curl";
        return false;
    }

    curl_easy_reset(curl);

    // Persistent options after reset
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

    // Per-request options
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(DOWNLOAD_TIMEOUT_SEC));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(CONNECT_TIMEOUT_SEC));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);

    // User agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36");

    // Optional basic auth (tile endpoints in this project often require it).
    if (!basicAuthUserPwd_.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, basicAuthUserPwd_.c_str());
    }

    // Cancel hook
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &TileFetcher::ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);

    // Add Origin/Referer headers
    if (tls_headers) {
        curl_slist_free_all(tls_headers);
        tls_headers = nullptr;
    }
    std::string origin = ExtractOrigin(request.url);
    if (!origin.empty()) {
        tls_headers = curl_slist_append(tls_headers, ("Origin: " + origin).c_str());
        tls_headers = curl_slist_append(tls_headers, ("Referer: " + origin + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, tls_headers);
    }

    result.data.reserve(256 * 1024);
    tls_currentKey = request.key;
    CURLcode res = curl_easy_perform(curl);
    tls_currentKey.reset();
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpStatus);
    const char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    
    // Debug: Log non-200 responses
    if (result.httpStatus != 200) {
        std::cerr << "[FETCH] " << request.key.ToString() 
                  << " HTTP " << result.httpStatus << std::endl;
    }
    
    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        std::cerr << "[FETCH] " << request.key.ToString() 
                  << " CURL error: " << result.error << std::endl;
        return false;
    }

    if (result.httpStatus != 200) {
        result.error = "HTTP " + std::to_string(result.httpStatus);
        return false;
    }

    if (!LooksLikeImage(result.data)) {
        if (contentType && std::strncmp(contentType, "image/", 6) != 0) {
            result.error = std::string("Non-image content-type: ") + contentType;
        } else {
            result.error = "Invalid image data";
        }
        std::cerr << "[FETCH] " << request.key.ToString()
                  << " invalid image payload" << std::endl;
        return false;
    }
    
    return true;
}

} // namespace globe
