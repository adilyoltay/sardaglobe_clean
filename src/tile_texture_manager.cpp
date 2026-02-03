#include "tile_texture_manager.h"
#include <algorithm>
#include <cstring>

// stb_image declarations (implementation in globe_engine.cpp)
extern "C" {
    unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len,
                                          int* x, int* y, int* comp, int req_comp);
    void stbi_image_free(void* retval_from_stbi_load);
}

namespace earth {

// ============================================================================
// TILE TEXTURE MANAGER
// ============================================================================

TileTextureManager::TileTextureManager() : config_() {}

TileTextureManager::TileTextureManager(const Config& config) : config_(config) {}

TileTextureManager::~TileTextureManager() {
    Clear();
    if (loadingTexture_) glDeleteTextures(1, &loadingTexture_);
    if (errorTexture_) glDeleteTextures(1, &errorTexture_);
}

void TileTextureManager::QueueUpload(const TextureUploadRequest& request) {
    std::lock_guard<std::mutex> lock(uploadMutex_);
    uploadQueue_.push(request);
}

int TileTextureManager::ProcessUploads(uint32_t currentFrame) {
    stats_.uploadsThisFrame = 0;
    
    std::vector<TextureUploadRequest> batch;
    {
        std::lock_guard<std::mutex> lock(uploadMutex_);
        while (!uploadQueue_.empty() && 
               static_cast<int>(batch.size()) < config_.maxUploadsPerFrame) {
            batch.push_back(std::move(uploadQueue_.front()));
            uploadQueue_.pop();
        }
    }
    
    for (auto& req : batch) {
        // Check memory limit before upload
        EnforceMemoryLimit();
        
        GLuint tex = CreateTexture(
            req.pixels.data(),
            req.width,
            req.height,
            req.compressed && config_.useCompression,
            req.generateMipmaps && config_.generateMipmaps
        );
        
        if (tex != 0) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            
            // Remove old texture if exists
            auto it = cache_.find(req.key);
            if (it != cache_.end()) {
                stats_.currentBytes -= it->second.sizeBytes;
                glDeleteTextures(1, &it->second.texture);
            }
            
            TextureEntry entry;
            entry.texture = tex;
            entry.width = req.width;
            entry.height = req.height;
            entry.sizeBytes = EstimateTextureBytes(req.width, req.height, 
                                                    req.generateMipmaps && config_.generateMipmaps);
            entry.lastFrameUsed = currentFrame;
            entry.accessCount = 1;
            
            cache_[req.key] = entry;
            stats_.currentBytes += entry.sizeBytes;
            stats_.textureCount = cache_.size();
            stats_.uploadsThisFrame++;
        }
    }
    
    return static_cast<int>(stats_.uploadsThisFrame);
}

GLuint TileTextureManager::GetTexture(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        stats_.cacheHits++;
        return it->second.texture;
    }
    stats_.cacheMisses++;
    return 0;
}

bool TileTextureManager::IsReady(const std::string& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.find(key) != cache_.end();
}

void TileTextureManager::Touch(const std::string& key, uint32_t currentFrame) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.lastFrameUsed = currentFrame;
        it->second.accessCount++;
    }
}

void TileTextureManager::Pin(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.pinned = true;
    }
}

void TileTextureManager::Unpin(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.pinned = false;
    }
}

void TileTextureManager::Evict(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && !it->second.pinned) {
        stats_.currentBytes -= it->second.sizeBytes;
        glDeleteTextures(1, &it->second.texture);
        cache_.erase(it);
        stats_.textureCount = cache_.size();
        stats_.evictionsThisFrame++;
    }
}

void TileTextureManager::EvictOldest(size_t targetBytes) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    while (stats_.currentBytes > targetBytes && !cache_.empty()) {
        // Find oldest unpinned entry
        auto oldest = cache_.end();
        uint32_t oldestFrame = UINT32_MAX;
        
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (!it->second.pinned && it->second.lastFrameUsed < oldestFrame) {
                oldestFrame = it->second.lastFrameUsed;
                oldest = it;
            }
        }
        
        if (oldest == cache_.end()) break; // All pinned
        
        stats_.currentBytes -= oldest->second.sizeBytes;
        glDeleteTextures(1, &oldest->second.texture);
        cache_.erase(oldest);
        stats_.evictionsThisFrame++;
    }
    
    stats_.textureCount = cache_.size();
}

void TileTextureManager::Clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto& pair : cache_) {
        if (pair.second.texture) {
            glDeleteTextures(1, &pair.second.texture);
        }
    }
    cache_.clear();
    stats_.currentBytes = 0;
    stats_.textureCount = 0;
}

GLuint TileTextureManager::CreateSolidColor(float r, float g, float b, float a) {
    unsigned char pixel[4] = {
        static_cast<unsigned char>(r * 255.0f),
        static_cast<unsigned char>(g * 255.0f),
        static_cast<unsigned char>(b * 255.0f),
        static_cast<unsigned char>(a * 255.0f)
    };
    return CreateTexture(pixel, 1, 1, false, false);
}

GLuint TileTextureManager::GetLoadingTexture() {
    if (loadingTexture_ == 0) {
        loadingTexture_ = CreateSolidColor(0.35f, 0.35f, 0.38f);
    }
    return loadingTexture_;
}

GLuint TileTextureManager::GetErrorTexture() {
    if (errorTexture_ == 0) {
        errorTexture_ = CreateSolidColor(0.8f, 0.2f, 0.2f);
    }
    return errorTexture_;
}

void TileTextureManager::ResetFrameStats() {
    stats_.uploadsThisFrame = 0;
    stats_.evictionsThisFrame = 0;
}

GLuint TileTextureManager::CreateTexture(const unsigned char* pixels, int width, int height,
                                          bool compressed, bool mipmaps) {
    if (!pixels || width <= 0 || height <= 0) return 0;
    
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config_.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config_.wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 
                    mipmaps ? config_.minFilter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, config_.magFilter);
    
    GLenum internalFormat = compressed ? GL_COMPRESSED_RGBA : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void TileTextureManager::DeleteTexture(GLuint& texture) {
    if (texture) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

size_t TileTextureManager::EstimateTextureBytes(int width, int height, bool mipmaps) const {
    size_t baseBytes = static_cast<size_t>(width) * height * 4; // RGBA
    if (mipmaps) {
        // Mipmaps add ~33% more
        return static_cast<size_t>(baseBytes * 1.33);
    }
    return baseBytes;
}

void TileTextureManager::EnforceMemoryLimit() {
    if (stats_.currentBytes > config_.maxCacheBytes) {
        // Evict until under 90% of limit
        EvictOldest(static_cast<size_t>(config_.maxCacheBytes * 0.9));
    }
}

// ============================================================================
// ASYNC IMAGE DECODER
// ============================================================================

AsyncImageDecoder::DecodeResult AsyncImageDecoder::Decode(
    const std::string& key,
    const std::vector<unsigned char>& data
) {
    DecodeResult result;
    result.key = key;
    
    if (data.empty()) {
        result.success = false;
        return result;
    }
    
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &result.width,
        &result.height,
        &channels,
        4 // Force RGBA
    );
    
    if (!pixels) {
        result.success = false;
        return result;
    }
    
    size_t pixelCount = static_cast<size_t>(result.width) * result.height * 4;
    result.pixels.assign(pixels, pixels + pixelCount);
    stbi_image_free(pixels);
    
    result.success = true;
    result.isEmpty = IsEmptyImage(result.pixels, result.width, result.height);
    
    return result;
}

bool AsyncImageDecoder::IsEmptyImage(const std::vector<unsigned char>& pixels,
                                      int width, int height) {
    if (pixels.empty()) return true;
    
    constexpr unsigned char kAlphaThreshold = 10;
    
    // Check if all pixels are transparent
    for (int i = 0; i < width * height; ++i) {
        if (pixels[i * 4 + 3] > kAlphaThreshold) {
            return false; // Found visible pixel
        }
    }
    
    return true;
}

} // namespace earth
