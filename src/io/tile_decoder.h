#pragma once

#include "download_types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace globe {

// Interface for tile decoding
class ITileDecoder {
public:
    virtual ~ITileDecoder() = default;
    virtual void Decode(DecodeRequest request) = 0;
    virtual void Shutdown() = 0;
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

private:
    void WorkerLoop();
    bool DoDecode(const DecodeRequest& request, DecodeResult& result);
    
    std::queue<DecodeRequest> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    
    ResultCallback resultCallback_;
    std::mutex callbackMutex_;
};

} // namespace globe
