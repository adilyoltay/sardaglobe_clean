#include "tile_fetcher.h"
#include <GLFW/glfw3.h>

GlobeTileFetcher::GlobeTileFetcher(
    std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& queue,
    std::mutex& mutex,
    std::condition_variable& cv,
    const std::string& urlTemplate,
    TileScheduler* scheduler,
    std::mutex& cancelMutex,
    std::unordered_set<SchedulerKey, SchedulerKey::Hash>& cancelledKeys)
    : queue_(queue)
    , mutex_(mutex)
    , cv_(cv)
    , urlTemplate_(urlTemplate)
    , scheduler_(scheduler)
    , sharedCancelMutex_(cancelMutex)
    , sharedCancelledKeys_(cancelledKeys) {
}

void GlobeTileFetcher::SetUrlTemplate(const std::string& url) {
    urlTemplate_ = url;
}

void GlobeTileFetcher::SetScheduler(TileScheduler* scheduler) {
    scheduler_ = scheduler;
}

void GlobeTileFetcher::Fetch(const SchedulerKey& key, const TaskParams& params, int priority) {
    DownloadJob job;
    job.urlTemplate = params.urlTemplate.empty() ? urlTemplate_ : params.urlTemplate;
    job.layerId = params.layerId;
    job.isVector = params.isVector;
    job.z = key.tileKey.level;
    job.x = key.tileKey.x;
    job.y = key.tileKey.y;
    job.priority = priority;
    job.priorityScore = params.priorityScore;
    job.queueTime = glfwGetTime();
    
    // Set callback to notify Scheduler
    TileScheduler* sched = scheduler_;
    job.callback = [sched, key](std::vector<unsigned char> data, bool success) {
        if (sched) {
            sched->OnFetchComplete(key, std::move(data), success);
        }
    };
    
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(job));
    cv_.notify_one();
}

void GlobeTileFetcher::Cancel(const SchedulerKey& key) {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    sharedCancelledKeys_.insert(key);
}

bool GlobeTileFetcher::IsCancelled(const SchedulerKey& key) {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    return sharedCancelledKeys_.count(key) > 0;
}

void GlobeTileFetcher::ClearCancelled(const SchedulerKey& key) {
    std::lock_guard<std::mutex> lock(sharedCancelMutex_);
    sharedCancelledKeys_.erase(key);
}
