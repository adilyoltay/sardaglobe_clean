// RockMesh Manager
// Manages RockTree NodeData fetch, decode, and GPU upload
// Sprint 1: Single quadkey vertical slice
// Sprint 2: LOD-aware mesh management with visible set tracking

#pragma once

#include "rockmesh_runtime.h"
#include "../io/providers/google_earth_nodedata_client.h"
#include "../io/providers/rocktree_node_data_parser.h"
#include "../io/providers/rocktree_octree_index.h"
#include "../io/providers/ge_rate_limiter.h"
#include "../core/config.h"
#include "../core/bounded_queue.h"
#include "../core/tile_key.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <queue>
#include <list>
#include <memory>

// Sprint 2.2: Forward declare for disk cache
namespace globe {
class NodeDataDiskCache;
}

namespace globe {

// State machine for mesh loading
enum class RockMeshState {
    None,
    Queued,
    Fetching,
    ReadyToUpload,
    Uploaded,
    Failed,
    Stale  // Sprint 2: Out of visible set, marked for eviction
};

struct RockMeshEntry {
    RockMeshState state = RockMeshState::None;
    RockMeshGpu gpu;
    std::string error;
    
    // Sprint 2: LOD and generation tracking
    int requestedLod = -1;           // LOD level when requested
    uint64_t generation = 0;         // Visibility generation token
    double lastAccessTime = 0.0;     // For LRU cache eviction
    int priority = 0;                // Screen-space priority (higher = more important)
    
    // Upload epoch for stale upload prevention
    uint64_t uploadEpoch = 0;        // Monotonic epoch assigned at dispatch time
    
    // Sprint 2.3: Seamless rendering fade
    float fade = 1.0f;               // Current fade value (0.0 = invisible, 1.0 = fully visible)
    float fadeRate = 3.0f;           // Fade speed (units per second)
    int targetLod = -1;              // Target LOD level (for transitions)
    bool hasAncestorChildrenMask = false;  // True if children of this node are rendering
};

// Request struct for priority queue
struct MeshRequest {
    std::string nodeKey;
    int priority;
    int lod;
    uint64_t generation;
    
    bool operator<(const MeshRequest& other) const {
        // Higher priority first (for priority_queue)
        return priority < other.priority;
    }
};

// Manager for RockTree mesh loading
class RockMeshManager {
public:
    explicit RockMeshManager(const Config& config);
    ~RockMeshManager();
    
    // Initialize worker thread
    bool Init();
    
    // Shutdown (join worker)
    void Shutdown();
    
    // Sprint 1: Request a mesh by nodeKey (seed set)
    void Request(const std::string& nodeKey);
    
    // Sprint 2: Update visible tile set and generate mesh requests
    // Sprint 3: Added camera position for distance-based LOD selection
    void UpdateVisibleQuadKeys(const std::vector<TileKey>& visibleLeaves, 
                               const glm::dvec3& cameraPosEcef = glm::dvec3(0.0));
    
    // Sprint 2: Set current viewport generation (invalidates stale requests)
    void SetViewportVersion(uint64_t version);
    
    // Process pending uploads (call from main thread, GL context active)
    // Returns true if work was done
    bool ProcessUploads(double budgetMs);
    
    // Render all uploaded meshes
    // Phase 6: Added shaderProgram parameter for fade uniform
    // Faz 1C: Added useRte parameter for RTE/RTC jitter-free rendering
    void Render(GLuint shaderProgram = 0, bool useRte = true);
    
    // Sprint 3: Update fade values (call each frame before Render)
    void UpdateFades(float deltaTime);
    
    // Check if any work pending (for request-driven frame)
    bool HasPendingWork() const;
    // Returns the currently resolved GE dataset epoch for NodeData endpoints.
    // Empty string means no epoch is resolved yet.
    std::string GetResolvedEpoch() const;
    // Convenience check for epoch availability.
    bool HasResolvedEpoch() const;
    
    // Sprint 2: Check if any inflight work (fetching or uploading)
    bool HasInflightWork() const;
    
    // Get count of uploaded meshes
    size_t GetUploadedCount() const;
    
    // Sprint 2: Get active node keys snapshot (for debugging)
    std::vector<std::string> GetActiveNodeKeysSnapshot() const;
    
    // Sprint 2: Process priority queue (called from main thread)
    // Returns number of requests dispatched
    int ProcessPriorityQueue(double budgetMs);
    
    // Sprint 2: Get debug stats
    struct Stats {
        int requestedCount = 0;           // Total requests from visible set
        int enqueuedCount = 0;            // Successfully queued to requestQueue
        int droppedFullQueueCount = 0;    // Dropped due to full queue
        int dedupedCount = 0;             // Skipped (already requested)
        int priorityDispatchedCount = 0;  // Dispatched via priority queue
        int staleDropCount = 0;           // Dropped due to stale generation
        int generationDrops = 0;          // Worker dropped stale entries
        int uploadQueueDrops = 0;         // Failed to push to upload queue
        int uploadedCount = 0;            // Successfully uploaded to GPU
        int failureCount = 0;             // Failed (network, parse, build)
        int cachedCount = 0;              // Stale but cached for quick return
        int inFlightCount = 0;            // Currently fetching
        
        // Sprint 2.2: Disk cache metrics
        int diskCacheHits = 0;            // Served from disk cache
        int diskCacheMisses = 0;          // Network fetch required
        int diskCacheWrites = 0;          // Written to disk cache
        int diskCacheErrors = 0;          // Disk cache read/write errors
        
        // P0-P2: Vertex explosion mitigation discard counters
        int discardInvalidTransform = 0;      // Invalid transform matrix
        int discardInvalidScale = 0;          // Invalid/non-finite scale
        int discardInvalidBounds = 0;         // Invalid bounds
        int discardNonFiniteVertex = 0;       // Non-finite vertex positions
        int discardAabbExceeded = 0;          // AABB diagonal exceeds threshold
        int discardVertexDistanceExceeded = 0; // Vertex distance from origin exceeds threshold
        int fallbackTextureUsed = 0;          // Fallback texture used for invalid mesh
        
        // P0: Stale upload tracking
        int staleUploadSkips = 0;             // Uploads skipped due to stale token
        int staleUploadBytes = 0;             // Bytes of stale uploads skipped
        
        // HTTP error classification
        int failedHttp400Count = 0;           // Bad request (invalid node key format)
        int failedHttp404Count = 0;           // Not found (node has no mesh data)
        int failedHttpOtherCount = 0;         // Other HTTP errors
        int failedNetworkCount = 0;           // Network/timeout errors
        int blacklistSkipCount = 0;           // Skipped due to terminal error blacklist
    };
    Stats GetStats() const;
    
#ifdef NATIVE_GLOBE_TESTING
    // P0: Test-only API for direct BuildMesh testing (no GL/worker needed)
    RockMeshCpu BuildMeshForTest(const std::string& nodeKey, const ParsedNodeData& parsed);
#endif
    
private:
    const Config& config_;
    
    // State machine
    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, RockMeshEntry> entries_;
    
    // Sprint 2: LRU cache for stale entries (nodeKey list)
    std::list<std::string> lruList_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lruMap_;
    
    // Worker queue (nodeKeys to fetch)
    BoundedQueue<std::string> requestQueue_;
    
    // Upload queue (CPU meshes ready for GPU)
    BoundedQueue<RockMeshCpu> uploadQueue_;
    
    // Sprint 2: Priority queue for visible-based requests
    mutable std::mutex requestMutex_;
    std::priority_queue<MeshRequest> pendingRequests_;
    
    // Worker thread
    std::thread workerThread_;
    std::atomic<bool> shutdown_{false};
    
    // Sprint 2: Generation tracking
    std::atomic<uint64_t> currentGeneration_{0};
    
    // Sprint 2: Debug stats
    mutable std::mutex statsMutex_;
    Stats stats_;
    
    // Fallback texture (1x1 gray)
    GLuint fallbackTexture_ = 0;
    
    // Sprint 2.2: Disk cache for NodeData responses
    std::unique_ptr<NodeDataDiskCache> diskCache_;
    std::string cacheDir_;  // From config
    
    // Sprint 2.3: Global dedupe - tracks keys already queued or in-flight
    mutable std::mutex queuedMutex_;
    std::unordered_set<std::string> queuedOrPendingKeys_;
    
    // P0: Upload epoch tracking for stale callback prevention
    // Each node has a monotonically increasing epoch
    // Uploads with mismatched epochs are rejected as stale
    mutable std::mutex uploadEpochMutex_;
    std::unordered_map<std::string, uint64_t> uploadEpochByNode_;
    uint64_t nextUploadEpoch_ = 1;
    
    // Sprint 2.3: Final visible mesh keys after hierarchy processing
    std::unordered_set<std::string> visibleMeshKeys_;
    
    // Terminal error blacklist: keys that returned 400/404 get a cooldown
    // to avoid wasting bandwidth on permanently failing paths
    mutable std::mutex blacklistMutex_;
    std::unordered_map<std::string, double> blacklistedKeys_;  // nodeKey → expiry time (glfwGetTime)
    static constexpr double BLACKLIST_COOLDOWN_SEC = 300.0;    // 5 min cooldown for 400/404
    bool IsBlacklisted(const std::string& key) const;
    void BlacklistKey(const std::string& key);
    
    // Worker main loop
    void WorkerLoop();

    // Octree BulkMetadata worker loop
    void OctreeWorkerLoop();

    // Build CPU mesh from parsed data
    RockMeshCpu BuildMesh(const std::string& nodeKey, const ParsedNodeData& parsed);

    // Create fallback texture
    bool CreateFallbackTexture();
    
    // P0: Upload epoch management for stale callback prevention
    uint64_t AllocateUploadEpoch(const std::string& nodeKey);
    uint64_t InvalidateUploadEpoch(const std::string& nodeKey);
    bool IsUploadEpochCurrent(const std::string& nodeKey, uint64_t epoch) const;
    void RemoveUploadEpoch(const std::string& nodeKey);

    // Sprint 2: Helper methods
    std::string TileKeyToNodeKey(const TileKey& key) const;
    void MarkStaleEntries(const std::unordered_set<std::string>& visibleKeys);
    void UpdateLRU(const std::string& nodeKey);
    void EvictIfNeeded();

    // Sprint 2: In-flight tracking
    std::unordered_set<std::string> inFlightSet_;
    mutable std::mutex inFlightMutex_;

    // Octree discovery system
    std::shared_ptr<RockTreeOctreeIndex> octreeIndex_;
    std::unique_ptr<GeRateLimiter> rateLimiter_;
    std::thread octreeWorkerThread_;

    // Per-worker NodeDataClient epoch (set after octree init)
    // Thread-safe epoch storage (GE Octree integration)
    mutable std::mutex epochMutex_;
    std::string resolvedEpoch_;
};

} // namespace globe
