#pragma once

#include "tile_key.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace globe {

// Tile lifecycle states
enum class TileState {
    Unloaded,       // Not in memory
    Scheduled,      // Queued for fetch
    Fetching,       // HTTP request in progress
    Decoding,       // Image decode in progress
    Uploading,      // GPU upload pending
    Ready,          // Fully loaded, renderable
    Failed          // Load failed
};

// Tile data structure
struct Tile {
    // Identity
    TileKey key;
    
    // State
    TileState state = TileState::Unloaded;
    
    // Geometry (computed from key)
    glm::vec3 center{0.0f};       // World-space center
    float boundingRadius = 0.0f;   // Bounding sphere radius
    float angularRadius = 0.0f;    // Angular extent (radians)
    
    // Texture
    uint32_t textureId = 0;        // OpenGL texture ID (0 = none)
    bool ownsTexture = false;      // True if we should delete texture
    
    // Decoded data (temporary, cleared after upload)
    std::vector<uint8_t> pixels;
    int pixelWidth = 0;
    int pixelHeight = 0;
    
    // Mesh
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t indexCount = 0;
    bool hasMesh = false;
    
    // DEM/Elevation
    bool demUsed = false;
    bool demPending = false;
    
    // Usage tracking
    uint64_t lastFrameUsed = 0;
    uint32_t accessCount = 0;
    double lastAccessTime = 0.0;
    
    // Priority
    float importance = 0.0f;       // For eviction decisions
    
    // Retry
    int retryCount = 0;
    double lastRetryTime = 0.0;
    
    // Constructor
    Tile() = default;
    explicit Tile(const TileKey& k) : key(k) {}
    Tile(int z, int x, int y) : key(z, x, y) {}
    
    // State queries
    bool IsReady() const { return state == TileState::Ready && textureId != 0; }
    bool IsLoading() const {
        return state == TileState::Scheduled ||
               state == TileState::Fetching ||
               state == TileState::Decoding ||
               state == TileState::Uploading;
    }
    bool IsFailed() const { return state == TileState::Failed; }
    
    // Cleanup
    void ClearPixels() {
        pixels.clear();
        pixels.shrink_to_fit();
        pixelWidth = 0;
        pixelHeight = 0;
    }
};

} // namespace globe
