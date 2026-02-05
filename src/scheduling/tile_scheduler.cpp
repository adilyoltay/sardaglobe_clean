#include "tile_scheduler.h"
#include "tile_state_machine.h"
#include <iostream>

namespace globe {

TileScheduler::TileScheduler(const Config& config) 
    : config_(config)
    , urlTemplate_(config.tileUrl) {
    
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
    fetchResults_.Close();
    decodeResults_.Close();
    if (fetcher_) fetcher_->Shutdown();
    if (decoder_) decoder_->Shutdown();
}

std::string TileScheduler::BuildUrl(const TileKey& key) const {
    return urlTemplate_.Build(key.level, key.x, key.y);
}

void TileScheduler::Request(const TileKey& key, Priority priority, float score) {
    // Check if already pending
    {
        std::lock_guard<std::mutex> lock(trackingMutex_);
        if (pendingFetches_.count(key) || pendingDecodes_.count(key)) {
            return;  // Already in progress
        }
        int inFlight = static_cast<int>(pendingFetches_.size()) + GetActiveFetches();
        if (inFlight >= config_.maxInFlightFetches && priority != Priority::Urgent) {
            return;  // Backpressure for non-urgent requests
        }
        pendingFetches_.insert(key);
    }
    
    // Queue fetch
    FetchRequest req;
    req.key = key;
    req.url = BuildUrl(key);
    req.priority = priority;
    req.score = score;
    if (cache_) {
        req.tryReadCache = [this](const TileKey& k, std::vector<uint8_t>& out) {
            return cache_->Read(k, config_.tileUrl, out);
        };
        req.writeCache = [this](const TileKey& k, const std::vector<uint8_t>& data) {
            cache_->Write(k, config_.tileUrl, data);
        };
    }
    
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
        FetchResult result;
        while (fetchResults_.TryPop(result)) {
            
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
            
            // Send to decoder
            {
                std::lock_guard<std::mutex> tlock(trackingMutex_);
                pendingDecodes_.insert(result.key);
            }
            
            DecodeRequest dreq;
            dreq.key = result.key;
            dreq.data = std::move(result.data);
            dreq.priority = result.priority;
            dreq.score = result.score;
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
        DecodeResult result;
        while (decodeResults_.TryPop(result)) {
            
            {
                std::lock_guard<std::mutex> tlock(trackingMutex_);
                pendingDecodes_.erase(result.key);
            }
            
            auto it = tiles.find(result.key);
            if (it == tiles.end()) continue;
            
            if (!result.success) {
                if (cache_) {
                    cache_->Remove(result.key, config_.tileUrl);
                }
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
    if (!fetchResults_.Push(std::move(result))) {
        // Queue closed during shutdown
        return;
    }
}

void TileScheduler::OnDecodeComplete(DecodeResult result) {
    if (!decodeResults_.Push(std::move(result))) {
        // Queue closed during shutdown
        return;
    }
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

TileScheduler::SchedulerStats TileScheduler::GetStats() const {
    SchedulerStats stats;
    stats.pendingFetches = GetPendingFetches();
    stats.pendingDecodes = GetPendingDecodes();
    stats.activeFetches = GetActiveFetches();
    stats.fetchResultQueue = fetchResults_.Size();
    stats.decodeResultQueue = decodeResults_.Size();
    stats.droppedFetchResults = droppedFetchResults_.load();
    stats.droppedDecodeResults = droppedDecodeResults_.load();
    if (fetcher_) {
        auto count = fetcher_->GetFetchCount();
        auto totalUs = fetcher_->GetTotalFetchTimeUs();
        stats.avgFetchMs = count > 0 ? (static_cast<double>(totalUs) / 1000.0) / count : 0.0;
    }
    if (decoder_) {
        auto count = decoder_->GetDecodeCount();
        auto totalUs = decoder_->GetTotalDecodeTimeUs();
        stats.avgDecodeMs = count > 0 ? (static_cast<double>(totalUs) / 1000.0) / count : 0.0;
    }
    return stats;
}

} // namespace globe
