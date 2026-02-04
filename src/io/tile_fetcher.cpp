#include "tile_fetcher.h"
#include "../core/constants.h"
#include <curl/curl.h>
#include <iostream>

namespace globe {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
    auto* bytes = static_cast<uint8_t*>(contents);
    buffer->insert(buffer->end(), bytes, bytes + totalSize);
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
            
            request = std::move(const_cast<FetchRequest&>(queue_.top()));
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
        
        FetchResult result;
        result.key = request.key;
        result.success = DoFetch(request, result);
        
        --activeCount_;
        
        // Invoke callback
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (resultCallback_) {
                resultCallback_(std::move(result));
            }
        }
        
        // Also invoke request-specific callback
        if (request.onComplete) {
            request.onComplete(std::move(result.data), result.success);
        }
    }
}

bool TileFetcher::DoFetch(const FetchRequest& request, FetchResult& result) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to init curl";
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(DOWNLOAD_TIMEOUT_SEC));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(CONNECT_TIMEOUT_SEC));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Skip SSL verification for testing
    
    // User agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, 
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36");
    
    // Add Origin/Referer headers
    struct curl_slist* headers = nullptr;
    std::string origin = ExtractOrigin(request.url);
    if (!origin.empty()) {
        headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
        headers = curl_slist_append(headers, ("Referer: " + origin + "/").c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpStatus);
    
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    
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
    
    return result.httpStatus == 200;
}

} // namespace globe
