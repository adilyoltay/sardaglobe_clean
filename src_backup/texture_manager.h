#pragma once

// ============================================================================
// TextureManager - Centralized texture creation and management
// Extracted from globe_engine.cpp to reduce monolithic file size
// ============================================================================

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>

// Forward declaration for stb_image (implementation in cpp)
struct StbImageDeleter {
    void operator()(unsigned char* ptr) const;
};

namespace texture {

// ============================================================================
// Texture Creation Functions
// ============================================================================

// Create a 1x1 fallback texture with the given color
GLuint CreateFallbackTexture(const glm::vec3& color);

// Create texture from encoded image data (PNG/JPG) - decodes internally
GLuint CreateTextureFromMemory(const std::vector<unsigned char>& data);

// Create texture from raw RGBA pixel data
GLuint CreateTextureFromRGBA(const unsigned char* pixels, int width, int height);

// Create the default loading/placeholder texture
GLuint CreateLoadingTexture();

// ============================================================================
// Image Decoding Functions
// ============================================================================

// Decode image data to RGBA pixels
// Returns true on success, fills outPixels, outWidth, outHeight
bool DecodeImageRGBA(const std::vector<unsigned char>& data,
                     std::vector<unsigned char>& outPixels,
                     int& outWidth, int& outHeight);

// Analyze alpha channel of RGBA image
// anyTransparent: true if any pixel has alpha < 255
// allTransparent: true if all pixels have alpha == 0
void AnalyzeAlpha(const std::vector<unsigned char>& rgba,
                  bool& anyTransparent, bool& allTransparent);

// ============================================================================
// TextureManager Class - Optional pooling and tracking
// ============================================================================

class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();
    
    // Prevent copying
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    
    // Initialize with a loading texture
    void Initialize();
    
    // Get the shared loading texture
    GLuint GetLoadingTexture() const { return loadingTexture_; }
    
    // Create a texture and track it for cleanup
    GLuint CreateTracked(const std::vector<unsigned char>& data);
    GLuint CreateTrackedRGBA(const unsigned char* pixels, int width, int height);
    
    // Release a tracked texture
    void Release(GLuint texture);
    
    // Release all tracked textures
    void ReleaseAll();
    
    // Statistics
    struct Stats {
        size_t totalTextures = 0;
        size_t totalBytes = 0;
    };
    Stats GetStats() const;
    
private:
    GLuint loadingTexture_ = 0;
    
    struct TextureInfo {
        int width = 0;
        int height = 0;
        size_t bytes = 0;
    };
    
    std::unordered_map<GLuint, TextureInfo> trackedTextures_;
    mutable std::mutex mutex_;
};

} // namespace texture
