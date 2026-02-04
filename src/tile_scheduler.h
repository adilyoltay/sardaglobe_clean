#pragma once

#include "tile.h"
#include "tile_key.h"
#include <functional>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <cmath>

// Unique key for scheduling (TileKey + Layer Context)
struct SchedulerKey {
  TileKey tileKey;
  std::string layerId;
  bool isVector = false;

  bool operator==(const SchedulerKey& other) const {
    return tileKey == other.tileKey && layerId == other.layerId && isVector == other.isVector;
  }

  struct Hash {
    size_t operator()(const SchedulerKey& k) const {
      size_t h1 = TileKey::Hash{}(k.tileKey);
      size_t h2 = std::hash<std::string>{}(k.layerId);
      size_t h3 = std::hash<bool>{}(k.isVector);
      return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
  };
};

// Task parameters for fetching
struct TaskParams {
  std::string urlTemplate;
  std::string layerId;
  bool isVector = false;
  float priorityScore = 0.0f;  // Google Earth style: Distance + Viewport Overlap + Importance
};

// Interface for fetching data (e.g. HTTP)
class ITileFetcher {
public:
  virtual ~ITileFetcher() = default;
  // Start fetching a tile. Should call Scheduler::OnFetchComplete when done.
  virtual void Fetch(const SchedulerKey& key, const TaskParams& params, int priority) = 0;
  virtual void Cancel(const SchedulerKey& key) = 0;
};

// Interface for decoding data (e.g. PNG/JPG to RGBA)
class ITileDecoder {
public:
  virtual ~ITileDecoder() = default;
  // Start decoding. Should call Scheduler::OnDecodeComplete when done.
  virtual void Decode(const SchedulerKey& key, std::vector<unsigned char> data) = 0;
};

// Scheduler for tile lifecycle
class TileScheduler {
public:
  TileScheduler(ITileFetcher* fetcher, ITileDecoder* decoder = nullptr);
  ~TileScheduler();
  
  // Configure limits
  void SetMaxActiveFetches(int max);
  void SetMaxActiveDecodes(int max);
  
  // Request a tile load
  void Schedule(Tile* tile, const TaskParams& params);
  
  // Cancel a tile load (if not yet complete)
  void Cancel(Tile* tile);
  
  // Notify that a fetch completed (called by Fetcher)
  // data can be empty if failed
  void OnFetchComplete(const SchedulerKey& key, std::vector<unsigned char> data, bool success);
  
  // Notify that decode completed (called by Decoder)
  // pixels contains raw RGBA bytes
  void OnDecodeComplete(const SchedulerKey& key, std::vector<unsigned char> pixels, int width, int height, bool success);
  
  // Main thread update (process uploads, etc.)
  void Update(std::function<Tile*(const SchedulerKey&)> tileResolver, double currentTime);
  
  // Metrics
  int GetPendingCount() const;
  int GetActiveFetches() const;
  int GetActiveDecodes() const;
  
private:
  ITileFetcher* fetcher_;
  ITileDecoder* decoder_;
  
  int maxActiveFetches_ = 64;   // Increased for faster loading
  int maxActiveDecodes_ = 32;   // Increased for faster decoding
  int maxPendingDecodes_ = 256; // P1 Fix: Max pending decode queue size to prevent memory bloat
  int activeFetches_ = 0;
  int activeDecodes_ = 0;
  
  struct Job {
    SchedulerKey key;
    TaskParams params;
    int priority; // Lower is better (0=URGENT)
    
    // Priority queue orders by max value, so we invert for min-priority first
    bool operator<(const Job& other) const {
      if (priority != other.priority) return priority > other.priority;
      
      // Always prefer Low Z (Parents/Visible Leaves) first to ensure coverage.
      // PriorityQueue pops the "largest" element.
      // We want Low Z to be popped first, so Low Z must be considered "larger".
      // If a.z=22, b.z=2. We want b to be larger.
      // a.z > b.z (22 > 2) is True. So a < b. b is larger.
      return key.tileKey.level > other.key.tileKey.level; 
    }
  };
  
  mutable std::mutex queueMutex_;
  std::priority_queue<Job> scheduledQueue_;
  std::unordered_set<SchedulerKey, SchedulerKey::Hash> pendingKeys_; // All keys in flight (any state)
  
  // Callbacks/Results queue to main thread
  struct Result {
    SchedulerKey key;
    bool success;
    std::vector<unsigned char> data; // For fetch result or decode pixels
    int width = 0;
    int height = 0;
    bool isDecodeResult = false;
  };
  
  std::vector<Result> results_; // Results to process in Update
  std::queue<Result> pendingDecodes_; // Phase 5: Backlog for decode limits
  mutable std::mutex resultsMutex_;
  
  // Helper to safely transition state
  void TransitionState(Tile* tile, TileLoadState newState);
};
