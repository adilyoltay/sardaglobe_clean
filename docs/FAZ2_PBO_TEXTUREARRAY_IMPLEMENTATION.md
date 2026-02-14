# Faz 2 - PBO ve Texture2Array Implementasyon Kılavuzu

> **Hedef:** GPU upload stutter'ını ve texture bleeding'i çözmek  
> **Süre:** 4-5 gün  
> **Öncelik:** P0 (Engelleyici)

---

## Bölüm 1: PBO (Pixel Buffer Object) Upload Sistemi

### 1.1 Neden PBO?

**Mevcut Sorun:**
```cpp
// tile_manager.cpp
void UploadTexture(const std::vector<uint8_t>& pixels, int width, int height) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, 
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // ^ Bu main thread'i bloke edebilir! CPU->GPU kopyası senkron
}
```

**PBO Çözümü:**
- CPU'dan GPU'ya async DMA transfer
- Main thread sadece küçük bir "bind + texSubImage" yapar
- Asıl veri kopyası DMA controller tarafından async yapılır

### 1.2 Mimari

```cpp
// src/rendering/pbo_upload_manager.h
#pragma once

#include <glad/glad.h>
#include <vector>
#include <queue>
#include <memory>
#include <functional>
#include "../core/tile_key.h"

namespace globe {

// Async texture upload via PBO
class PboUploadManager {
public:
    struct UploadRequest {
        TileKey key;
        std::vector<uint8_t> data;
        int width;
        int height;
        std::function<void(GLuint)> onComplete;  // Callback with texture ID
    };
    
    struct UploadResult {
        TileKey key;
        bool success;
        GLuint textureId;
        std::string error;
    };

    explicit PboUploadManager(int ringBufferCount = 2);
    ~PboUploadManager();
    
    // Initialize PBO ring buffers
    bool Init();
    void Shutdown();
    
    // Stage data for upload (can be called from any thread)
    void StageUpload(UploadRequest request);
    
    // Process pending uploads (must be called from main thread with GL context)
    // Returns number of uploads processed
    int ProcessUploads(double budgetMs);
    
    // Get completed uploads (call after ProcessUploads)
    std::vector<UploadResult> GetCompletedUploads();
    
    // Check if there are pending uploads
    bool HasPendingUploads() const;
    
private:
    struct PboRingBuffer {
        GLuint pboId = 0;
        size_t capacity = 0;
        bool inUse = false;
    };
    
    struct PendingUpload {
        TileKey key;
        GLuint pboId;
        size_t dataSize;
        int width;
        int height;
        std::function<void(GLuint)> onComplete;
        double submitTime;
    };
    
    std::vector<PboRingBuffer> ringBuffers_;
    std::queue<UploadRequest> stagingQueue_;
    std::queue<PendingUpload> pendingUploads_;
    std::vector<UploadResult> completedUploads_;
    
    mutable std::mutex stagingMutex_;
    mutable std::mutex completedMutex_;
    
    bool initialized_ = false;
    
    // Internal
    bool AllocatePboBuffer(PboRingBuffer& pbo, size_t size);
    void FreePboBuffer(PboRingBuffer& pbo);
    PboRingBuffer* AcquireRingBuffer();
    void ReleaseRingBuffer(GLuint pboId);
};

} // namespace globe
```

### 1.3 Implementasyon

```cpp
// src/rendering/pbo_upload_manager.cpp
#include "pbo_upload_manager.h"
#include <glad/glad.h>
#include <iostream>
#include <chrono>

namespace globe {

PboUploadManager::PboUploadManager(int ringBufferCount) {
    ringBuffers_.resize(ringBufferCount);
}

PboUploadManager::~PboUploadManager() {
    Shutdown();
}

bool PboUploadManager::Init() {
    if (initialized_) return true;
    
    // Check GL version support
    if (!GLAD_GL_ARB_pixel_buffer_object && !GLAD_GL_EXT_pixel_buffer_object) {
        std::cerr << "[PBO] PBO not supported, falling back to sync upload\n";
        // Still functional, just not accelerated
    }
    
    initialized_ = true;
    return true;
}

void PboUploadManager::Shutdown() {
    if (!initialized_) return;
    
    // Clean up any pending uploads
    while (!pendingUploads_.empty()) {
        pendingUploads_.pop();
    }
    
    // Delete PBOs
    for (auto& pbo : ringBuffers_) {
        if (pbo.pboId != 0) {
            glDeleteBuffers(1, &pbo.pboId);
            pbo.pboId = 0;
        }
    }
    
    initialized_ = false;
}

bool PboUploadManager::AllocatePboBuffer(PboRingBuffer& pbo, size_t size) {
    if (pbo.pboId == 0) {
        glGenBuffers(1, &pbo.pboId);
    }
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo.pboId);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    pbo.capacity = size;
    return true;
}

void PboUploadManager::FreePboBuffer(PboRingBuffer& pbo) {
    if (pbo.pboId != 0) {
        glDeleteBuffers(1, &pbo.pboId);
        pbo.pboId = 0;
        pbo.capacity = 0;
    }
}

PboRingBuffer* PboUploadManager::AcquireRingBuffer() {
    for (auto& pbo : ringBuffers_) {
        if (!pbo.inUse) {
            pbo.inUse = true;
            return &pbo;
        }
    }
    return nullptr;
}

void PboUploadManager::ReleaseRingBuffer(GLuint pboId) {
    for (auto& pbo : ringBuffers_) {
        if (pbo.pboId == pboId) {
            pbo.inUse = false;
            return;
        }
    }
}

void PboUploadManager::StageUpload(UploadRequest request) {
    std::lock_guard<std::mutex> lock(stagingMutex_);
    stagingQueue_.push(std::move(request));
}

int PboUploadManager::ProcessUploads(double budgetMs) {
    auto startTime = std::chrono::high_resolution_clock::now();
    int processedCount = 0;
    
    // Move staged uploads to pending (with PBO allocation)
    {
        std::lock_guard<std::mutex> lock(stagingMutex_);
        while (!stagingQueue_.empty()) {
            auto& request = stagingQueue_.front();
            
            // Acquire ring buffer
            auto* pbo = AcquireRingBuffer();
            if (!pbo) {
                break;  // No free PBOs, wait for next frame
            }
            
            // Ensure PBO has enough capacity
            size_t dataSize = request.data.size();
            if (pbo->capacity < dataSize) {
                AllocatePboBuffer(*pbo, dataSize * 2);  // 2x for future growth
            }
            
            // Upload data to PBO (async DMA)
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo->pboId);
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, dataSize, request.data.data());
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            
            // Move to pending queue
            PendingUpload pending;
            pending.key = request.key;
            pending.pboId = pbo->pboId;
            pending.dataSize = dataSize;
            pending.width = request.width;
            pending.height = request.height;
            pending.onComplete = std::move(request.onComplete);
            pending.submitTime = glfwGetTime();
            
            pendingUploads_.push(std::move(pending));
            
            // Clear request data (moved to GPU)
            request.data.clear();
            request.data.shrink_to_fit();
            
            stagingQueue_.pop();
        }
    }
    
    // Process pending uploads (create textures from PBOs)
    while (!pendingUploads_.empty()) {
        auto& pending = pendingUploads_.front();
        
        // Check time budget
        auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
        double elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();
        if (elapsedMs >= budgetMs) {
            break;
        }
        
        // Create texture from PBO
        GLuint textureId;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        
        // Allocate texture storage
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pending.width, pending.height, 
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        
        // Bind PBO and upload to texture (fast, GPU-GPU copy)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pending.pboId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pending.width, pending.height,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);  // Offset 0 = PBO start
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        
        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Generate mipmaps
        glGenerateMipmap(GL_TEXTURE_2D);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        // Release PBO back to pool
        ReleaseRingBuffer(pending.pboId);
        
        // Call completion callback
        if (pending.onComplete) {
            pending.onComplete(textureId);
        }
        
        // Add to completed
        {
            std::lock_guard<std::mutex> lock(completedMutex_);
            completedUploads_.push_back({pending.key, true, textureId, ""});
        }
        
        processedCount++;
        pendingUploads_.pop();
    }
    
    return processedCount;
}

std::vector<PboUploadManager::UploadResult> PboUploadManager::GetCompletedUploads() {
    std::lock_guard<std::mutex> lock(completedMutex_);
    std::vector<UploadResult> results;
    results.swap(completedUploads_);
    return results;
}

bool PboUploadManager::HasPendingUploads() const {
    std::lock_guard<std::mutex> lock(stagingMutex_);
    return !stagingQueue_.empty() || !pendingUploads_.empty();
}

} // namespace globe
```

### 1.4 TextureManager Entegrasyonu

```cpp
// src/rendering/texture_manager.h
// Mevcut TextureManager'a PBO entegrasyonu

class TextureManager {
public:
    // ... mevcut API ...
    
    // PBO entegrasyonu
    void SetUsePboUploads(bool usePbo) { usePboUploads_ = usePbo; }
    
    // Process PBO uploads (call from main thread)
    void ProcessPboUploads(double budgetMs);
    
private:
    // ... mevcut alanlar ...
    
    bool usePboUploads_ = false;
    std::unique_ptr<PboUploadManager> pboManager_;
};
```

```cpp
// src/rendering/texture_manager.cpp
void TextureManager::ProcessPboUploads(double budgetMs) {
    if (!usePboUploads_ || !pboManager_) return;
    
    // Process pending uploads
    int processed = pboManager_->ProcessUploads(budgetMs);
    
    // Get completed uploads
    auto completed = pboManager_->GetCompletedUploads();
    for (auto& result : completed) {
        if (result.success) {
            // Update tile with new texture
            auto it = tiles_.find(result.key);
            if (it != tiles_.end()) {
                it->second.textureId = result.textureId;
                it->second.hasTexture = true;
            }
        }
    }
}
```

---

## Bölüm 2: Texture2Array Migration

### 2.1 Neden Texture2DArray?

**Mevcut Texture Atlas Sorunları:**
- Gutter/padding gereksinimi (2-4px)
- Mipmap seviyelerinde bleeding
- Atlas yeniden paketleme maliyeti
- Fragment shader'da atlas UV hesaplama overhead

**Texture2Array Avantajları:**
- Her tile kendi layer'ında (tam izolasyon)
- Sıfır bleeding (gutter yok)
- Daha basit shader (no atlas math)
- Daha hızlı sampling (daha iyi cache locality)

### 2.2 Mimari

```cpp
// src/rendering/texture_array_manager.h
#pragma once

#include <glad/glad.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../core/tile_key.h"

namespace globe {

// Manages 2D texture arrays for tile storage
// Each resolution tier has its own array
class TextureArrayManager {
public:
    // Resolution tiers (tile sizes)
    static constexpr int TIER_256 = 0;   // 256x256 tiles
    static constexpr int TIER_512 = 1;   // 512x512 tiles
    static constexpr int NUM_TIERS = 2;
    
    struct LayerInfo {
        TileKey key;
        int layerIndex;
        bool inUse;
        double lastAccess;
    };
    
    struct TextureArray {
        GLuint textureId = 0;
        int resolution;
        int numLayers;
        std::vector<LayerInfo> layers;
        std::vector<int> freeLayers;  // Stack of free layer indices
    };
    
    explicit TextureArrayManager(int layersPerArray = 128);
    ~TextureArrayManager();
    
    bool Init();
    void Shutdown();
    
    // Allocate a layer for a tile
    // Returns layer handle (tier << 16 | layerIndex) or -1 on failure
    int AllocateLayer(const TileKey& key, int width, int height);
    
    // Upload texture data to a layer
    bool UploadToLayer(int layerHandle, const std::vector<uint8_t>& rgba, 
                       int width, int height);
    
    // Free a layer
    void FreeLayer(int layerHandle);
    
    // Get OpenGL texture ID for a tier
    GLuint GetTextureId(int tier) const;
    
    // Get layer index from handle
    static int GetLayerIndex(int layerHandle) { return layerHandle & 0xFFFF; }
    static int GetTier(int layerHandle) { return layerHandle >> 16; }
    static int MakeHandle(int tier, int layerIndex) { return (tier << 16) | layerIndex; }
    
    // Bind texture array for rendering
    void BindArray(int tier, GLuint textureUnit);
    
    // Stats
    int GetTotalLayers() const;
    int GetUsedLayers() const;
    int GetFreeLayers() const;
    
private:
    int layersPerArray_;
    std::vector<TextureArray> arrays_;
    std::unordered_map<TileKey, int, TileKeyHash> keyToHandle_;
    
    bool CreateArray(int tier, int resolution);
    void DestroyArray(TextureArray& array);
    int FindOrCreateArray(int resolution);
};

} // namespace globe
```

### 2.3 Implementasyon

```cpp
// src/rendering/texture_array_manager.cpp
#include "texture_array_manager.h"
#include <glad/glad.h>
#include <iostream>

namespace globe {

TextureArrayManager::TextureArrayManager(int layersPerArray) 
    : layersPerArray_(layersPerArray) {
    arrays_.resize(NUM_TIERS);
    arrays_[TIER_256].resolution = 256;
    arrays_[TIER_512].resolution = 512;
}

TextureArrayManager::~TextureArrayManager() {
    Shutdown();
}

bool TextureArrayManager::Init() {
    // Create arrays for each tier
    for (int tier = 0; tier < NUM_TIERS; ++tier) {
        if (!CreateArray(tier, arrays_[tier].resolution)) {
            std::cerr << "[TextureArray] Failed to create array for tier " << tier << "\n";
            return false;
        }
    }
    return true;
}

void TextureArrayManager::Shutdown() {
    for (auto& array : arrays_) {
        DestroyArray(array);
    }
    keyToHandle_.clear();
}

bool TextureArrayManager::CreateArray(int tier, int resolution) {
    auto& array = arrays_[tier];
    array.resolution = resolution;
    array.numLayers = layersPerArray_;
    array.layers.resize(layersPerArray_);
    
    // Initialize free layer stack (LIFO for cache friendliness)
    for (int i = layersPerArray_ - 1; i >= 0; --i) {
        array.freeLayers.push_back(i);
    }
    
    // Create OpenGL texture array
    glGenTextures(1, &array.textureId);
    glBindTexture(GL_TEXTURE_2D_ARRAY, array.textureId);
    
    // Allocate storage
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 
                 resolution, resolution, layersPerArray_,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    
    // Set parameters
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate mipmaps
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    
    std::cout << "[TextureArray] Created tier " << tier 
              << " array: " << resolution << "x" << resolution 
              << " x " << layersPerArray_ << " layers\n";
    
    return true;
}

void TextureArrayManager::DestroyArray(TextureArray& array) {
    if (array.textureId != 0) {
        glDeleteTextures(1, &array.textureId);
        array.textureId = 0;
    }
    array.layers.clear();
    array.freeLayers.clear();
}

int TextureArrayManager::AllocateLayer(const TileKey& key, int width, int height) {
    // Determine tier based on size
    int tier;
    if (width <= 256 && height <= 256) {
        tier = TIER_256;
    } else {
        tier = TIER_512;
    }
    
    auto& array = arrays_[tier];
    
    if (array.freeLayers.empty()) {
        std::cerr << "[TextureArray] Tier " << tier << " full!\n";
        return -1;
    }
    
    // Get free layer
    int layerIndex = array.freeLayers.back();
    array.freeLayers.pop_back();
    
    // Mark as used
    array.layers[layerIndex].key = key;
    array.layers[layerIndex].layerIndex = layerIndex;
    array.layers[layerIndex].inUse = true;
    array.layers[layerIndex].lastAccess = glfwGetTime();
    
    int handle = MakeHandle(tier, layerIndex);
    keyToHandle_[key] = handle;
    
    return handle;
}

bool TextureArrayManager::UploadToLayer(int layerHandle, 
                                        const std::vector<uint8_t>& rgba,
                                        int width, int height) {
    int tier = GetTier(layerHandle);
    int layerIndex = GetLayerIndex(layerHandle);
    
    if (tier < 0 || tier >= NUM_TIERS) return false;
    
    auto& array = arrays_[tier];
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, array.textureId);
    
    // Upload to specific layer
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 
                    0, 0, layerIndex,  // x, y, layer
                    width, height, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    
    // Update mipmaps for this layer
    // Note: GL 4.3+ has glGenerateTextureMipmap, but for 3.3:
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    
    return true;
}

void TextureArrayManager::FreeLayer(int layerHandle) {
    int tier = GetTier(layerHandle);
    int layerIndex = GetLayerIndex(layerHandle);
    
    if (tier < 0 || tier >= NUM_TIERS) return;
    
    auto& array = arrays_[tier];
    
    if (layerIndex >= 0 && layerIndex < array.numLayers) {
        // Remove from key map
        keyToHandle_.erase(array.layers[layerIndex].key);
        
        // Mark as free
        array.layers[layerIndex].inUse = false;
        array.freeLayers.push_back(layerIndex);
    }
}

GLuint TextureArrayManager::GetTextureId(int tier) const {
    if (tier < 0 || tier >= NUM_TIERS) return 0;
    return arrays_[tier].textureId;
}

void TextureArrayManager::BindArray(int tier, GLuint textureUnit) {
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, GetTextureId(tier));
}

int TextureArrayManager::GetTotalLayers() const {
    return NUM_TIERS * layersPerArray_;
}

int TextureArrayManager::GetUsedLayers() const {
    int used = 0;
    for (const auto& array : arrays_) {
        used += (array.numLayers - array.freeLayers.size());
    }
    return used;
}

int TextureArrayManager::GetFreeLayers() const {
    int free = 0;
    for (const auto& array : arrays_) {
        free += array.freeLayers.size();
    }
    return free;
}

} // namespace globe
```

### 2.4 Shader Değişiklikleri

```glsl
// tile_array_vertex.glsl
#version 330 core

uniform mat4 uMVP;
uniform vec3 uTileOriginHi;
uniform vec3 uTileOriginLo;

in vec3 aPosition;
in vec2 aTexCoord;

out vec2 vTexCoord;
flat out int vTextureLayer;  // Per-tile layer index

void main() {
    vec3 tileOrigin = uTileOriginHi + uTileOriginLo;
    vec3 worldPos = tileOrigin + aPosition;
    
    gl_Position = uMVP * vec4(worldPos, 1.0);
    vTexCoord = aTexCoord;
    // vTextureLayer set via glVertexAttribI1i or uniform
}

// tile_array_fragment.glsl
#version 330 core

uniform sampler2DArray uTextureArray;
uniform int uTextureLayer;

in vec2 vTexCoord;
flat in int vTextureLayer;

out vec4 FragColor;

void main() {
    // Sample from array using layer index
    vec4 color = texture(uTextureArray, vec3(vTexCoord, float(uTextureLayer)));
    FragColor = color;
}
```

### 2.5 TileRenderer Entegrasyonu

```cpp
// src/rendering/tile_renderer.cpp
void TileRenderer::RenderTile(const Tile& tile, const RenderState& state) {
    if (config_.useTexture2DArray) {
        RenderTileArray(tile, state);
    } else {
        RenderTileAtlas(tile, state);
    }
}

void TileRenderer::RenderTileArray(const Tile& tile, const RenderState& state) {
    // Get layer handle for this tile
    int layerHandle = tile.textureLayerHandle;
    if (layerHandle < 0) return;
    
    int tier = TextureArrayManager::GetTier(layerHandle);
    int layerIndex = TextureArrayManager::GetLayerIndex(layerHandle);
    
    // Bind texture array for this tier
    textureArrayManager_->BindArray(tier, 0);
    
    // Set layer uniform
    shaderArray_->SetUniform("uTextureLayer", layerIndex);
    
    // Bind mesh and draw
    glBindVertexArray(tile.vao);
    glDrawElements(GL_TRIANGLES, tile.indexCount, GL_UNSIGNED_INT, nullptr);
}
```

---

## Bölüm 3: Test Planı

### 3.1 PBO Testi

```cpp
// tests/pbo_upload_latency_test.cpp
TEST(PboUpload, AsyncVsSyncPerformance) {
    PboUploadManager pboManager(2);
    ASSERT_TRUE(pboManager.Init());
    
    // Create test data
    const int numTextures = 100;
    const int texSize = 256 * 256 * 4;
    std::vector<std::vector<uint8_t>> testData(numTextures);
    for (int i = 0; i < numTextures; ++i) {
        testData[i].resize(texSize);
        std::fill(testData[i].begin(), testData[i].end(), i % 256);
    }
    
    // Measure sync upload time
    auto syncStart = std::chrono::high_resolution_clock::now();
    std::vector<GLuint> syncTextures;
    for (int i = 0; i < numTextures; ++i) {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, testData[i].data());
        syncTextures.push_back(tex);
    }
    glFinish();  // Wait for all uploads
    auto syncEnd = std::chrono::high_resolution_clock::now();
    
    // Measure PBO upload time
    auto pboStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numTextures; ++i) {
        TileKey key{10, i % 16, i / 16};
        pboManager.StageUpload({key, std::move(testData[i]), 256, 256, nullptr});
    }
    
    // Process uploads with time budget
    int processed = 0;
    while (pboManager.HasPendingUploads() && processed < numTextures) {
        processed += pboManager.ProcessUploads(16.0);  // 16ms budget per frame
    }
    auto pboEnd = std::chrono::high_resolution_clock::now();
    
    // PBO should be faster or comparable
    double syncMs = std::chrono::duration<double, std::milli>(syncEnd - syncStart).count();
    double pboMs = std::chrono::duration<double, std::milli>(pboEnd - pboStart).count();
    
    std::cout << "Sync: " << syncMs << "ms, PBO: " << pboMs << "ms\n";
    EXPECT_EQ(processed, numTextures);
    
    // Cleanup
    for (auto tex : syncTextures) {
        glDeleteTextures(1, &tex);
    }
}
```

### 3.2 Texture Array Testi

```cpp
// tests/texture_array_test.cpp
TEST(TextureArray, NoBleedingBetweenLayers) {
    TextureArrayManager manager(4);  // 4 layers per tier
    ASSERT_TRUE(manager.Init());
    
    // Upload distinct patterns to adjacent layers
    const int size = 16;
    std::vector<uint8_t> red(size * size * 4);
    std::vector<uint8_t> blue(size * size * 4);
    for (int i = 0; i < size * size; ++i) {
        red[i*4+0] = 255; red[i*4+1] = 0; red[i*4+2] = 0; red[i*4+3] = 255;
        blue[i*4+0] = 0; blue[i*4+1] = 0; blue[i*4+2] = 255; blue[i*4+3] = 255;
    }
    
    TileKey key1{10, 0, 0};
    TileKey key2{10, 0, 1};
    
    int handle1 = manager.AllocateLayer(key1, size, size);
    int handle2 = manager.AllocateLayer(key2, size, size);
    
    ASSERT_GE(handle1, 0);
    ASSERT_GE(handle2, 0);
    
    EXPECT_TRUE(manager.UploadToLayer(handle1, red, size, size));
    EXPECT_TRUE(manager.UploadToLayer(handle2, blue, size, size));
    
    // Read back and verify no bleeding (would require framebuffer readback test)
    // This is a conceptual test - actual verification via render test
    
    // Verify layers are different
    EXPECT_NE(handle1, handle2);
}

TEST(TextureArray, LayerReuse) {
    TextureArrayManager manager(2);  // Only 2 layers
    ASSERT_TRUE(manager.Init());
    
    TileKey key1{10, 0, 0};
    TileKey key2{10, 0, 1};
    TileKey key3{10, 0, 2};  // Third tile
    
    int handle1 = manager.AllocateLayer(key1, 256, 256);
    int handle2 = manager.AllocateLayer(key2, 256, 256);
    EXPECT_GE(handle1, 0);
    EXPECT_GE(handle2, 0);
    
    // Free first layer
    manager.FreeLayer(handle1);
    
    // Should be able to allocate third
    int handle3 = manager.AllocateLayer(key3, 256, 256);
    EXPECT_GE(handle3, 0);
    
    // Should reuse first layer
    EXPECT_EQ(TextureArrayManager::GetLayerIndex(handle1), 
              TextureArrayManager::GetLayerIndex(handle3));
}
```

---

## Bölüm 4: Checklist

### PBO Implementasyon Checklist

- [ ] `PboUploadManager` sınıfı oluşturuldu
- [ ] Ring buffer alloc/free mantığı doğru
- [ ] Async DMA upload çalışıyor
- [ ] Main thread blocking minimize edildi
- [ ] `TextureManager` entegrasyonu tamamlandı
- [ ] Fallback (PBO yoksa sync upload) çalışıyor
- [ ] PBO upload latency test geçiyor

### Texture Array Checklist

- [ ] `TextureArrayManager` sınıfı oluşturuldu
- [ ] Tier-based resolution sistemi çalışıyor
- [ ] Layer allocate/free mantığı doğru
- [ ] Texture upload/subimage çalışıyor
- [ ] Shader array sampling implemente edildi
- [ ] No bleeding test geçiyor
- [ ] Layer reuse test geçiyor
- [ ] Memory kullanımı atlasa göre optimize

### Entegrasyon Checklist

- [ ] Feature flag'ler çalışıyor (`usePboUploads`, `useTexture2DArray`)
- [ ] Eski atlas path hala çalışıyor (backward compat)
- [ ] Frame stutter test geçiyor
- [ ] Memory usage benchmark yapıldı
- [ ] Tüm mevcut testler geçiyor
