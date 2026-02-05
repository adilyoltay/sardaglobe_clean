#pragma once

#include "download_types.h"
#include <curl/curl.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace globe {

// Interface for tile fetching
class ITileFetcher {
public:
    virtual ~ITileFetcher() = default;
    virtual void Fetch(FetchRequest request) = 0;
    virtual void Cancel(const TileKey& key) = 0;
    virtual void Shutdown() = 0;
};

// HTTP tile fetcher with worker pool
class TileFetcher : public ITileFetcher {
public:
    using ResultCallback = std::function<void(FetchResult)>;
    
    explicit TileFetcher(int numWorkers = 8);
    ~TileFetcher() override;
    
    // Set callback for completed fetches
    void SetResultCallback(ResultCallback callback);
    
    // ITileFetcher interface
    void Fetch(FetchRequest request) override;
    void Cancel(const TileKey& key) override;
    void Shutdown() override;
    
    // Stats
    int GetPendingCount() const;
    int GetActiveCount() const;
    uint64_t GetFetchCount() const;
    uint64_t GetTotalFetchTimeUs() const;

private:
    void WorkerLoop();
    bool DoFetch(const FetchRequest& request, FetchResult& result);
    static int ProgressCallback(void* userp, curl_off_t dltotal, curl_off_t dlnow,
                                curl_off_t ultotal, curl_off_t ulnow);
    
    std::priority_queue<FetchRequest, std::vector<FetchRequest>, FetchRequestCompare> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    std::atomic<int> activeCount_{0};

    // Timing stats (microseconds)
    std::atomic<uint64_t> fetchCount_{0};
    std::atomic<uint64_t> totalFetchTimeUs_{0};
    
    ResultCallback resultCallback_;
    std::mutex callbackMutex_;
    
    // Cancellation
    std::unordered_set<TileKey> cancelled_;
    std::mutex cancelMutex_;
};

} // namespace globe
