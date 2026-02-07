#pragma once

#include "download_types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <queue>
#include <cstdint>

namespace globe {

// Interface for tile decoding
class ITileDecoder {
public:
    virtual ~ITileDecoder() = default;
    virtual void Decode(DecodeRequest request) = 0;
    virtual void Shutdown() = 0;
};

// Comparison for decode priority (higher priority first, then higher score)
struct DecodeRequestCompare {
    bool operator()(const DecodeRequest& a, const DecodeRequest& b) const {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.score < b.score;
    }
};

// Image decoder with worker thread
class TileDecoder : public ITileDecoder {
public:
    using ResultCallback = std::function<void(DecodeResult)>;
    
    explicit TileDecoder(int numWorkers = 4);
    ~TileDecoder() override;
    
    // Set callback for completed decodes
    void SetResultCallback(ResultCallback callback);
    
    // ITileDecoder interface
    void Decode(DecodeRequest request) override;
    void Shutdown() override;
    
    // Stats
    int GetPendingCount() const;
    uint64_t GetDecodeCount() const;
    uint64_t GetTotalDecodeTimeUs() const;
    uint64_t GetDecodedBlobHits() const;

private:
    void WorkerLoop();
    bool DoDecode(const DecodeRequest& request, DecodeResult& result);
    
    std::priority_queue<DecodeRequest, std::vector<DecodeRequest>, DecodeRequestCompare> urgentQueue_;
    std::priority_queue<DecodeRequest, std::vector<DecodeRequest>, DecodeRequestCompare> normalQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    int urgentProcessed_ = 0;
    static constexpr int URGENT_BATCH_SIZE = 4;
    
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};

    // Timing stats (microseconds)
    std::atomic<uint64_t> decodeCount_{0};
    std::atomic<uint64_t> totalDecodeTimeUs_{0};
    std::atomic<uint64_t> decodedBlobHits_{0};
    
    ResultCallback resultCallback_;
    std::mutex callbackMutex_;
};

} // namespace globe
