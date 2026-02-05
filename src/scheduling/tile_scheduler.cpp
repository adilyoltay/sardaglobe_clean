#include "tile_scheduler.h"
#include "tile_state_machine.h"
#include <regex>
#include <iostream>

namespace globe {

TileScheduler::TileScheduler(const Config& config) 
    : config_(config) {
    
    fetcher_ = std::make_unique<TileFetcher>(config.maxConcurrentFetches);
    decoder_ = std::make_unique<TileDecoder>(config.maxConcurrentDecodes);
    cache_ = std::make_unique<TileCache>(config.cacheDir);
    cache_->SetEnabled(config.useDiskCache);
    
    // Set callbacks
    fetcher_->SetResultCallback([this](FetchResult result) {
        OnFetchComplete(std::move(result));
    });
    
    decoder_->SetResultCallback([this](DecodeResult result) {
        OnDecodeComplete(std::move(result));
    });
}

TileScheduler::~TileScheduler() {
    if (fetcher_) fetcher_->Shutdown();
    if (decoder_) decoder_->Shutdown();
}

std::string TileScheduler::BuildUrl(const TileKey& key) const {
    std::string url = config_.tileUrl;
    url = std::regex_replace(url, std::regex("\\{z\\}"), std::to_string(key.level));
    url = std::regex_replace(url, std::regex("\\{x\\}"), std::to_string(key.x));
    url = std::regex_replace(url, std::regex("\\{y\\}"), std::to_string(key.y));
    return url;
}

void TileScheduler::Request(const TileKey& key, Priority priority, float score) {
    // Check cache first
    std::vector<uint8_t> cachedData;
    if (cache_->Read(key, config_.tileUrl, cachedData)) {
        // Send directly to decoder
        {
            std::lock_guard<std::mutex> lock(trackingMutex_);
            if (pendingDecodes_.count(key)) return;  // Already decoding
            pendingDecodes_.insert(key);
        }
        
        DecodeRequest req;
        req.key = key;
        req.data = std::move(cachedData);
        decoder_->Decode(std::move(req));
        return;
    }
    
    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(trackingMutex_);
        if (pendingFetches_.count(key) || pendingDecodes_.count(key)) {
            return;  // Already in progress
        }
        pendingFetches_.insert(key);
    }
    
    // Queue fetch
    FetchRequest req;
    req.key = key;
    req.url = BuildUrl(key);
    req.priority = priority;
    req.score = score;
    
    fetcher_->Fetch(std::move(req));
}

void TileScheduler::Cancel(const TileKey& key) {
    fetcher_->Cancel(key);
    
    std::lock_guard<std::mutex> lock(trackingMutex_);
    pendingFetches_.erase(key);
}

void TileScheduler::Update(TileMap& tiles, double currentTime) {
    // Process dropped keys first - mark tiles as Failed for retry
    {
        std::lock_guard<std::mutex> lock(droppedKeysMutex_);
        while (!droppedKeys_.empty()) {
            TileKey key = droppedKeys_.front();
            droppedKeys_.pop();
            
            auto it = tiles.find(key);
            if (it != tiles.end()) {
                TileStateMachine::Advance(it->second, TileStateMachine::Event::Drop, currentTime);
            }
        }
    }
    
    // Process fetch results
    {
        std::lock_guard<std::mutex> lock(fetchResultsMutex_);
        while (!fetchResults_.empty()) {
            FetchResult result = std::move(fetchResults_.front());
            fetchResults_.pop();
            
            {
                std::lock_guard<std::mutex> tlock(trackingMutex_);
                pendingFetches_.erase(result.key);
            }
            
            if (!result.success) {
                // Mark tile as failed via state machine
                auto it = tiles.find(result.key);
                if (it != tiles.end()) {
                    TileStateMachine::Advance(it->second, TileStateMachine::Event::FetchFail, currentTime);
                }
                ++recentFetchFails_;
                continue;
            }
            
            // Cache the data
            cache_->Write(result.key, config_.tileUrl, result.data);
            
            // Send to decoder
            {
                std::lock_guard<std::mutex> tlock(trackingMutex_);
                pendingDecodes_.insert(result.key);
            }
            
            DecodeRequest dreq;
            dreq.key = result.key;
            dreq.data = std::move(result.data);
            decoder_->Decode(std::move(dreq));
            
            // Update tile state via state machine
            auto it = tiles.find(result.key);
            if (it != tiles.end()) {
                TileStateMachine::Advance(it->second, TileStateMachine::Event::FetchOk, currentTime);
            }
        }
    }
    
    // Process decode results
    {
        std::lock_guard<std::mutex> lock(decodeResultsMutex_);
        while (!decodeResults_.empty()) {
            DecodeResult result = std::move(decodeResults_.front());
            decodeResults_.pop();
            
            {
                std::lock_guard<std::mutex> tlock(trackingMutex_);
                pendingDecodes_.erase(result.key);
            }
            
            auto it = tiles.find(result.key);
            if (it == tiles.end()) continue;
            
            if (!result.success) {
                TileStateMachine::Advance(it->second, TileStateMachine::Event::DecodeFail, currentTime);
                continue;
            }
            
            // Store decoded pixels for GPU upload
            it->second.pixels = std::move(result.pixels);
            it->second.pixelWidth = result.width;
            it->second.pixelHeight = result.height;
            TileStateMachine::Advance(it->second, TileStateMachine::Event::DecodeOk, currentTime);
            
            // Notify for upload
            if (uploadCallback_) {
                uploadCallback_(it->second);
            }
        }
    }
}

void TileScheduler::SetUploadCallback(UploadCallback callback) {
    uploadCallback_ = std::move(callback);
}

void TileScheduler::OnFetchComplete(FetchResult result) {
    std::lock_guard<std::mutex> lock(fetchResultsMutex_);
    
    // Drop oldest if queue is full (backpressure)
    // CRITICAL: Track dropped key so Update() can mark tile as Failed
    if (fetchResults_.size() >= MAX_RESULT_QUEUE) {
        FetchResult& dropped = fetchResults_.front();
        TileKey droppedKey = dropped.key;
        {
            std::lock_guard<std::mutex> tlock(trackingMutex_);
            pendingFetches_.erase(droppedKey);
        }
        {
            std::lock_guard<std::mutex> dlock(droppedKeysMutex_);
            droppedKeys_.push(droppedKey);
        }
        fetchResults_.pop();
        ++droppedFetchResults_;
    }
    fetchResults_.push(std::move(result));
}

void TileScheduler::OnDecodeComplete(DecodeResult result) {
    std::lock_guard<std::mutex> lock(decodeResultsMutex_);
    
    // Drop oldest if queue is full (backpressure)
    // CRITICAL: Track dropped key so Update() can mark tile as Failed
    if (decodeResults_.size() >= MAX_RESULT_QUEUE) {
        DecodeResult& dropped = decodeResults_.front();
        TileKey droppedKey = dropped.key;
        {
            std::lock_guard<std::mutex> tlock(trackingMutex_);
            pendingDecodes_.erase(droppedKey);
        }
        {
            std::lock_guard<std::mutex> dlock(droppedKeysMutex_);
            droppedKeys_.push(droppedKey);
        }
        decodeResults_.pop();
        ++droppedDecodeResults_;
    }
    decodeResults_.push(std::move(result));
}

int TileScheduler::GetPendingFetches() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(trackingMutex_));
    return static_cast<int>(pendingFetches_.size());
}

int TileScheduler::GetPendingDecodes() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(trackingMutex_));
    return static_cast<int>(pendingDecodes_.size());
}

int TileScheduler::GetActiveFetches() const {
    return fetcher_ ? fetcher_->GetActiveCount() : 0;
}

} // namespace globe
