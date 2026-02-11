#pragma once

#include "tile_key.h"
#include "extent.h"
#include <glm/glm.hpp>
#include <algorithm>
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
    // Timestamp (glfwGetTime seconds) when the state last changed.
    // Used for stale-loading detection/recovery (Scheduled/Fetching/Decoding/Uploading).
    double stateEnterTime = 0.0;
    
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
    glm::vec4 texScaleOffset{1.0f, 1.0f, 0.0f, 0.0f};  // UV transform: uv * scale.xy + offset.zw
    bool atlasAllocated = false;   // True if texture lives in shared atlas slot
    int atlasPage = -1;            // Atlas page index (if atlasAllocated)
    int atlasSlot = -1;            // Atlas slot index (if atlasAllocated)
    int atlasContentWidth = 0;     // Uploaded source width inside atlas slot
    int atlasContentHeight = 0;    // Uploaded source height inside atlas slot
    
    // Decoded data (temporary, cleared after upload)
    std::vector<uint8_t> pixels;
    int pixelWidth = 0;
    int pixelHeight = 0;
    bool hasTransparentPixels = false;
    bool mostlyBlackOpaqueRaster = false;
    
    // Mesh
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t indexCount = 0;
    uint32_t mainIndexCount = 0;
    uint32_t skirtIndexCount = 0;
    bool hasMesh = false;
    bool ownsEBO = true;
    bool meshPending = false;
    uint32_t meshRevision = 0;
    uint32_t meshBuiltRevision = 0;
    int builtSegments = 0;
    
    // DEM/Elevation
    bool demUsed = false;
    bool demPending = false;
    uint8_t demSourceLevelMin = 0;
    uint8_t demSourceLevelMax = 0;
    uint16_t demMissingSamples = 0;
    // DEM level requested by the engine for mesh baking (may be lower than tile level for coherence).
    uint8_t demTargetLevel = 0;
    // DEM level effectively used by the mesh builder (authoritative level chosen from cache/ancestors).
    uint8_t demEffectiveLevel = 0;
    // DEM edge-coherence sampling levels (packed 4x uint8, bytes: N,E,S,W).
    // When non-zero, mesh builder blends border vertices toward these levels to reduce cracks/cliffs.
    uint32_t demEdgeLevelPack = 0;
    float edgeGapMaxM = 0.0f;
    // Per-edge seam gap (meters), populated by engine seam scan.
    // Order: North, East, South, West.
    glm::vec4 edgeGapM{0.0f};
    uint8_t seamGapMask = 0;  // Bits for edges whose seam gap exceeds warning threshold (telemetry -> skirt feedback).

    // Mesh-edge height samples (km, already heightScale-adjusted) for seam/cliff measurement.
    // Layout: [North (W->E), East (N->S), South (W->E), West (N->S)], each of length (borderSegments+1).
    uint16_t borderSegments = 0;
    std::vector<float> borderHeightsKm;
    
    // Edge seam fix (FAZ 6.1): bits indicate which edges have coarser neighbors
    // Bit 0 = North (dy=-1), Bit 1 = East (dx=+1), Bit 2 = South (dy=+1), Bit 3 = West (dx=-1)
    uint8_t edgeCoarserMask = 0;
    uint8_t prevEdgeCoarserMask = 0;  // For tracking mesh rebuild need
    uint8_t stitchMask = 0;
    uint8_t skirtMask = 0;
    static constexpr uint8_t EDGE_NORTH = 1 << 0;  // 0x01
    static constexpr uint8_t EDGE_EAST  = 1 << 1;  // 0x02
    static constexpr uint8_t EDGE_SOUTH = 1 << 2;  // 0x04
    static constexpr uint8_t EDGE_WEST  = 1 << 3;  // 0x08

    // GE-style corner LODs for bilinear interpolation in vertex shader.
    // Order: NW, NE, SE, SW (with UV: NW=(0,1), NE=(1,1), SE=(1,0), SW=(0,0)).
    glm::vec4 cornerLods{0.0f};
    
    // Fade-in animation (Google Earth style smooth appearance)
    float fadeAlpha = 0.0f;          // Current fade value (0=invisible, 1=fully visible)
    double fadeStartTime = 0.0;       // When fade started
    bool fadeComplete = false;        // True when fade finished
    static constexpr float FADE_DURATION = 0.3f;  // 300ms default unpop duration

    // Terrain morph (flat -> displaced) to avoid DEM pop on first heightmap availability.
    float terrainMorph = 1.0f;             // 0=flat, 1=full terrain
    double terrainMorphStartTime = 0.0;
    bool terrainMorphActive = false;
    bool hadTerrainData = false;
    static constexpr float TERRAIN_MORPH_DURATION = 0.2f;  // 200ms
    
    // Update fade animation, returns current alpha
    float UpdateFade(double currentTime, float fadeDuration = FADE_DURATION) {
        if (fadeComplete) return 1.0f;
        if (fadeStartTime == 0.0) {
            fadeStartTime = currentTime;
        }
        float duration = std::max(0.01f, fadeDuration);
        float elapsed = static_cast<float>(currentTime - fadeStartTime);
        float computedAlpha = std::min(1.0f, elapsed / duration);
        // Keep fade monotonic even if duration changes frame-to-frame.
        fadeAlpha = std::max(fadeAlpha, computedAlpha);
        if (fadeAlpha >= 1.0f) {
            fadeComplete = true;
            fadeAlpha = 1.0f;
        }
        return fadeAlpha;
    }

    // Update terrain morph state. Returns current morph factor in [0,1].
    float UpdateTerrainMorph(double currentTime,
                             bool hasTerrainData,
                             float morphDuration = TERRAIN_MORPH_DURATION) {
        if (!hasTerrainData) {
            hadTerrainData = false;
            terrainMorphActive = false;
            terrainMorphStartTime = 0.0;
            terrainMorph = 0.0f;
            return terrainMorph;
        }

        if (!hadTerrainData) {
            hadTerrainData = true;
            terrainMorphActive = true;
            terrainMorphStartTime = currentTime;
            terrainMorph = 0.0f;
        }

        if (terrainMorphActive) {
            float duration = std::max(0.01f, morphDuration);
            float elapsed = static_cast<float>(currentTime - terrainMorphStartTime);
            terrainMorph = std::clamp(elapsed / duration, 0.0f, 1.0f);
            if (terrainMorph >= 1.0f) {
                terrainMorph = 1.0f;
                terrainMorphActive = false;
            }
        } else if (terrainMorph < 1.0f) {
            terrainMorph = 1.0f;
        }

        return terrainMorph;
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
