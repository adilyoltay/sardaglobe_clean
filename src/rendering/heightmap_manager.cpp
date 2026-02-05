#include "heightmap_manager.h"
#include <glad/glad.h>
#include <chrono>
#include <algorithm>

namespace globe {

HeightmapManager::~HeightmapManager() {
    Clear();
}

void HeightmapManager::QueueUpload(const TileKey& key, const DemGridData& demData, float heightScale) {
    if (!demData.valid || demData.heights.empty()) return;
    
    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(key) != cache_.end()) {
            return;  // Already uploaded
        }
    }
    
    // Prepare upload request
    HeightmapUploadRequest req;
    req.key = key;
    req.gridSize = demData.meshN;
    req.minHeight = static_cast<float>(demData.minHeight * 0.001 * heightScale);  // Convert to km with scale
    req.maxHeight = static_cast<float>(demData.maxHeight * 0.001 * heightScale);
    
    // Normalize heights to [0, 1]
    float range = req.maxHeight - req.minHeight;
    if (range < 0.0001f) range = 1.0f;  // Prevent division by zero
    
    req.normalizedHeights.resize(demData.heights.size());
    for (size_t i = 0; i < demData.heights.size(); ++i) {
        float heightKm = static_cast<float>(demData.heights[i] * 0.001 * heightScale);
        req.normalizedHeights[i] = (heightKm - req.minHeight) / range;
    }
    
    // Queue for upload (dedupe queued keys, and clear cancel marker on new request)
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (pendingKeys_.count(key) > 0) {
            return;  // Already queued for upload
        }
        canceledKeys_.erase(key);
        pendingKeys_.insert(key);
        uploadQueue_.push(std::move(req));
    }
}

int HeightmapManager::ProcessUploads(double budgetMs) {
    auto startTime = std::chrono::high_resolution_clock::now();
    int processed = 0;
    
    while (true) {
        // Check budget
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - startTime).count();
        if (elapsed >= budgetMs) break;
        
        // Get next request
        HeightmapUploadRequest req;
        bool canceled = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (uploadQueue_.empty()) break;
            req = std::move(uploadQueue_.front());
            uploadQueue_.pop();
            pendingKeys_.erase(req.key);  // Remove from pending set
            canceled = canceledKeys_.erase(req.key) > 0;
        }

        if (canceled) {
            continue;  // Tile was released/evicted before upload completed
        }
        
        // Create texture
        HeightmapTexture tex = CreateTexture(req);
        if (tex.valid) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            // Delete previous texture if exists (prevents leak on re-upload)
            auto it = cache_.find(req.key);
            if (it != cache_.end() && it->second.textureId != 0) {
                glDeleteTextures(1, &it->second.textureId);
            }
            cache_[req.key] = tex;
            processed++;
        }
    }
    
    return processed;
}

HeightmapTexture HeightmapManager::CreateTexture(const HeightmapUploadRequest& req) {
    HeightmapTexture tex;
    tex.width = req.gridSize;
    tex.height = req.gridSize;
    tex.minHeight = req.minHeight;
    tex.maxHeight = req.maxHeight;
    
    if (req.normalizedHeights.empty() || req.gridSize <= 0) {
        return tex;
    }
    
    // Create R16F texture
    glGenTextures(1, &tex.textureId);
    glBindTexture(GL_TEXTURE_2D, tex.textureId);
    
    // Upload as R16F (16-bit float, single channel)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F,
                 req.gridSize, req.gridSize, 0,
                 GL_RED, GL_FLOAT, req.normalizedHeights.data());
    
    // Bilinear filtering for smooth height interpolation
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Clamp to edge to prevent seam artifacts
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    tex.valid = true;
    return tex;
}

bool HeightmapManager::GetTexture(const TileKey& key, HeightmapTexture& out) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second.valid) {
        out = it->second;
        return true;
    }
    return false;
}

bool HeightmapManager::HasTexture(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    return it != cache_.end() && it->second.valid;
}

void HeightmapManager::Release(const TileKey& key) {
    // Cancel any queued/in-flight upload for this key.
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingKeys_.erase(key);
        canceledKeys_.insert(key);

        std::queue<HeightmapUploadRequest> filtered;
        while (!uploadQueue_.empty()) {
            HeightmapUploadRequest req = std::move(uploadQueue_.front());
            uploadQueue_.pop();
            if (req.key != key) {
                filtered.push(std::move(req));
            }
        }
        uploadQueue_ = std::move(filtered);
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        if (it->second.textureId != 0) {
            glDeleteTextures(1, &it->second.textureId);
        }
        cache_.erase(it);
    }
}

void HeightmapManager::Clear() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::queue<HeightmapUploadRequest> empty;
        uploadQueue_.swap(empty);
        pendingKeys_.clear();
        canceledKeys_.clear();
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto& [key, tex] : cache_) {
        if (tex.textureId != 0) {
            glDeleteTextures(1, &tex.textureId);
        }
    }
    cache_.clear();
}

int HeightmapManager::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return static_cast<int>(cache_.size());
}

int HeightmapManager::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<int>(uploadQueue_.size());
}

} // namespace globe
