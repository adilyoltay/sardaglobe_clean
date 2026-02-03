#pragma once

#include <glad/glad.h>
#include <vector>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <string>
#include <functional>
#include <atomic>

namespace earth {

// Texture upload request (CPU → GPU)
struct TextureUploadRequest {
    std::string key;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    bool generateMipmaps = true;
    bool compressed = true;
};

// Texture cache entry
struct TextureEntry {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    size_t sizeBytes = 0;
    uint32_t lastFrameUsed = 0;
    int accessCount = 0;
    bool pinned = false;
};

// High-performance texture manager
// - Batched GPU uploads (avoid pipeline stalls)
// - LRU cache with byte-based eviction
// - Async-friendly (upload queue for main thread)
class TileTextureManager {
public:
    struct Config {
        size_t maxCacheBytes = 512 * 1024 * 1024;  // 512 MB default
        int maxUploadsPerFrame = 8;                 // Limit uploads per frame
        bool useCompression = true;                 // GL_COMPRESSED_RGBA
        bool generateMipmaps = true;
        GLint minFilter = GL_LINEAR_MIPMAP_LINEAR;
        GLint magFilter = GL_LINEAR;
        GLint wrapS = GL_CLAMP_TO_EDGE;
        GLint wrapT = GL_CLAMP_TO_EDGE;
    };
    
    struct Stats {
        size_t currentBytes = 0;
        size_t textureCount = 0;
        size_t uploadsThisFrame = 0;
        size_t evictionsThisFrame = 0;
        size_t cacheHits = 0;
        size_t cacheMisses = 0;
    };
    
    TileTextureManager();
    explicit TileTextureManager(const Config& config);
    ~TileTextureManager();
    
    // Queue texture for upload (thread-safe)
    void QueueUpload(const TextureUploadRequest& request);
    
    // Process upload queue (call from main/GL thread)
    // Returns number of textures uploaded
    int ProcessUploads(uint32_t currentFrame);
    
    // Get texture (returns 0 if not loaded)
    GLuint GetTexture(const std::string& key);
    
    // Check if texture is ready
    bool IsReady(const std::string& key) const;
    
    // Touch texture (update LRU)
    void Touch(const std::string& key, uint32_t currentFrame);
    
    // Pin/unpin texture (prevent eviction)
    void Pin(const std::string& key);
    void Unpin(const std::string& key);
    
    // Manual eviction
    void Evict(const std::string& key);
    void EvictOldest(size_t targetBytes);
    void Clear();
    
    // Create fallback textures
    GLuint CreateSolidColor(float r, float g, float b, float a = 1.0f);
    GLuint GetLoadingTexture();
    GLuint GetErrorTexture();
    
    // Stats
    const Stats& GetStats() const { return stats_; }
    void ResetFrameStats();
    
    // Configuration
    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }
    
private:
    Config config_;
    Stats stats_;
    
    std::unordered_map<std::string, TextureEntry> cache_;
    mutable std::mutex cacheMutex_;
    
    std::queue<TextureUploadRequest> uploadQueue_;
    mutable std::mutex uploadMutex_;
    
    GLuint loadingTexture_ = 0;
    GLuint errorTexture_ = 0;
    
    // Internal helpers
    GLuint CreateTexture(const unsigned char* pixels, int width, int height,
                         bool compressed, bool mipmaps);
    void DeleteTexture(GLuint& texture);
    size_t EstimateTextureBytes(int width, int height, bool mipmaps) const;
    void EnforceMemoryLimit();
};

// Async image decoder (runs on worker threads)
class AsyncImageDecoder {
public:
    struct DecodeResult {
        std::string key;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        bool success = false;
        bool isEmpty = false;  // Transparent/empty tile
    };
    
    using DecodeCallback = std::function<void(const DecodeResult&)>;
    
    // Decode image data (PNG/JPG) to RGBA pixels
    static DecodeResult Decode(const std::string& key,
                               const std::vector<unsigned char>& data);
    
    // Check if image is empty/transparent
    static bool IsEmptyImage(const std::vector<unsigned char>& pixels,
                             int width, int height);
};

} // namespace earth
