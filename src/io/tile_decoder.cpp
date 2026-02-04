#include "tile_decoder.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
        queue_.push(std::move(request));
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
    return static_cast<int>(queue_.size());
}

void TileDecoder::WorkerLoop() {
    while (running_) {
        DecodeRequest request;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !queue_.empty();
            });
            
            if (!running_) break;
            if (queue_.empty()) continue;
            
            request = std::move(queue_.front());
            queue_.pop();
        }
        
        DecodeResult result;
        result.key = request.key;
        result.success = DoDecode(request, result);
        
        // Invoke callback
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (resultCallback_) {
            resultCallback_(std::move(result));
        }
    }
}

bool TileDecoder::DoDecode(const DecodeRequest& request, DecodeResult& result) {
    if (request.data.empty()) {
        return false;
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
