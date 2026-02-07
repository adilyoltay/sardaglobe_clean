#include "tile_decoder.h"
#include "decoded_tile_blob.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <chrono>

namespace globe {

TileDecoder::TileDecoder(int numWorkers) {
    stbi_set_flip_vertically_on_load(true);
    
    workers_.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

TileDecoder::~TileDecoder() {
    Shutdown();
}

void TileDecoder::SetResultCallback(ResultCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    resultCallback_ = std::move(callback);
}

void TileDecoder::Decode(DecodeRequest request) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (request.priority == Priority::Urgent) {
            urgentQueue_.push(std::move(request));
        } else {
            normalQueue_.push(std::move(request));
        }
    }
    queueCv_.notify_one();
}

void TileDecoder::Shutdown() {
    running_ = false;
    queueCv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

int TileDecoder::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queueMutex_));
    return static_cast<int>(urgentQueue_.size() + normalQueue_.size());
}

uint64_t TileDecoder::GetDecodeCount() const {
    return decodeCount_.load();
}

uint64_t TileDecoder::GetTotalDecodeTimeUs() const {
    return totalDecodeTimeUs_.load();
}

uint64_t TileDecoder::GetDecodedBlobHits() const {
    return decodedBlobHits_.load();
}

void TileDecoder::WorkerLoop() {
    while (running_) {
        DecodeRequest request;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !urgentQueue_.empty() || !normalQueue_.empty();
            });
            
            if (!running_) break;
            if (urgentQueue_.empty() && normalQueue_.empty()) continue;

            // Fairness: process N urgent, then 1 normal if available
            if (urgentProcessed_ >= URGENT_BATCH_SIZE && !normalQueue_.empty()) {
                request = std::move(normalQueue_.top());
                normalQueue_.pop();
                urgentProcessed_ = 0;
            } else if (!urgentQueue_.empty()) {
                request = std::move(urgentQueue_.top());
                urgentQueue_.pop();
                ++urgentProcessed_;
            } else {
                request = std::move(normalQueue_.top());
                normalQueue_.pop();
                urgentProcessed_ = 0;
            }
        }
        
        auto start = std::chrono::high_resolution_clock::now();

        DecodeResult result;
        result.key = request.key;
        result.success = DoDecode(request, result);

        auto end = std::chrono::high_resolution_clock::now();
        uint64_t elapsedUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        decodeCount_.fetch_add(1);
        totalDecodeTimeUs_.fetch_add(elapsedUs);
        
        // Invoke callback
        ResultCallback callbackCopy;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            callbackCopy = resultCallback_;
        }
        if (callbackCopy) {
            callbackCopy(std::move(result));
        }
    }
}

bool TileDecoder::DoDecode(const DecodeRequest& request, DecodeResult& result) {
    if (request.data.empty()) {
        return false;
    }

    // Fast path: pre-decoded RGBA blob from memory cache (skip image codec cost).
    if (decoded_blob::Unpack(request.data, result.width, result.height, result.pixels)) {
        decodedBlobHits_.fetch_add(1);
        return true;
    }
    
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        request.data.data(),
        static_cast<int>(request.data.size()),
        &result.width,
        &result.height,
        &channels,
        4  // Force RGBA
    );
    
    if (!pixels) {
        return false;
    }
    
    size_t size = static_cast<size_t>(result.width) * result.height * 4;
    result.pixels.assign(pixels, pixels + size);
    stbi_image_free(pixels);
    
    return true;
}

} // namespace globe
