#pragma once

#include "tile_key.h"
#include "extent.h"
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
    
    // Geographic extent (OpenGlobus integration)
    Extent extent;
    
    // State
    TileState state = TileState::Unloaded;
    
    // Geometry (computed from key)
    glm::vec3 center{0.0f};       // World-space center (ECEF)
    float boundingRadius = 0.0f;   // Bounding sphere radius
    float angularRadius = 0.0f;    // Angular extent (radians)
    
    // Compute extent from tile key
    void ComputeExtent() {
        extent = Extent::FromTileWGS84(key.x, key.y, key.level);
    }
    
    // Texture
    uint32_t textureId = 0;        // OpenGL texture ID (0 = none)
    bool ownsTexture = false;      // True if we should delete texture
    int texWidth = 0;              // GPU texture width
    int texHeight = 0;             // GPU texture height
    
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
    bool ownsEBO = true;
    bool meshPending = false;
    uint32_t meshRevision = 0;
    uint32_t meshBuiltRevision = 0;
    int builtSegments = 0;
    
    // DEM/Elevation
    bool demUsed = false;
    bool demPending = false;
    
    // Edge seam fix (FAZ 6.1): bits indicate which edges have coarser neighbors
    // Bit 0 = North (dy=-1), Bit 1 = East (dx=+1), Bit 2 = South (dy=+1), Bit 3 = West (dx=-1)
    uint8_t edgeCoarserMask = 0;
    uint8_t prevEdgeCoarserMask = 0;  // For tracking mesh rebuild need
    static constexpr uint8_t EDGE_NORTH = 1 << 0;  // 0x01
    static constexpr uint8_t EDGE_EAST  = 1 << 1;  // 0x02
    static constexpr uint8_t EDGE_SOUTH = 1 << 2;  // 0x04
    static constexpr uint8_t EDGE_WEST  = 1 << 3;  // 0x08
    
    // Fade-in animation (Google Earth style smooth appearance)
    float fadeAlpha = 0.0f;          // Current fade value (0=invisible, 1=fully visible)
    double fadeStartTime = 0.0;       // When fade started
    bool fadeComplete = false;        // True when fade finished
    static constexpr float FADE_DURATION = 0.3f;  // 300ms fade-in
    
    // Update fade animation, returns current alpha
    float UpdateFade(double currentTime) {
        if (fadeComplete) return 1.0f;
        if (fadeStartTime == 0.0) {
            fadeStartTime = currentTime;
        }
        float elapsed = static_cast<float>(currentTime - fadeStartTime);
        fadeAlpha = std::min(1.0f, elapsed / FADE_DURATION);
        if (fadeAlpha >= 1.0f) {
            fadeComplete = true;
            fadeAlpha = 1.0f;
        }
        return fadeAlpha;
    }
    
    // Usage tracking
    uint64_t lastFrameUsed = 0;
    uint32_t accessCount = 0;
    double lastAccessTime = 0.0;
    
    // Priority
    float importance = 0.0f;       // For eviction decisions
    uint8_t requestPriority = 1;   // 0=Low, 1=Normal, 2=Urgent
    
    // Retry
    int retryCount = 0;
    double lastRetryTime = 0.0;

    // Pinning (per-frame epoch)
    uint32_t pinnedEpoch = 0;
    
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
        pixelWidth = 0;
        pixelHeight = 0;
    }
};

} // namespace globe
