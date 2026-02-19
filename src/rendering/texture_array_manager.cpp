// TextureArrayManager.cpp
// Layer-based texture storage implementation

#include "texture_array_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace globe {

TextureArrayManager::TextureArrayManager(const Config& config)
    : config_(config) {
}

TextureArrayManager::~TextureArrayManager() {
    Shutdown();
}

TextureArrayManager::TextureArrayManager(TextureArrayManager&& other) noexcept
    : config_(other.config_)
    , tiers_(std::move(other.tiers_))
    , currentFrame_(other.currentFrame_)
    , initialized_(other.initialized_)
    , stats_(other.stats_) {
    other.initialized_ = false;
}

TextureArrayManager& TextureArrayManager::operator=(TextureArrayManager&& other) noexcept {
    if (this != &other) {
        Shutdown();
        
        config_ = other.config_;
        tiers_ = std::move(other.tiers_);
        currentFrame_ = other.currentFrame_;
        initialized_ = other.initialized_;
        stats_ = other.stats_;
        
        other.initialized_ = false;
    }
    return *this;
}

bool TextureArrayManager::Initialize() {
    if (initialized_) {
        return true;
    }
    
    // Check GL version supports texture arrays
    if (!GLAD_GL_VERSION_3_0 && !GLAD_GL_EXT_texture_array) {
        std::cerr << "[TextureArrayManager] GL_TEXTURE_2D_ARRAY not supported (requires GL 3.0+ or GL_EXT_texture_array)\n";
        return false;
    }
    
    // P0-2: Check max array layers - need at least 128 for reasonable tile storage
    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    if (maxLayers < 128) {
        std::cerr << "[TextureArrayManager] Insufficient GL_MAX_ARRAY_TEXTURE_LAYERS (" 
                  << maxLayers << " < 128), texture arrays disabled\n";
        return false;
    }
    
    initialized_ = true;
    return true;
}

void TextureArrayManager::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Destroy all tiers
    for (auto& tier : tiers_) {
        DestroyTier(tier);
    }
    tiers_.clear();
    
    initialized_ = false;
}

bool TextureArrayManager::InitializeTier(Tier& tier) {
    if (tier.initialized) {
        return true;
    }
    
    glGenTextures(1, &tier.textureId);
    if (tier.textureId == 0) {
        return false;
    }
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, tier.textureId);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, 
                    tier.config.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Anisotropic filtering
    if (config_.useAnisotropicFiltering) {
        if (GLAD_GL_EXT_texture_filter_anisotropic) {
            GLfloat maxAniso = 0.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 
                           std::min(config_.maxAnisotropy, maxAniso));
        }
    }
    
    // Allocate storage
    GLint mipLevels = tier.config.generateMipmaps ? 
        (tier.config.maxMipLevels > 0 ? tier.config.maxMipLevels : 
         static_cast<GLint>(1 + std::floor(std::log2(std::max(tier.config.tileWidth, tier.config.tileHeight)))))
        : 1;
    
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, mipLevels, tier.config.internalFormat,
                   tier.config.tileWidth, tier.config.tileHeight, tier.config.maxLayers);
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &tier.textureId);
        tier.textureId = 0;
        return false;
    }
    
    // Initialize layer info
    tier.layers.resize(tier.config.maxLayers);
    for (int i = 0; i < tier.config.maxLayers; ++i) {
        tier.layers[i].handle = INVALID_LAYER_HANDLE;
        tier.layers[i].inUse = false;
    }
    
    // Initialize free list (all layers free initially)
    tier.freeList.reserve(tier.config.maxLayers);
    for (int i = tier.config.maxLayers - 1; i >= 0; --i) {
        tier.freeList.push_back(i);
    }
    
    tier.stats.maxLayers = tier.config.maxLayers;
    tier.stats.freeLayers = tier.config.maxLayers;
    tier.stats.currentTextureId = static_cast<GLsizei>(tier.textureId);
    tier.initialized = true;
    
    return true;
}

void TextureArrayManager::DestroyTier(Tier& tier) {
    if (!tier.initialized) {
        return;
    }
    
    if (tier.textureId != 0) {
        glDeleteTextures(1, &tier.textureId);
        tier.textureId = 0;
    }
    
    tier.layers.clear();
    tier.freeList.clear();
    tier.handleToIndex.clear();
    tier.initialized = false;
}

bool TextureArrayManager::ResizeTier(Tier& tier, GLint newMaxLayers) {
    if (!tier.initialized || newMaxLayers <= tier.config.maxLayers) {
        return false;
    }
    
    // Save old state
    GLuint oldTexture = tier.textureId;
    GLint oldMaxLayers = tier.config.maxLayers;
    
    // Create new larger texture
    tier.config.maxLayers = newMaxLayers;
    
    glGenTextures(1, &tier.textureId);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tier.textureId);
    
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, 
                    tier.config.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    GLint mipLevels = tier.config.generateMipmaps ? 
        (tier.config.maxMipLevels > 0 ? tier.config.maxMipLevels : 
         static_cast<GLint>(1 + std::floor(std::log2(std::max(tier.config.tileWidth, tier.config.tileHeight)))))
        : 1;
    
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, mipLevels, tier.config.internalFormat,
                   tier.config.tileWidth, tier.config.tileHeight, newMaxLayers);
    
    // Copy existing layers
    for (int i = 0; i < oldMaxLayers; ++i) {
        if (tier.layers[i].inUse) {
            // Copy from old texture to new
            // Note: This requires glCopyImageSubData or framebuffer blit
            // For simplicity, we'll just invalidate old layers
            tier.layers[i].inUse = false;
            tier.layers[i].handle = INVALID_LAYER_HANDLE;
        }
    }
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glDeleteTextures(1, &oldTexture);
    
    // Rebuild layer info and free list
    tier.layers.resize(newMaxLayers);
    tier.freeList.clear();
    tier.handleToIndex.clear();
    
    for (int i = 0; i < newMaxLayers; ++i) {
        if (!tier.layers[i].inUse) {
            tier.freeList.push_back(i);
        }
    }
    
    tier.stats.maxLayers = newMaxLayers;
    tier.stats.freeLayers = static_cast<GLint>(tier.freeList.size());
    tier.stats.tierResizes++;
    
    return glGetError() == GL_NO_ERROR;
}

int TextureArrayManager::RegisterTier(const TierConfig& config) {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    return RegisterTierInternal(config);
}

int TextureArrayManager::RegisterTierInternal(const TierConfig& config) {
    Tier tier;
    tier.config = config;
    
    if (!InitializeTier(tier)) {
        return -1;
    }
    
    int tierId = static_cast<int>(tiers_.size());
    tiers_.push_back(std::move(tier));

    return tierId;
}

int TextureArrayManager::GetOrCreateTier(GLsizei width, GLsizei height, bool generateMipmaps) {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    // Find existing tier with matching dimensions
    for (int i = 0; i < static_cast<int>(tiers_.size()); ++i) {
        const auto& tier = tiers_[i];
        if (tier.config.tileWidth == width && 
            tier.config.tileHeight == height &&
            tier.config.generateMipmaps == generateMipmaps) {
            return i;
        }
    }
    
    // Create new tier
    TierConfig config;
    config.tileWidth = width;
    config.tileHeight = height;
    config.maxLayers = config_.initialLayersPerTier;
    config.internalFormat = GL_RGBA8;
    config.format = GL_RGBA;
    config.type = GL_UNSIGNED_BYTE;
    config.generateMipmaps = generateMipmaps;
    config.maxMipLevels = 0;
    
    return RegisterTierInternal(config);
}

LayerHandle TextureArrayManager::AllocLayerInTier(Tier& tier) {
    if (!tier.initialized) {
        return INVALID_LAYER_HANDLE;
    }
    
    // Try to get from free list
    if (!tier.freeList.empty()) {
        int layerIndex = tier.freeList.back();
        tier.freeList.pop_back();
        
        LayerHandle handle = tier.nextHandle++;
        
        tier.layers[layerIndex].handle = handle;
        tier.layers[layerIndex].inUse = true;
        tier.layers[layerIndex].lastUsedFrame = currentFrame_;
        tier.layers[layerIndex].width = tier.config.tileWidth;
        tier.layers[layerIndex].height = tier.config.tileHeight;
        
        tier.handleToIndex[handle] = layerIndex;
        
        tier.stats.usedLayers++;
        tier.stats.freeLayers = static_cast<GLint>(tier.freeList.size());
        
        return handle;
    }
    
    // No free layers - could try to resize or evict
    // For now, return invalid
    return INVALID_LAYER_HANDLE;
}

LayerHandle TextureArrayManager::AllocateLayer(int tierId) {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    if (tierId < 0 || tierId >= static_cast<int>(tiers_.size())) {
        return INVALID_LAYER_HANDLE;
    }
    
    Tier& tier = tiers_[tierId];
    LayerHandle handle = AllocLayerInTier(tier);
    
    if (handle != INVALID_LAYER_HANDLE) {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.totalUploads++;
    } else {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.failedAllocations++;
    }
    
    return handle;
}

void TextureArrayManager::FreeLayerInTier(Tier& tier, LayerHandle handle) {
    auto it = tier.handleToIndex.find(handle);
    if (it == tier.handleToIndex.end()) {
        return;
    }
    
    int layerIndex = it->second;
    
    tier.layers[layerIndex].handle = INVALID_LAYER_HANDLE;
    tier.layers[layerIndex].inUse = false;
    tier.layers[layerIndex].ownerKey = nullptr;
    
    tier.freeList.push_back(layerIndex);
    tier.handleToIndex.erase(it);
    
    tier.stats.usedLayers--;
    tier.stats.freeLayers = static_cast<GLint>(tier.freeList.size());
    tier.stats.layerRecycles++;
}

void TextureArrayManager::FreeLayer(LayerHandle handle) {
    if (handle == INVALID_LAYER_HANDLE) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    // Find which tier owns this handle
    for (auto& tier : tiers_) {
        if (tier.handleToIndex.find(handle) != tier.handleToIndex.end()) {
            FreeLayerInTier(tier, handle);
            
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.totalRecycles++;
            return;
        }
    }
}

bool TextureArrayManager::IsLayerValid(LayerHandle handle) const {
    if (handle == INVALID_LAYER_HANDLE) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    for (const auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it != tier.handleToIndex.end()) {
            return tier.layers[it->second].IsValid();
        }
    }
    
    return false;
}

const LayerInfo* TextureArrayManager::GetLayerInfo(LayerHandle handle) const {
    if (handle == INVALID_LAYER_HANDLE) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    for (const auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it != tier.handleToIndex.end()) {
            return &tier.layers[it->second];
        }
    }
    
    return nullptr;
}

bool TextureArrayManager::UploadToLayer(LayerHandle handle, const void* data, 
                                        GLsizei width, GLsizei height,
                                        GLenum format, GLenum type) {
    if (!data || handle == INVALID_LAYER_HANDLE) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    // Find tier and layer index
    for (auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it == tier.handleToIndex.end()) {
            continue;
        }
        
        int layerIndex = it->second;
        if (!tier.layers[layerIndex].inUse) {
            return false;
        }
        
        glBindTexture(GL_TEXTURE_2D_ARRAY, tier.textureId);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layerIndex,
                        width, height, 1, format, type, data);
        
        if (tier.config.generateMipmaps) {
            // Generate mipmaps for this specific layer
            // Note: glGenerateMipmap generates for all layers, which is expensive
            // For per-layer mipmap generation, we'd need manual mipmap upload
            // or use GL_ARB_sparse_texture if available
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        }
        
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        
        tier.layers[layerIndex].lastUsedFrame = currentFrame_;
        tier.layers[layerIndex].dirty = false;
        
        return glGetError() == GL_NO_ERROR;
    }
    
    return false;
}

bool TextureArrayManager::UploadToLayerOwned(LayerHandle handle, std::vector<uint8_t>&& data,
                                             GLsizei width, GLsizei height) {
    // For now, just upload and discard the data
    // In a full PBO integration, this would queue the upload
    bool success = UploadToLayer(handle, data.data(), width, height, GL_RGBA, GL_UNSIGNED_BYTE);
    
    // Data is consumed either way (moved)
    (void)data;
    
    return success;
}

GLuint TextureArrayManager::GetTierTextureId(int tierId) const {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    if (tierId < 0 || tierId >= static_cast<int>(tiers_.size())) {
        return 0;
    }
    
    return tiers_[tierId].textureId;
}

GLint TextureArrayManager::GetLayerIndex(LayerHandle handle) const {
    if (handle == INVALID_LAYER_HANDLE) {
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    for (const auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it != tier.handleToIndex.end()) {
            return it->second;
        }
    }
    
    return -1;
}

bool TextureArrayManager::BindTier(int tierId, GLuint textureUnit) const {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    if (tierId < 0 || tierId >= static_cast<int>(tiers_.size())) {
        return false;
    }
    
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tiers_[tierId].textureId);
    
    return true;
}

bool TextureArrayManager::BindLayer(LayerHandle handle, GLuint textureUnit) const {
    // For single-layer binding, we need to either:
    // 1. Use glBindTextureLayer (GL 4.5+)
    // 2. Bind the array and set a uniform for the layer index
    // 3. Create a view texture
    
    // For now, just bind the tier and rely on shader to select layer
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    for (const auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it != tier.handleToIndex.end()) {
            glActiveTexture(GL_TEXTURE0 + textureUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tier.textureId);
            return true;
        }
    }
    
    return false;
}

void TextureArrayManager::TouchLayer(LayerHandle handle) {
    if (handle == INVALID_LAYER_HANDLE) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    for (auto& tier : tiers_) {
        auto it = tier.handleToIndex.find(handle);
        if (it != tier.handleToIndex.end()) {
            tier.layers[it->second].lastUsedFrame = currentFrame_;
            return;
        }
    }
}

int TextureArrayManager::EvictLRU(int targetFreeCount) {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    int evicted = 0;
    
    for (auto& tier : tiers_) {
        if (evicted >= targetFreeCount) {
            break;
        }
        
        // Find LRU layers in this tier
        std::vector<std::pair<int, uint64_t>> candidates; // layer index, last used frame
        for (int i = 0; i < static_cast<int>(tier.layers.size()); ++i) {
            if (tier.layers[i].inUse) {
                candidates.push_back({i, tier.layers[i].lastUsedFrame});
            }
        }
        
        // Sort by last used frame (oldest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) {
                      return a.second < b.second;
                  });
        
        // Evict oldest
        for (const auto& [layerIndex, _] : candidates) {
            if (evicted >= targetFreeCount) {
                break;
            }
            
            LayerHandle handle = tier.layers[layerIndex].handle;
            FreeLayerInTier(tier, handle);
            ++evicted;
        }
    }
    
    return evicted;
}

void TextureArrayManager::BeginFrame(uint64_t frameNumber) {
    currentFrame_ = frameNumber;
}

void TextureArrayManager::EndFrame() {
    // Could do periodic cleanup here
}

ArrayStats TextureArrayManager::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

TierStats TextureArrayManager::GetTierStats(int tierId) const {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    if (tierId < 0 || tierId >= static_cast<int>(tiers_.size())) {
        return TierStats{};
    }
    
    return tiers_[tierId].stats;
}

void TextureArrayManager::ResetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = ArrayStats{};
    
    for (auto& tier : tiers_) {
        tier.stats.totalUploads = 0;
        tier.stats.layerRecycles = 0;
        tier.stats.tierResizes = 0;
    }
}

void TextureArrayManager::DumpState() const {
    std::lock_guard<std::mutex> lock(tiersMutex_);
    
    std::cerr << "[TextureArrayManager] State:\n";
    std::cerr << "  Initialized: " << (initialized_ ? "yes" : "no") << "\n";
    std::cerr << "  Current frame: " << currentFrame_ << "\n";
    std::cerr << "  Tier count: " << tiers_.size() << "\n";
    
    for (int i = 0; i < static_cast<int>(tiers_.size()); ++i) {
        const auto& tier = tiers_[i];
        std::cerr << "  Tier " << i << ":\n";
        std::cerr << "    Size: " << tier.config.tileWidth << "x" << tier.config.tileHeight << "\n";
        std::cerr << "    Max layers: " << tier.config.maxLayers << "\n";
        std::cerr << "    Used: " << tier.stats.usedLayers << "\n";
        std::cerr << "    Free: " << tier.stats.freeLayers << "\n";
        std::cerr << "    Texture ID: " << tier.textureId << "\n";
    }
}

} // namespace globe
