#pragma once

#include "tile.h"
#include "tile_key.h"
#include "globe_config.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

// Height sampling callback type
using HeightSampler = std::function<bool(double lonDeg, double latDeg, int level, double& heightMeters)>;

// Forward declarations
struct DownloadJob;
struct DownloadJobComparator;
class DeferredQueue;

// ============================================================================
// TILE SYNC CONTEXT
// ============================================================================

// Context struct to reduce parameter passing in tile sync functions
struct TileSyncContext {
  std::unordered_map<std::string, Tile>& tiles;
  const GlobeConfig& config;
  GLuint loadingTexture;
  const std::unordered_set<std::string>& required;
  const std::vector<std::string>& leaves;
  std::unordered_set<std::string>& pending;
  std::priority_queue<DownloadJob, std::vector<DownloadJob>, DownloadJobComparator>& downloadQueue;
  std::mutex& downloadMutex;
  std::condition_variable& downloadCv;
  int segments;
  const HeightSampler* heightSampler;
  size_t& meshRebuilds;
  size_t maxRebuilds;
  size_t& queueSize;
  size_t& textureUploads;
  size_t maxUploads;
  uint32_t currentFrame;
  std::mutex* pendingMutex;
  const glm::vec3* cameraPos;
  int currentZoom;
  const glm::mat4* mvp;
  
  // Precomputed data
  std::unordered_set<std::string> leafSet;
  int baseMinZoom;
  int baseMaxZoom;
  double now;
  bool canSampleDem;
};

// ============================================================================
// TILE MANAGER CLASS
// ============================================================================

class TileManager {
public:
    TileManager() = default;
    ~TileManager() = default;
    
    // Tile lifecycle
    void CreateTileIfNeeded(std::unordered_map<std::string, Tile>& tiles,
                            const TileKey& key,
                            GLuint loadingTexture);
    
    // Download priority calculation (Google Earth style)
    static float ComputeDownloadPriority(const Tile& tile,
                                         const glm::vec3* cameraPos,
                                         const glm::mat4* mvp,
                                         int currentZoom,
                                         bool isLeaf);
    
    // Cache management
    void EvictTiles(std::unordered_map<std::string, Tile>& tiles,
                    const std::unordered_set<std::string>& required,
                    size_t maxCachedBytes,
                    double timeLimitMs,
                    DeferredQueue* deferredQueue);
    
    // Pin/Unpin (Google Earth style cache protection)
    void PinTile(const std::string& key);
    void UnpinTile(const std::string& key);
    bool IsTilePinned(const std::string& key) const;
    
    // Prefetch system (Google Earth style - preload tiles based on camera trajectory)
    struct PrefetchConfig {
        bool enabled;
        int neighborRings;
        int maxPrefetchPerFrame;
        float minCameraSpeed;
        
        PrefetchConfig() 
            : enabled(true)
            , neighborRings(1)           // How many rings of neighbors to prefetch
            , maxPrefetchPerFrame(8)     // Max prefetch requests per frame
            , minCameraSpeed(0.01f) {}   // Minimum camera speed for directional prefetch
    };
    
    // Get tiles to prefetch based on visible leaves and camera movement
    static std::vector<TileKey> ComputePrefetchTiles(
        const std::vector<std::string>& visibleLeaves,
        const glm::vec3& cameraVelocity,
        const PrefetchConfig& config);
    
    // Statistics
    struct Stats {
        size_t totalTiles = 0;
        size_t readyTiles = 0;
        size_t loadingTiles = 0;
        size_t failedTiles = 0;
        size_t pinnedTiles = 0;
        size_t cachedBytes = 0;
    };
    
    Stats GetStats(const std::unordered_map<std::string, Tile>& tiles) const;
    
private:
    std::unordered_set<std::string> pinnedTiles_;
    mutable std::mutex pinMutex_;
};

// ============================================================================
// HELPER FUNCTIONS (moved from globe_engine.cpp)
// ============================================================================

// Compute required ancestors for fallback rendering
std::unordered_set<std::string> ComputeRequiredAncestors(
    const std::vector<std::pair<TileKey, std::string>>& orderedRequired);

// Estimate tile memory usage
size_t EstimateTileBytes(const Tile& tile);
