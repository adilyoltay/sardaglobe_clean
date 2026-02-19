// TextureArrayManager.h
// Layer-based texture storage using GL_TEXTURE_2D_ARRAY
// Prevents bleeding between tiles (atlas problem)
// Reference: Google Earth WASM mirth engine

#ifndef GLOBE_TEXTURE_ARRAY_MANAGER_H_
#define GLOBE_TEXTURE_ARRAY_MANAGER_H_

#include <glad/glad.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <functional>

namespace globe {

// Forward declarations
struct TileKey;

// Layer handle for texture array
using LayerHandle = int;
constexpr LayerHandle INVALID_LAYER_HANDLE = -1;

// Tier configuration
struct TierConfig {
    GLsizei tileWidth;          // Width of each tile in pixels
    GLsizei tileHeight;         // Height of each tile in pixels
    GLint maxLayers;            // Maximum layers in this tier
    GLenum internalFormat;      // Internal format (e.g., GL_RGBA8)
    GLenum format;              // Pixel format (e.g., GL_RGBA)
    GLenum type;                // Pixel type (e.g., GL_UNSIGNED_BYTE)
    bool generateMipmaps;       // Generate mipmaps for this tier
    GLsizei maxMipLevels;       // Maximum mipmap levels (0 = all)
};

// Layer metadata
struct LayerInfo {
    LayerHandle handle = INVALID_LAYER_HANDLE;
    GLsizei width = 0;
    GLsizei height = 0;
    uint64_t lastUsedFrame = 0;
    bool inUse = false;
    bool dirty = false;         // Needs mipmap regeneration
    TileKey* ownerKey = nullptr; // Optional: track which tile owns this
    
    bool IsValid() const { return handle != INVALID_LAYER_HANDLE && inUse; }
};

// Tier statistics
struct TierStats {
    GLint maxLayers = 0;
    GLint usedLayers = 0;
    GLint freeLayers = 0;
    uint64_t totalUploads = 0;
    uint64_t layerRecycles = 0;
    uint64_t tierResizes = 0;
    GLsizei currentTextureId = 0;
};

// Array usage statistics
struct ArrayStats {
    int tierCount = 0;
    uint64_t totalUploads = 0;
    uint64_t totalRecycles = 0;
    uint64_t failedAllocations = 0;
};

// Texture Array Manager
// Manages multiple tiers of texture arrays for different tile sizes
class TextureArrayManager {
public:
    // Configuration
    struct Config {
        bool useTexture2DArray;      // Enable texture array path
        int initialLayersPerTier;    // Initial layer count per tier
        int maxLayersPerTier;        // Maximum layers per tier
        bool generateMipmaps;        // Generate mipmaps
        bool useAnisotropicFiltering; // Use anisotropic filtering
        float maxAnisotropy;         // Max anisotropy level
        
        Config()
            : useTexture2DArray(true)
            , initialLayersPerTier(64)
            , maxLayersPerTier(256)
            , generateMipmaps(true)
            , useAnisotropicFiltering(true)
            , maxAnisotropy(16.0f) {}
    };

    explicit TextureArrayManager(const Config& config = Config{});
    ~TextureArrayManager();

    // Non-copyable, movable
    TextureArrayManager(const TextureArrayManager&) = delete;
    TextureArrayManager& operator=(const TextureArrayManager&) = delete;
    TextureArrayManager(TextureArrayManager&&) noexcept;
    TextureArrayManager& operator=(TextureArrayManager&&) noexcept;

    // Initialize OpenGL resources
    bool Initialize();
    void Shutdown();

    // Tier management
    // Register a tier for specific tile dimensions
    // Returns tier ID, or -1 on failure
    int RegisterTier(const TierConfig& config);
    
    // Get tier ID for tile dimensions (creates if needed)
    int GetOrCreateTier(GLsizei width, GLsizei height, bool generateMipmaps = true);

    // Layer allocation
    // Allocate a layer in specified tier
    // Returns layer handle, or INVALID_LAYER_HANDLE on failure
    LayerHandle AllocateLayer(int tierId);
    
    // Free a layer (returns it to the free pool)
    void FreeLayer(LayerHandle handle);
    
    // Check if layer is valid
    bool IsLayerValid(LayerHandle handle) const;
    
    // Get layer info
    const LayerInfo* GetLayerInfo(LayerHandle handle) const;

    // Upload data to layer
    // Can be called from PBO completion callback
    bool UploadToLayer(LayerHandle handle, const void* data, GLsizei width, GLsizei height,
                       GLenum format, GLenum type);
    
    // Upload with owned data (moves into manager)
    bool UploadToLayerOwned(LayerHandle handle, std::vector<uint8_t>&& data,
                            GLsizei width, GLsizei height);

    // Get configuration
    const Config& GetConfig() const { return config_; }
    
    // Get texture ID for tier (for binding)
    GLuint GetTierTextureId(int tierId) const;
    
    // Get layer index for handle (for shader uniform)
    GLint GetLayerIndex(LayerHandle handle) const;

    // Bind tier texture to texture unit
    bool BindTier(int tierId, GLuint textureUnit) const;
    
    // Bind specific layer (for single-layer rendering)
    bool BindLayer(LayerHandle handle, GLuint textureUnit) const;

    // Eviction
    // Mark layer as recently used
    void TouchLayer(LayerHandle handle);
    
    // Evict least recently used layers to free space
    // Returns number of layers evicted
    int EvictLRU(int targetFreeCount);

    // Frame management
    void BeginFrame(uint64_t frameNumber);
    void EndFrame();

    // Statistics
    ArrayStats GetStats() const;
    TierStats GetTierStats(int tierId) const;
    void ResetStats();

    // Debug
    void DumpState() const;

private:
    struct Tier {
        TierConfig config;
        GLuint textureId = 0;
        std::vector<LayerInfo> layers;
        std::vector<int> freeList;          // Indices of free layers
        std::unordered_map<int, int> handleToIndex; // handle -> layer index
        int nextHandle = 0;
        TierStats stats;
        bool initialized = false;
    };

    bool InitializeTier(Tier& tier);
    void DestroyTier(Tier& tier);
    bool ResizeTier(Tier& tier, GLint newMaxLayers);
    LayerHandle AllocLayerInTier(Tier& tier);
    void FreeLayerInTier(Tier& tier, LayerHandle handle);
    int RegisterTierInternal(const TierConfig& config);

private:
    Config config_;
    std::vector<Tier> tiers_;
    mutable std::mutex tiersMutex_;
    
    uint64_t currentFrame_ = 0;
    bool initialized_ = false;
    
    // Stats
    mutable std::mutex statsMutex_;
    ArrayStats stats_;
};

} // namespace globe

#endif // GLOBE_TEXTURE_ARRAY_MANAGER_H_
