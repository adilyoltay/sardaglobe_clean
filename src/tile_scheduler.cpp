#include "tile_scheduler.h"
#include <algorithm>
#include <iostream>

// Helper to create key from Tile
SchedulerKey MakeSchedulerKey(const Tile* tile) {
    return { tile->key, tile->layerId, tile->isVector };
}

TileScheduler::TileScheduler(ITileFetcher* fetcher, ITileDecoder* decoder)
    : fetcher_(fetcher), decoder_(decoder) {}

TileScheduler::~TileScheduler() {}

void TileScheduler::SetMaxActiveFetches(int max) {
  std::lock_guard<std::mutex> lock(queueMutex_);
  maxActiveFetches_ = max;
}

void TileScheduler::SetMaxActiveDecodes(int max) {
  std::lock_guard<std::mutex> lock(queueMutex_);
  maxActiveDecodes_ = max;
}

void TileScheduler::Schedule(Tile* tile, const TaskParams& params) {
  if (!tile) return;
  
  // Update Tile context from params before scheduling
  tile->layerId = params.layerId;
  tile->isVector = params.isVector;
  
  std::lock_guard<std::mutex> lock(queueMutex_);
  
  bool isPromoting = (tile->loadState == TileLoadState::SCHEDULED);

  if (!isPromoting &&
      tile->loadState != TileLoadState::UNLOADED && 
      tile->loadState != TileLoadState::FAILED &&
      tile->loadState != TileLoadState::EVICTED) {
    return; // Already active
  }
  
  SchedulerKey key = MakeSchedulerKey(tile);
  
  if (pendingKeys_.count(key)) {
    if (!isPromoting) return; // Already pending
  } else {
    tile->loadState = TileLoadState::SCHEDULED;
    pendingKeys_.insert(key);
  }
  
  Job job;
  job.key = key;
  job.params = params;
  job.priority = static_cast<int>(tile->loadPriority);
  scheduledQueue_.push(job);
}

void TileScheduler::Cancel(Tile* tile) {
  if (!tile) return;
  
  std::lock_guard<std::mutex> lock(queueMutex_);
  SchedulerKey key = MakeSchedulerKey(tile);
  
  if (pendingKeys_.count(key)) {
    if (tile->loadState == TileLoadState::FETCHING && fetcher_) {
        fetcher_->Cancel(key);
    }
    pendingKeys_.erase(key);
  }
  
  // Reset state if it was loading
  if (tile->loadState == TileLoadState::SCHEDULED || 
      tile->loadState == TileLoadState::FETCHING || 
      tile->loadState == TileLoadState::DECODING) {
    tile->loadState = TileLoadState::UNLOADED;
  }
}

void TileScheduler::OnFetchComplete(const SchedulerKey& key, std::vector<unsigned char> data, bool success) {
  std::lock_guard<std::mutex> lock(resultsMutex_);
  Result r;
  r.key = key;
  r.success = success;
  r.data = std::move(data);
  r.isDecodeResult = false;
  results_.push_back(std::move(r));
}

void TileScheduler::OnDecodeComplete(const SchedulerKey& key, std::vector<unsigned char> pixels, int width, int height, bool success) {
  std::lock_guard<std::mutex> lock(resultsMutex_);
  Result r;
  r.key = key;
  r.success = success;
  r.data = std::move(pixels);
  r.width = width;
  r.height = height;
  r.isDecodeResult = true;
  results_.push_back(std::move(r));
}

void TileScheduler::Update(std::function<Tile*(const SchedulerKey&)> tileResolver, double currentTime) {
  // Process results
  std::vector<Result> localResults;
  {
    std::lock_guard<std::mutex> lock(resultsMutex_);
    localResults.swap(results_);
  }
  
  for (auto& res : localResults) {
    Tile* tile = tileResolver(res.key);
    
    if (res.isDecodeResult) {
      // Decode finished
      {
        std::lock_guard<std::mutex> lock(queueMutex_);
        activeDecodes_--;
        pendingKeys_.erase(res.key);
      }
      
      if (tile && tile->loadState == TileLoadState::DECODING) {
        if (res.success) {
          tile->loadState = TileLoadState::READY;
          tile->decodedData = std::move(res.data);
          tile->decodedWidth = res.width;
          tile->decodedHeight = res.height;
          tile->retryCount = 0; // Reset retry on success
        } else {
          tile->loadState = TileLoadState::FAILED;
          tile->retryCount++;
          tile->lastRetryTime = currentTime;
        }
      }
    } else {
      // Fetch finished
      {
        std::lock_guard<std::mutex> lock(queueMutex_);
        activeFetches_--;
      }
      
      if (tile && tile->loadState == TileLoadState::FETCHING) {
        if (res.success) {
          // Check if vector -> Skip decode
          if (res.key.isVector) {
             tile->loadState = TileLoadState::READY;
             tile->decodedData = std::move(res.data);
             tile->decodedWidth = 0; 
             tile->retryCount = 0;
             {
                 std::lock_guard<std::mutex> lock(queueMutex_);
                 pendingKeys_.erase(res.key);
             }
          } else if (decoder_) {
            // Queue for decode (Phase 5 fix: enforce limits)
            pendingDecodes_.push(std::move(res));
          } else {
            // No decoder, assume ready (or sync decode elsewhere)
            tile->loadState = TileLoadState::READY;
            tile->decodedData = std::move(res.data);
            tile->decodedWidth = 0; // RAW
            tile->retryCount = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                pendingKeys_.erase(res.key);
            }
          }
        } else {
          tile->loadState = TileLoadState::FAILED;
          tile->retryCount++;
          tile->lastRetryTime = currentTime;
          {
              std::lock_guard<std::mutex> lock(queueMutex_);
              pendingKeys_.erase(res.key);
          }
        }
      } else {
        // Tile was canceled or evicted, cleanup
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingKeys_.erase(res.key);
      }
    }
  }
  
  // Process pending decodes respecting limits
  while (!pendingDecodes_.empty()) {
      int active = 0;
      {
          std::lock_guard<std::mutex> lock(queueMutex_);
          active = activeDecodes_;
      }
      if (active >= maxActiveDecodes_) break;
      
      auto res = std::move(pendingDecodes_.front());
      pendingDecodes_.pop();
      
      Tile* tile = tileResolver(res.key);
      if (tile && tile->loadState == TileLoadState::FETCHING) {
          tile->loadState = TileLoadState::DECODING;
          {
              std::lock_guard<std::mutex> lock(queueMutex_);
              activeDecodes_++;
          }
          decoder_->Decode(res.key, std::move(res.data));
      } else {
          // Tile gone/cancelled, cleanup
          std::lock_guard<std::mutex> lock(queueMutex_);
          pendingKeys_.erase(res.key);
      }
  }
  
  // Schedule new jobs
  std::lock_guard<std::mutex> lock(queueMutex_);
  
  while (activeFetches_ < maxActiveFetches_ && !scheduledQueue_.empty()) {
    Job job = scheduledQueue_.top();
    scheduledQueue_.pop();
    
    // Check if still pending (might have been cancelled)
    if (pendingKeys_.find(job.key) == pendingKeys_.end()) {
      continue;
    }
    
    Tile* tile = tileResolver(job.key);
    if (!tile) {
      // Tile evicted while in queue, clear pending state so it can be re-scheduled
      pendingKeys_.erase(job.key);
      continue;
    }

    if (tile->loadState == TileLoadState::SCHEDULED) {
      tile->loadState = TileLoadState::FETCHING;
      activeFetches_++;
      if (fetcher_) {
        fetcher_->Fetch(job.key, job.params, job.priority);
      } else {
        // Mock success
        activeFetches_--;
        tile->loadState = TileLoadState::READY;
        pendingKeys_.erase(job.key);
      }
    }
  }
}

int TileScheduler::GetPendingCount() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return pendingKeys_.size();
}

int TileScheduler::GetActiveFetches() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return activeFetches_;
}

int TileScheduler::GetActiveDecodes() const {
  std::lock_guard<std::mutex> lock(queueMutex_);
  return activeDecodes_;
}
