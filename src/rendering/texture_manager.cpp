#include "texture_manager.h"
#include "../scheduling/tile_state_machine.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>

namespace globe {

namespace {

std::vector<uint8_t> BuildDilatedRgba(
    const std::vector<uint8_t>& src,
    int width,
    int height,
    int gutterPx
) {
    if (width <= 0 || height <= 0 || gutterPx <= 0) {
        return src;
    }

    const int paddedWidth = width + gutterPx * 2;
    const int paddedHeight = height + gutterPx * 2;
    std::vector<uint8_t> padded(static_cast<size_t>(paddedWidth * paddedHeight * 4), 0);

    auto dstPixel = [&](int x, int y) -> uint8_t* {
        return padded.data() + static_cast<size_t>((y * paddedWidth + x) * 4);
    };
    auto srcPixel = [&](int x, int y) -> const uint8_t* {
        return src.data() + static_cast<size_t>((y * width + x) * 4);
    };

    // Copy content block.
    for (int y = 0; y < height; ++y) {
        uint8_t* dst = dstPixel(gutterPx, y + gutterPx);
        const uint8_t* srcRow = srcPixel(0, y);
        std::memcpy(dst, srcRow, static_cast<size_t>(width * 4));
    }

    // Dilate left/right borders.
    for (int y = 0; y < height; ++y) {
        uint8_t* rowStart = dstPixel(gutterPx, y + gutterPx);
        const uint8_t* left = rowStart;
        const uint8_t* right = rowStart + static_cast<size_t>((width - 1) * 4);
        for (int g = 1; g <= gutterPx; ++g) {
            std::memcpy(dstPixel(gutterPx - g, y + gutterPx), left, 4);
            std::memcpy(dstPixel(gutterPx + width - 1 + g, y + gutterPx), right, 4);
        }
    }

    // Dilate top/bottom borders (including corners copied from already-dilated rows).
    for (int g = 1; g <= gutterPx; ++g) {
        const uint8_t* topSrc = dstPixel(0, gutterPx);
        const uint8_t* bottomSrc = dstPixel(0, gutterPx + height - 1);
        std::memcpy(dstPixel(0, gutterPx - g), topSrc, static_cast<size_t>(paddedWidth * 4));
        std::memcpy(dstPixel(0, gutterPx + height - 1 + g), bottomSrc, static_cast<size_t>(paddedWidth * 4));
    }

    return padded;
}

} // namespace

TextureManager::TextureManager(const Config& config)
    : config_(config)
    , atlasEnabled_(config.textureAtlasEnabled)
    , atlasMipmaps_(config.textureAtlasMipmaps)
    , atlasAllocator_(config.textureAtlasSize, config.textureAtlasSlotSize, config.atlasGutterPx) {
    if (atlasEnabled_ && !atlasAllocator_.IsValid()) {
        atlasEnabled_ = false;
    }
    loadingTexture_ = CreateLoadingTexture();
}

TextureManager::~TextureManager() {
    for (uint32_t pageTexture : atlasPageTextures_) {
        if (pageTexture != 0) {
            glDeleteTextures(1, &pageTexture);
        }
    }
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

uint32_t TextureManager::CreateAtlasPageTexture() {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, atlasMipmaps_ ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        atlasAllocator_.GetAtlasSize(),
        atlasAllocator_.GetAtlasSize(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    if (atlasMipmaps_) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    ++textureCount_;
    return texture;
}

bool TextureManager::EnsureAtlasPageTexture(int pageIndex) {
    if (pageIndex < 0) {
        return false;
    }
    while (static_cast<int>(atlasPageTextures_.size()) <= pageIndex) {
        atlasPageTextures_.push_back(CreateAtlasPageTexture());
    }
    return atlasPageTextures_[pageIndex] != 0;
}

bool TextureManager::UploadToAtlas(Tile& tile) {
    if (!atlasEnabled_) {
        return false;
    }
    if (tile.pixelWidth <= 0 || tile.pixelHeight <= 0 || tile.pixels.empty()) {
        return false;
    }
    if (tile.pixelWidth > atlasAllocator_.GetSlotSize() ||
        tile.pixelHeight > atlasAllocator_.GetSlotSize()) {
        return false;
    }

    TextureAtlasAllocation allocation;
    if (tile.atlasAllocated) {
        if (!atlasAllocator_.Resolve(
                tile.atlasPage,
                tile.atlasSlot,
                tile.pixelWidth,
                tile.pixelHeight,
                allocation)) {
            tile.atlasAllocated = false;
            tile.atlasPage = -1;
            tile.atlasSlot = -1;
        }
    }

    if (!tile.atlasAllocated) {
        if (!atlasAllocator_.Allocate(tile.pixelWidth, tile.pixelHeight, allocation)) {
            return false;
        }
        tile.atlasAllocated = true;
        tile.atlasPage = allocation.pageIndex;
        tile.atlasSlot = allocation.slotIndex;
    }

    if (!EnsureAtlasPageTexture(allocation.pageIndex)) {
        ReleaseAtlasAllocation(tile);
        return false;
    }

    uint32_t atlasTextureId = atlasPageTextures_[allocation.pageIndex];

    int uploadX = allocation.x;
    int uploadY = allocation.y;
    int uploadWidth = allocation.width;
    int uploadHeight = allocation.height;
    const uint8_t* uploadPixels = tile.pixels.data();
    std::vector<uint8_t> dilatedPixels;
    if (config_.atlasEdgeDilate && allocation.gutterPx > 0) {
        dilatedPixels = BuildDilatedRgba(tile.pixels, tile.pixelWidth, tile.pixelHeight, allocation.gutterPx);
        if (!dilatedPixels.empty()) {
            uploadX = allocation.slotX;
            uploadY = allocation.slotY;
            uploadWidth = tile.pixelWidth + allocation.gutterPx * 2;
            uploadHeight = tile.pixelHeight + allocation.gutterPx * 2;
            uploadPixels = dilatedPixels.data();
        }
    }

    glBindTexture(GL_TEXTURE_2D, atlasTextureId);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        uploadX,
        uploadY,
        uploadWidth,
        uploadHeight,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        uploadPixels
    );
    if (atlasMipmaps_) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (tile.ownsTexture && tile.textureId != 0) {
        DeleteTexture(tile.textureId);
    }

    tile.textureId = atlasTextureId;
    tile.ownsTexture = false;
    tile.texWidth = atlasAllocator_.GetAtlasSize();
    tile.texHeight = atlasAllocator_.GetAtlasSize();
    tile.texScaleOffset = atlasAllocator_.ToUvTransform(allocation);
    tile.atlasAllocated = true;
    tile.atlasPage = allocation.pageIndex;
    tile.atlasSlot = allocation.slotIndex;
    tile.atlasContentWidth = allocation.width;
    tile.atlasContentHeight = allocation.height;
    return true;
}

void TextureManager::ReleaseAtlasAllocation(Tile& tile) {
    if (!tile.atlasAllocated) {
        return;
    }
    TextureAtlasAllocation allocation;
    allocation.pageIndex = tile.atlasPage;
    allocation.slotIndex = tile.atlasSlot;
    allocation.width = 1;
    allocation.height = 1;
    atlasAllocator_.Free(allocation);
    tile.atlasAllocated = false;
    tile.atlasPage = -1;
    tile.atlasSlot = -1;
    tile.atlasContentWidth = 0;
    tile.atlasContentHeight = 0;
}

void TextureManager::ReleaseTileResources(Tile& tile) {
    ReleaseAtlasAllocation(tile);
    if (tile.ownsTexture && tile.textureId != 0) {
        DeleteTexture(tile.textureId);
    }
    tile.textureId = 0;
    tile.ownsTexture = false;
    tile.texWidth = 0;
    tile.texHeight = 0;
    tile.texScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    tile.atlasContentWidth = 0;
    tile.atlasContentHeight = 0;
}

void TextureManager::TrimTrailingEmptyAtlasPages() {
    if (!atlasEnabled_) {
        return;
    }

    int removedPages = atlasAllocator_.TrimTrailingEmptyPages();
    while (removedPages > 0 && !atlasPageTextures_.empty()) {
        uint32_t textureId = atlasPageTextures_.back();
        atlasPageTextures_.pop_back();
        if (textureId != 0) {
            glDeleteTextures(1, &textureId);
            textureCount_ = std::max(0, textureCount_ - 1);
        }
        --removedPages;
    }
}

bool TextureManager::CopyAtlasRegion(uint32_t srcTextureId, int srcX, int srcY,
                                     uint32_t dstTextureId, int dstX, int dstY,
                                     int width, int height) {
    if (srcTextureId == 0 || dstTextureId == 0 || width <= 0 || height <= 0) {
        return false;
    }

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);

    GLuint readFbo = 0;
    GLuint drawFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glGenFramebuffers(1, &drawFbo);

    bool ok = false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTextureId, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTextureId, 0);

    GLenum readStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum drawStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (readStatus == GL_FRAMEBUFFER_COMPLETE && drawStatus == GL_FRAMEBUFFER_COMPLETE) {
        glBlitFramebuffer(
            srcX, srcY, srcX + width, srcY + height,
            dstX, dstY, dstX + width, dstY + height,
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST
        );
        ok = (glGetError() == GL_NO_ERROR);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glDeleteFramebuffers(1, &readFbo);
    glDeleteFramebuffers(1, &drawFbo);
    return ok;
}

int TextureManager::CompactAtlas(std::unordered_map<TileKey, Tile>& tiles, int maxMoves, double budgetMs) {
    if (!atlasEnabled_ || atlasAllocator_.GetPageCount() <= 1 || maxMoves <= 0) {
        return 0;
    }

    double startMs = glfwGetTime() * 1000.0;
    int moves = 0;

    while (moves < maxMoves) {
        if (budgetMs > 0.0) {
            double elapsedMs = glfwGetTime() * 1000.0 - startMs;
            if (elapsedMs >= budgetMs && moves > 0) {
                break;
            }
        }

        int highestPage = atlasAllocator_.GetPageCount() - 1;
        while (highestPage > 0 && atlasAllocator_.GetPageUsedSlots(highestPage) == 0) {
            --highestPage;
        }
        if (highestPage <= 0) {
            break;
        }

        Tile* srcTile = nullptr;
        for (auto& [_, tile] : tiles) {
            if (!tile.atlasAllocated || tile.atlasPage != highestPage) {
                continue;
            }
            if (tile.textureId == 0) {
                continue;
            }
            if (srcTile == nullptr || tile.lastAccessTime < srcTile->lastAccessTime) {
                srcTile = &tile;
            }
        }
        if (srcTile == nullptr) {
            break;
        }

        int contentWidth = std::max(1, srcTile->atlasContentWidth);
        int contentHeight = std::max(1, srcTile->atlasContentHeight);

        TextureAtlasAllocation dstAlloc;
        if (!atlasAllocator_.Allocate(contentWidth, contentHeight, dstAlloc)) {
            break;
        }
        if (dstAlloc.pageIndex >= srcTile->atlasPage) {
            atlasAllocator_.Free(dstAlloc);
            break;
        }

        TextureAtlasAllocation srcAlloc;
        if (!atlasAllocator_.Resolve(srcTile->atlasPage, srcTile->atlasSlot, contentWidth, contentHeight, srcAlloc)) {
            atlasAllocator_.Free(dstAlloc);
            break;
        }

        if (!EnsureAtlasPageTexture(dstAlloc.pageIndex)) {
            atlasAllocator_.Free(dstAlloc);
            break;
        }

        uint32_t srcTextureId = atlasPageTextures_[srcAlloc.pageIndex];
        uint32_t dstTextureId = atlasPageTextures_[dstAlloc.pageIndex];
        int gutterPx = (config_.atlasEdgeDilate ? srcAlloc.gutterPx : 0);
        int copyWidth = srcAlloc.width + gutterPx * 2;
        int copyHeight = srcAlloc.height + gutterPx * 2;
        int srcCopyX = srcAlloc.x - gutterPx;
        int srcCopyY = srcAlloc.y - gutterPx;
        int dstCopyX = dstAlloc.x - gutterPx;
        int dstCopyY = dstAlloc.y - gutterPx;
        if (!CopyAtlasRegion(srcTextureId, srcCopyX, srcCopyY, dstTextureId, dstCopyX, dstCopyY,
                             copyWidth, copyHeight)) {
            atlasAllocator_.Free(dstAlloc);
            break;
        }

        atlasAllocator_.Free(srcAlloc);
        srcTile->atlasPage = dstAlloc.pageIndex;
        srcTile->atlasSlot = dstAlloc.slotIndex;
        srcTile->textureId = dstTextureId;
        srcTile->texScaleOffset = atlasAllocator_.ToUvTransform(dstAlloc);
        srcTile->atlasContentWidth = dstAlloc.width;
        srcTile->atlasContentHeight = dstAlloc.height;
        ++moves;
    }

    TrimTrailingEmptyAtlasPages();
    return moves;
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

        // Explicit upload-start lifecycle event before GPU work.
        TileStateMachine::Advance(tile, TileStateMachine::Event::UploadStart, glfwGetTime());
        
        bool uploadedToAtlas = UploadToAtlas(tile);

        if (!uploadedToAtlas) {
            if (tile.atlasAllocated) {
                ReleaseAtlasAllocation(tile);
            }

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
                    DeleteTexture(tile.textureId);
                }
                
                // Create new texture
                tile.textureId = CreateTexture(tile.pixels.data(), tile.pixelWidth, tile.pixelHeight);
                tile.ownsTexture = true;
                tile.texWidth = tile.pixelWidth;
                tile.texHeight = tile.pixelHeight;
            }

            // Half-texel inset to reduce visible raster seams on per-tile textures.
            // Atlas path already applies an equivalent inset in TextureAtlasAllocator::ToUvTransform().
            if (tile.texWidth > 1 && tile.texHeight > 1) {
                float insetX = 0.5f / static_cast<float>(tile.texWidth);
                float insetY = 0.5f / static_cast<float>(tile.texHeight);
                tile.texScaleOffset = glm::vec4(
                    1.0f - 2.0f * insetX,
                    1.0f - 2.0f * insetY,
                    insetX,
                    insetY
                );
            } else {
                tile.texScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            }
            tile.atlasAllocated = false;
            tile.atlasPage = -1;
            tile.atlasSlot = -1;
            tile.atlasContentWidth = 0;
            tile.atlasContentHeight = 0;
        }
        
        // Use state machine for upload completion (handles fade reset)
        TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk, glfwGetTime());
        
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
        textureCount_ = std::max(0, textureCount_ - 1);
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

    if (static_cast<int>(tiles.size()) > maxTiles) {
        struct EvictionCandidate {
            TileKey key;
            double lastAccess = 0.0;
            int atlasPage = -1;
        };

        // Build list of eviction candidates and count actual pinned tiles
        std::vector<EvictionCandidate> candidates;
        int actualPinnedCount = 0;

        for (const auto& [key, tile] : tiles) {
            if (tile.state == TileState::Ready && tile.textureId != 0) {
                if (IsPinned(tile)) {
                    ++actualPinnedCount;
                } else {
                    EvictionCandidate candidate;
                    candidate.key = key;
                    candidate.lastAccess = tile.lastAccessTime;
                    candidate.atlasPage = tile.atlasAllocated ? tile.atlasPage : -1;
                    candidates.push_back(candidate);
                }
            }
        }

        if (!candidates.empty()) {
            // Evict oldest unpinned tiles.
            // When pinned >= maxTiles, unpinnedTarget clamps to 0 → evict ALL unpinned
            int unpinnedTarget = std::max(0, maxTiles - actualPinnedCount);
            int toEvict = std::max(0, static_cast<int>(candidates.size()) - unpinnedTarget);

            if (toEvict > 0) {
                // Atlas-aware ordering: prefer higher atlas pages first to free whole pages.
                std::sort(candidates.begin(), candidates.end(),
                          [](const EvictionCandidate& a, const EvictionCandidate& b) {
                              if (a.atlasPage != b.atlasPage) {
                                  return a.atlasPage > b.atlasPage;
                              }
                              return a.lastAccess < b.lastAccess;
                          });

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

                    const TileKey& key = candidates[i].key;
                    auto it = tiles.find(key);
                    if (it != tiles.end()) {
                        Tile& tile = it->second;
                        TileStateMachine::Advance(tile, TileStateMachine::Event::Evict, glfwGetTime());

                        // Notify eviction callback (e.g., for heightmap cleanup)
                        if (evictionCallback_) {
                            evictionCallback_(key);
                        }

                        // Delete/release texture resources (owned texture or atlas slot).
                        ReleaseTileResources(tile);

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
        }
    }

    // Maintain atlas packing even when no tile eviction is needed.
    if (atlasEnabled_) {
        int maxMoves = std::max(1, config_.maxEvictsPerFrame / 2);
        double compactBudgetMs = std::max(0.05, config_.evictBudgetMs * 0.25);
        CompactAtlas(tiles, maxMoves, compactBudgetMs);
    }
}

} // namespace globe
