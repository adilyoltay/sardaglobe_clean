#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "tile_key.h"

struct TileMesh {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  GLsizei indexCount = 0;
};

// HS-style pole mesh for north/south pole rendering
struct PoleMesh {
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  GLsizei indexCount = 0;
  bool initialized = false;
};

// Edge flags for seam stitching (bit flags)
enum EdgeFlag {
  EDGE_NONE   = 0,
  EDGE_LEFT   = 1 << 0,  // Left edge needs stitching (neighbor is lower LOD)
  EDGE_RIGHT  = 1 << 1,  // Right edge needs stitching
  EDGE_TOP    = 1 << 2,  // Top edge needs stitching
  EDGE_BOTTOM = 1 << 3   // Bottom edge needs stitching
};

// JS parity: Texture loading state machine (main.js tr_* constants)
enum class TextureState {
  NONE = 0,           // Initial state, no texture
  LOADING = 1,        // tr_loading - request in progress
  LOAD_OK = 2,        // tr_load_OK - texture loaded successfully
  LOAD_OK_NO_DATA = 3,// tr_load_OK_NoData - empty/transparent tile
  LOAD_NO_INTERNET = 4// tr_load_NoInternet - network error
};

// Google Earth style tile lifecycle state machine
// Provides fine-grained control over tile loading and rendering
enum class TileLoadState {
  UNLOADED = 0,       // Not yet requested
  SCHEDULED = 1,      // In download queue, waiting to be fetched
  FETCHING = 2,       // HTTP request in flight
  DECODING = 3,       // Data received, being decoded/processed
  UPLOADING = 4,      // Uploading to GPU (texture creation)
  READY = 5,          // Ready to render
  FAILED = 6,         // Load failed (network error, decode error, etc.)
  EVICTED = 7         // Evicted from cache, needs reload
};

// Helper to convert TileLoadState to string (for debugging)
inline const char* TileLoadStateToString(TileLoadState state) {
  switch (state) {
    case TileLoadState::UNLOADED: return "UNLOADED";
    case TileLoadState::SCHEDULED: return "SCHEDULED";
    case TileLoadState::FETCHING: return "FETCHING";
    case TileLoadState::DECODING: return "DECODING";
    case TileLoadState::UPLOADING: return "UPLOADING";
    case TileLoadState::READY: return "READY";
    case TileLoadState::FAILED: return "FAILED";
    case TileLoadState::EVICTED: return "EVICTED";
    default: return "UNKNOWN";
  }
}

// Support URL modes (transparent/empty/out-of-bbox)
enum class SupportMode {
  NONE = 0,
  TRANSPARENT_PIXEL = 1,
  EMPTY_CONTENT = 2,
  OUT_OF_BBOX = 3
};

// Cesium-style tile selection state for stable LOD transitions
enum class TileSelectionState {
  NONE = 0,              // Not visited this frame
  CULLED = 1,            // Frustum culled
  RENDERED = 2,          // Selected for rendering
  REFINED = 3,           // SSE exceeded, subdivided to children
  RENDERED_AND_KICKED = 4,// Was rendered, kicked for ancestor fallback
  REFINED_AND_KICKED = 5  // Was refined, kicked for ancestor fallback
};

// Download priority levels (lower = higher priority)
enum class LoadPriority {
  URGENT = 0,     // Visible leaf tiles, immediate need
  NORMAL = 1,     // Visible parent/ancestor tiles
  PRELOAD = 2,    // Prefetch for smooth panning
  LOW = 3         // Background loading
};

struct Tile {
  // Identification
  TileKey key;  // Replaces separate x,y,z
  
  // Kept for compatibility during refactor, will sync with key
  int x = 0;
  int y = 0;
  int z = 0;
  
  // Rendering resources
  GLuint texture = 0;
  bool ownsTexture = false;
  TileMesh mesh;
  
  // State machines
  TextureState textureState = TextureState::NONE;  // JS parity state machine
  TileSelectionState selectionState = TileSelectionState::NONE;  // Cesium-style selection
  TileLoadState loadState = TileLoadState::UNLOADED;  // Google Earth style lifecycle
  
  // Geometry
  glm::vec3 center = {};
  float angularRadius = 0.0f;
  float radius = 0.0f;
  
  // Visual effects
  float fade = 0.0f;
  // Unpop transition (Google Earth style smooth texture appearance)
  double unpopStartTime = 0.0;    // Time when texture became ready
  float unpopFactor = 1.0f;       // 0 = show parent, 1 = show this tile's texture
  bool unpopComplete = false;     // True when transition finished
  
  // Metrics & Caching
  float lastSSE = 0.0f;           // Last computed screen space error
  uint32_t lastFrameUsed = 0;     // Frame number when last used (for LRU)
  size_t estimatedBytes = 0;      // Estimated memory usage for byte-based cache
  
  // Texture mapping
  glm::vec2 uvOffset = glm::vec2(0.0f);
  glm::vec2 uvScale = glm::vec2(1.0f);
  
  // Stitching
  int edgeFlags = EDGE_NONE;  // Which edges need seam stitching
  
  // Terrain / DEM
  bool demPending = false;    // Mesh built before DEM data was ready
  bool demUsed = false;       // Mesh built with DEM displacement
  double lastDemCheckTime = 0.0;
  
  // Support / Fallback
  SupportMode supportMode = SupportMode::NONE;
  bool supportPending = false;
  std::vector<unsigned char> supportMainPixels;
  int supportMainWidth = 0;
  int supportMainHeight = 0;
  
  // Integration: Decoded pixel data waiting for upload
  std::vector<unsigned char> decodedData;
  int decodedWidth = 0;
  int decodedHeight = 0;
  
  // Networking / Retry
  int retryCount = 0;         // For retry logic on failures
  double lastRetryTime = 0.0; // P3: Backoff - time of last retry attempt
  LoadPriority loadPriority = LoadPriority::NORMAL;
  
  // Scheduler Unification
  std::string layerId;
  bool isVector = false;
  
  // Ancestor-meets-SSE tracking: which ancestor can render if this tile not ready
  std::string fallbackAncestorKey;
  bool ancestorMeetsSSE = false;  // True if an ancestor satisfies SSE threshold
  
  // Cache Pin/Unpin (Google Earth style - prevent eviction of important tiles)
  bool pinned = false;            // If true, tile won't be evicted from cache
  float importanceScore = 0.0f;   // Higher = less likely to evict (LRU + importance)
  int accessCount = 0;            // Number of times tile was accessed
  
  // Constructor
  Tile() = default;
  Tile(int level, int tx, int ty) : key(level, tx, ty), x(tx), y(ty), z(level) {}
};
