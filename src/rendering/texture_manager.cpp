#include "texture_manager.h"
#include "../scheduling/tile_state_machine.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>

namespace globe {

TextureManager::TextureManager(const Config& config)
    : config_(config) {
    loadingTexture_ = CreateLoadingTexture();
}

TextureManager::~TextureManager() {
    if (loadingTexture_ != 0) {
        glDeleteTextures(1, &loadingTexture_);
    }
}

uint32_t TextureManager::CreateLoadingTexture() {
    // Create a simple gray checkerboard pattern
    constexpr int size = 64;
    std::vector<uint8_t> pixels(size * size * 4);
    
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int idx = (y * size + x) * 4;
            uint8_t color = ((x / 8 + y / 8) % 2 == 0) ? 60 : 40;
            pixels[idx + 0] = color;
            pixels[idx + 1] = color;
            pixels[idx + 2] = color;
            pixels[idx + 3] = 255;
        }
    }
    
    return CreateTexture(pixels.data(), size, size);
}

uint32_t TextureManager::CreateTexture(const uint8_t* pixels, int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    ++textureCount_;
    return texture;
}

void TextureManager::QueueUpload(Tile& tile) {
    if (tile.pixels.empty() || tile.pixelWidth == 0 || tile.pixelHeight == 0) {
        return;
    }
    uploadQueue_.push(tile.key);
}

int TextureManager::ProcessUploads(std::unordered_map<TileKey, Tile>& tiles, double budgetMs) {
    double startTime = glfwGetTime() * 1000.0;
    int uploadCount = 0;
    
    // CRITICAL FIX: Also enforce maxUploadsPerFrame from config
    const int maxUploads = config_.maxUploadsPerFrame;
    
    while (!uploadQueue_.empty()) {
        // Check upload count limit (prevents frame hitch during heavy decode)
        if (uploadCount >= maxUploads) {
            break;  // Max uploads reached
        }
        
        // Check time budget
        double elapsed = glfwGetTime() * 1000.0 - startTime;
        if (elapsed >= budgetMs && uploadCount > 0) {
            break;  // Budget exhausted
        }
        
        TileKey key = uploadQueue_.front();
        uploadQueue_.pop();
        
        auto it = tiles.find(key);
        if (it == tiles.end()) continue;
        
        Tile& tile = it->second;
        if (tile.pixels.empty()) continue;
        
        // Delete old texture if owned
        if (tile.ownsTexture && tile.textureId != 0) {
            glDeleteTextures(1, &tile.textureId);
            --textureCount_;
        }
        
        // Create new texture
        tile.textureId = CreateTexture(tile.pixels.data(), tile.pixelWidth, tile.pixelHeight);
        tile.ownsTexture = true;
        
        // Use state machine for upload completion (handles fade reset)
        TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk);
        
        // Clear pixel data
        tile.ClearPixels();
        
        ++uploadCount;
    }
    
    return uploadCount;
}

void TextureManager::DeleteTexture(uint32_t textureId) {
    if (textureId != 0 && textureId != loadingTexture_) {
        glDeleteTextures(1, &textureId);
        --textureCount_;
    }
}

void TextureManager::EvictIfNeeded(std::unordered_map<TileKey, Tile>& tiles, int maxTiles) {
    if (static_cast<int>(tiles.size()) <= maxTiles) {
        return;
    }
    
    // Build list of eviction candidates (ready tiles sorted by last access)
    std::vector<std::pair<double, TileKey>> candidates;
    for (const auto& [key, tile] : tiles) {
        if (tile.state == TileState::Ready && tile.ownsTexture) {
            candidates.emplace_back(tile.lastAccessTime, key);
        }
    }
    
    // Sort by access time (oldest first)
    std::sort(candidates.begin(), candidates.end());
    
    // Evict oldest tiles
    int toEvict = static_cast<int>(tiles.size()) - maxTiles;
    for (int i = 0; i < toEvict && i < static_cast<int>(candidates.size()); ++i) {
        const TileKey& key = candidates[i].second;
        auto it = tiles.find(key);
        if (it != tiles.end()) {
            Tile& tile = it->second;
            
            // Delete texture
            if (tile.ownsTexture && tile.textureId != 0) {
                DeleteTexture(tile.textureId);
            }
            
            // Delete mesh (VAO/VBO/EBO) - CRITICAL: Prevents memory leak
            if (tile.vao != 0) {
                glDeleteVertexArrays(1, &tile.vao);
            }
            if (tile.vbo != 0) {
                glDeleteBuffers(1, &tile.vbo);
            }
            if (tile.ebo != 0) {
                glDeleteBuffers(1, &tile.ebo);
            }
            
            tiles.erase(it);
        }
    }
}

} // namespace globe
