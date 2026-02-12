#include "tile_decoder.h"
#include "decoded_tile_blob.h"

#include <stb_image.h>
#include <chrono>

namespace globe {

namespace {

bool HasAnyTransparency(const std::vector<uint8_t>& rgba) {
    // Any non-opaque alpha indicates potential nodata/edge holes that may need ancestor underlay.
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] < 255) {
            return true;
        }
    }
    return false;
}

bool IsMostlyBlackOpaque(const std::vector<uint8_t>& rgba) {
    // Detect provider-side opaque black nodata tiles.
    // Three-pass detection:
    //   1) Per-pixel ratio: >=85% of opaque pixels are near-black (R,G,B <= 20)
    //      Threshold raised from 10→20 and ratio from 90%→85% to catch JPEG-compressed
    //      nodata tiles where DCT rounding lifts pure-black values into 1-18 range.
    //   2) Mean brightness fallback: average opaque RGB <= 8.0 (catches tiles with
    //      thin non-black borders or JPEG artifacts that drop the ratio)
    //   3) Max channel check: if the brightest pixel is <= 30, the tile is black
    //      regardless of ratio (catches uniform very-dark tiles).
    if (rgba.empty()) {
        return false;
    }
    std::size_t opaqueCount = 0;
    std::size_t blackCount = 0;
    uint64_t brightnessSum = 0;
    uint8_t maxChannel = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const uint8_t r = rgba[i + 0];
        const uint8_t g = rgba[i + 1];
        const uint8_t b = rgba[i + 2];
        const uint8_t a = rgba[i + 3];
        if (a >= 250) {
            ++opaqueCount;
            brightnessSum += r + g + b;
            maxChannel = std::max(maxChannel, std::max(r, std::max(g, b)));
            if (r <= 20 && g <= 20 && b <= 20) {
                ++blackCount;
            }
        }
    }
    if (opaqueCount < 1024) {
        return false;
    }
    // Pass 1: ratio check (85% of pixels are near-black)
    const double ratio = static_cast<double>(blackCount) / static_cast<double>(opaqueCount);
    if (ratio >= 0.85) {
        return true;
    }
    // Pass 2: mean brightness fallback (catches tiles with thin colored borders)
    const double meanBrightness = static_cast<double>(brightnessSum) / (3.0 * static_cast<double>(opaqueCount));
    if (meanBrightness <= 8.0) {
        return true;
    }
    // Pass 3: max channel check (entire tile is uniformly very dark)
    if (maxChannel <= 30) {
        return true;
    }
    return false;
}

} // namespace

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
        result.hasTransparency = HasAnyTransparency(result.pixels);
        result.mostlyBlackOpaque = IsMostlyBlackOpaque(result.pixels);
        decodedBlobHits_.fetch_add(1);
        return true;
    }
    
    int channels = 0;
    // Pin flip mode per worker thread to avoid cross-subsystem interference.
    stbi_set_flip_vertically_on_load_thread(1);
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
    result.hasTransparency = HasAnyTransparency(result.pixels);
    result.mostlyBlackOpaque = IsMostlyBlackOpaque(result.pixels);
    
    return true;
}

} // namespace globe
