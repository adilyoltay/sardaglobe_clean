#include "texture_manager.h"

// Note: STB_IMAGE_IMPLEMENTATION is already defined in globe_engine.cpp
// We just include the header here for stbi_* function declarations
#include "stb_image.h"

// ============================================================================
// StbImageDeleter (defined outside namespace)
// ============================================================================

void StbImageDeleter::operator()(unsigned char* ptr) const {
    if (ptr) stbi_image_free(ptr);
}

namespace texture {

// ============================================================================
// Texture Creation Functions
// ============================================================================

GLuint CreateFallbackTexture(const glm::vec3& color) {
    GLuint tex = 0;
    unsigned char pixel[4] = {
        static_cast<unsigned char>(color.r * 255.0f),
        static_cast<unsigned char>(color.g * 255.0f),
        static_cast<unsigned char>(color.b * 255.0f),
        255
    };
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint CreateTextureFromMemory(const std::vector<unsigned char>& data) {
    int width = 0, height = 0, channels = 0;
    unsigned char* img = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &width, &height, &channels, 4);
    
    if (!img) {
        return 0;
    }
    
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    stbi_image_free(img);
    return tex;
}

GLuint CreateTextureFromRGBA(const unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) {
        return 0;
    }
    
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return tex;
}

GLuint CreateLoadingTexture() {
    return CreateFallbackTexture(glm::vec3(0.35f, 0.35f, 0.38f));
}

// ============================================================================
// Image Decoding Functions
// ============================================================================

bool DecodeImageRGBA(const std::vector<unsigned char>& data,
                     std::vector<unsigned char>& outPixels,
                     int& outWidth, int& outHeight) {
    if (data.empty()) return false;
    
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &outWidth, &outHeight, &channels, 4);
    
    if (!pixels) return false;
    
    size_t size = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4;
    outPixels.assign(pixels, pixels + size);
    stbi_image_free(pixels);
    
    return true;
}

void AnalyzeAlpha(const std::vector<unsigned char>& rgba,
                  bool& anyTransparent, bool& allTransparent) {
    anyTransparent = false;
    allTransparent = true;
    
    for (size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] < 255) {
            anyTransparent = true;
        }
        if (rgba[i] > 0) {
            allTransparent = false;
        }
        // Early exit if we've found both conditions
        if (anyTransparent && !allTransparent) {
            return;
        }
    }
}

// ============================================================================
// TextureManager Class
// ============================================================================

TextureManager::~TextureManager() {
    ReleaseAll();
}

void TextureManager::Initialize() {
    if (loadingTexture_ == 0) {
        loadingTexture_ = CreateLoadingTexture();
    }
}

GLuint TextureManager::CreateTracked(const std::vector<unsigned char>& data) {
    int width = 0, height = 0, channels = 0;
    unsigned char* img = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &width, &height, &channels, 4);
    
    if (!img) return 0;
    
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    stbi_image_free(img);
    
    if (tex) {
        std::lock_guard<std::mutex> lock(mutex_);
        TextureInfo info;
        info.width = width;
        info.height = height;
        info.bytes = static_cast<size_t>(width) * height * 4;
        trackedTextures_[tex] = info;
    }
    
    return tex;
}

GLuint TextureManager::CreateTrackedRGBA(const unsigned char* pixels, int width, int height) {
    GLuint tex = CreateTextureFromRGBA(pixels, width, height);
    
    if (tex) {
        std::lock_guard<std::mutex> lock(mutex_);
        TextureInfo info;
        info.width = width;
        info.height = height;
        info.bytes = static_cast<size_t>(width) * height * 4;
        trackedTextures_[tex] = info;
    }
    
    return tex;
}

void TextureManager::Release(GLuint texture) {
    if (texture == 0 || texture == loadingTexture_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = trackedTextures_.find(texture);
    if (it != trackedTextures_.end()) {
        glDeleteTextures(1, &texture);
        trackedTextures_.erase(it);
    }
}

void TextureManager::ReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& kv : trackedTextures_) {
        glDeleteTextures(1, &kv.first);
    }
    trackedTextures_.clear();
    
    if (loadingTexture_) {
        glDeleteTextures(1, &loadingTexture_);
        loadingTexture_ = 0;
    }
}

TextureManager::Stats TextureManager::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.totalTextures = trackedTextures_.size();
    for (const auto& kv : trackedTextures_) {
        stats.totalBytes += kv.second.bytes;
    }
    return stats;
}

} // namespace texture
