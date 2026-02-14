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
    Canceled,       // Loading canceled while out of view
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
    
    // RTE/RTC (Relative-to-Center) origin for jitter-free rendering
    // Double-precision origin split into high/low for GPU double emulation
    glm::vec3 originEcefHi{0.0f};  // High 16 bits of tile origin
    glm::vec3 originEcefLo{0.0f};  // Low 16 bits of tile origin
    
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
    
    // Faz 2B: Texture2DArray layer storage (prevents bleeding)
    int textureLayerHandle = -1;   // Layer handle in TextureArrayManager (-1 = not in array)
    int textureArrayLayer = -1;    // Actual layer index in GL texture array (-1 = not set)
    int textureArrayTier = -1;     // Tier ID for array lookup (-1 = not in array)
    bool usesTextureArray = false; // True when textureId is sourced from GL_TEXTURE_2D_ARRAY
    
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
    uint32_t surfaceVertexCount = 0;  // Surface vertices (skirt excluded), renderability metadata
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
    // P2-1: Max elevation for conservative bounding radius (prevents false-positive culling)
    float maxHeightKm = 0.0f;  // Maximum terrain height in km (0 = flat/unknown)
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
    // FIX A: Latched seam-skirt mask. Once a seam gap triggers a skirt on an edge,
    // it stays ON until a structural change (DEM level, edge coarser mask) resets it.
    // Prevents frame-to-frame oscillation: gap → skirt → gap hidden → skirt removed → gap...
    uint8_t latchedSeamSkirtMask = 0;
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
    // FIX 5: Max stagger spread so batch DEM arrivals don't all flash at once.
    static constexpr float TERRAIN_MORPH_MAX_STAGGER = 0.10f;  // 100ms
    
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
            // FIX 5: Stagger morph start using tile key hash so batch DEM completions
            // produce a smooth wave rather than a single-frame flash.
            // Hash spreads tiles across [0, TERRAIN_MORPH_MAX_STAGGER] seconds delay.
            uint32_t h = static_cast<uint32_t>(key.level * 73856093u) ^
                         static_cast<uint32_t>(key.x * 19349663u) ^
                         static_cast<uint32_t>(key.y * 83492791u);
            float stagger = static_cast<float>(h % 1000u) / 1000.0f * TERRAIN_MORPH_MAX_STAGGER;
            terrainMorphStartTime = currentTime + static_cast<double>(stagger);
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
