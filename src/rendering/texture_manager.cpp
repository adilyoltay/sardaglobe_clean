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

uint32_t TextureManager::GetLoadingTexture() {
    if (loadingTexture_ == 0) {
        loadingTexture_ = CreateLoadingTexture();
    }
    return loadingTexture_;
}

uint32_t TextureManager::CreateTexture(const uint8_t* pixels, int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    GLint prevAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    ++textureCount_;
    return texture;
}

void TextureManager::QueueUpload(Tile& tile) {
    if (tile.pixels.empty() || tile.pixelWidth == 0 || tile.pixelHeight == 0) {
        return;
    }
    UploadJob job;
    job.key = tile.key;
    job.priority = tile.requestPriority;
    job.score = tile.importance;
    job.sequence = uploadSequence_++;
    uploadQueue_.push(job);
}

int TextureManager::ProcessUploads(std::unordered_map<TileKey, Tile>& tiles, double budgetMs) {
    double startTime = glfwGetTime() * 1000.0;
    int uploadCount = 0;
    
    // CRITICAL FIX: Also enforce maxUploadsPerFrame from config
    const int maxUploads = config_.maxUploadsPerFrame;

    GLint prevAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
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
        
        UploadJob job = uploadQueue_.top();
        uploadQueue_.pop();
        TileKey key = job.key;
        
        auto it = tiles.find(key);
        if (it == tiles.end()) continue;
        
        Tile& tile = it->second;
        if (tile.pixels.empty()) continue;
        
        bool reuseTexture = tile.ownsTexture &&
                            tile.textureId != 0 &&
                            tile.texWidth == tile.pixelWidth &&
                            tile.texHeight == tile.pixelHeight;
        
        if (reuseTexture) {
            glBindTexture(GL_TEXTURE_2D, tile.textureId);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tile.pixelWidth, tile.pixelHeight,
                            GL_RGBA, GL_UNSIGNED_BYTE, tile.pixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            // Delete old texture if owned
            if (tile.ownsTexture && tile.textureId != 0) {
                glDeleteTextures(1, &tile.textureId);
                --textureCount_;
            }
            
            // Create new texture
            tile.textureId = CreateTexture(tile.pixels.data(), tile.pixelWidth, tile.pixelHeight);
            tile.ownsTexture = true;
            tile.texWidth = tile.pixelWidth;
            tile.texHeight = tile.pixelHeight;
        }
        
        // Use state machine for upload completion (handles fade reset)
        TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk);
        
        // Clear pixel data
        tile.ClearPixels();
        
        ++uploadCount;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);

    return uploadCount;
}

void TextureManager::DeleteTexture(uint32_t textureId) {
    if (textureId != 0 && textureId != loadingTexture_) {
        glDeleteTextures(1, &textureId);
        --textureCount_;
    }
}

// Pin API (GE-style cache policy)
void TextureManager::BeginPinEpoch() {
    ++pinEpoch_;
    if (pinEpoch_ == 0) {
        pinEpoch_ = 1;
    }
    pinnedCount_ = 0;
}

void TextureManager::PinTile(Tile& tile) {
    if (tile.pinnedEpoch != pinEpoch_) {
        tile.pinnedEpoch = pinEpoch_;
        ++pinnedCount_;
    }
}

bool TextureManager::IsPinned(const Tile& tile) const {
    return tile.pinnedEpoch == pinEpoch_;
}

void TextureManager::EvictIfNeeded(std::unordered_map<TileKey, Tile>& tiles, int maxTiles) {
    lastEvictedCount_ = 0;
    
    if (static_cast<int>(tiles.size()) <= maxTiles) {
        return;
    }
    
    // Build list of eviction candidates and count actual pinned tiles
    std::vector<std::pair<double, TileKey>> candidates;
    int actualPinnedCount = 0;
    
    for (const auto& [key, tile] : tiles) {
        if (tile.state == TileState::Ready && tile.ownsTexture) {
            if (IsPinned(tile)) {
                ++actualPinnedCount;
            } else {
                // Unpinned tile - candidate for eviction
                candidates.emplace_back(tile.lastAccessTime, key);
            }
        }
    }

    if (candidates.empty()) {
        return;
    }
    
    // Evict oldest unpinned tiles
    // When pinned >= maxTiles, unpinnedTarget clamps to 0 → evict ALL unpinned
    int unpinnedTarget = std::max(0, maxTiles - actualPinnedCount);
    int toEvict = std::max(0, static_cast<int>(candidates.size()) - unpinnedTarget);
    if (toEvict <= 0) {
        return;
    }

    if (toEvict < static_cast<int>(candidates.size())) {
        std::nth_element(
            candidates.begin(),
            candidates.begin() + toEvict,
            candidates.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; }
        );
    }

    const int maxEvicts = std::max(1, config_.maxEvictsPerFrame);
    const double budgetMs = config_.evictBudgetMs;
    double startMs = glfwGetTime() * 1000.0;

    int evicted = 0;
    for (int i = 0; i < toEvict && i < static_cast<int>(candidates.size()); ++i) {
        if (evicted >= maxEvicts) break;
        if (budgetMs > 0.0) {
            double elapsed = glfwGetTime() * 1000.0 - startMs;
            if (elapsed >= budgetMs && evicted > 0) {
                break;
            }
        }

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
            if (tile.ebo != 0 && tile.ownsEBO) {
                glDeleteBuffers(1, &tile.ebo);
            }
            
            tiles.erase(it);
            ++evicted;
        }
    }

    lastEvictedCount_ = evicted;
}

} // namespace globe
