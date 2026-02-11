#pragma once

#include "download_types.h"
#include <curl/curl.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
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
    
    // basicAuthUserPwd: optional HTTP basic auth, format "user:password".
    explicit TileFetcher(int numWorkers = 8, std::string basicAuthUserPwd = {});
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

    struct PendingRank {
        Priority priority = Priority::Normal;
        double score = 0.0;
        uint64_t seq = 0;
    };
    // Best-known rank per key (enables priority/score upgrades without queue surgery).
    std::unordered_map<TileKey, PendingRank> bestRanks_;
    // Keys currently being fetched by a worker (prevents duplicate active transfers on upgrades).
    std::unordered_set<TileKey> inFlight_;
    uint64_t enqueueSeq_ = 0;
    
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

    // Optional basic-auth credentials ("user:password")
    std::string basicAuthUserPwd_;
};

} // namespace globe
