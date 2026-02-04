#pragma once

#include "tile_scheduler.h"
#include "download_types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <functional>

// ============================================================================
// GLOBE TILE FETCHER
// Adapter that bridges ITileFetcher interface to legacy DownloadQueue system.
// Extracted from globe_engine.cpp for better modularity.
// ============================================================================
class GlobeTileFetcher : public ITileFetcher {
public:
    GlobeTileFetcher(
        std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue,
        std::mutex& mutex,
        std::condition_variable& cv,
        const std::string& urlTemplate,
        TileScheduler* scheduler,
        std::mutex& cancelMutex,
        std::unordered_set<SchedulerKey, SchedulerKey::Hash>& cancelledKeys);
    
    ~GlobeTileFetcher() override = default;
    
    // ITileFetcher interface
    void Fetch(const SchedulerKey& key, const TaskParams& params, int priority) override;
    void Cancel(const SchedulerKey& key) override;
    
    // Configuration
    void SetUrlTemplate(const std::string& url);
    void SetScheduler(TileScheduler* scheduler);
    
    // Cancellation helpers
    bool IsCancelled(const SchedulerKey& key);
    void ClearCancelled(const SchedulerKey& key);

private:
    std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue_;
    std::mutex& mutex_;
    std::condition_variable& cv_;
    std::string urlTemplate_;
    TileScheduler* scheduler_;
    std::mutex& sharedCancelMutex_;
    std::unordered_set<SchedulerKey, SchedulerKey::Hash>& sharedCancelledKeys_;
};
