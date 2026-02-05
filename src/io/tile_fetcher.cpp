#include "tile_fetcher.h"
#include "../core/constants.h"
#include "../debug/network_panel.h"
#include <curl/curl.h>
#include <iostream>
#include <optional>
#include <cstring>
#include <chrono>

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

// Thread-local current key for cancel checks
static thread_local std::optional<TileKey> tls_currentKey;
static thread_local CURL* tls_curl = nullptr;
static thread_local struct curl_slist* tls_headers = nullptr;

} // anonymous namespace

TileFetcher::TileFetcher(int numWorkers) {
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
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push(std::move(request));
    }
    queueCv_.notify_one();
}

void TileFetcher::Cancel(const TileKey& key) {
    std::lock_guard<std::mutex> lock(cancelMutex_);
    cancelled_.insert(key);
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
        }
        
        // Check if cancelled
        {
            std::lock_guard<std::mutex> lock(cancelMutex_);
            if (cancelled_.count(request.key)) {
                cancelled_.erase(request.key);
                continue;
            }
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
            result.httpStatus, result.data.size(), elapsedMs, cacheHit);

        --activeCount_;
        
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
