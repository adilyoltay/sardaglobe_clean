// RockMesh Manager
// Manages RockTree NodeData fetch, decode, and GPU upload
// Sprint 1: Single quadkey vertical slice
// Sprint 2: LOD-aware mesh management with visible set tracking

#pragma once

#include "rockmesh_runtime.h"
#include "../io/providers/google_earth_nodedata_client.h"
#include "../io/providers/rocktree_node_data_parser.h"
#include "../core/config.h"
#include "../core/bounded_queue.h"
#include "../core/tile_key.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <queue>
#include <list>

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
    void UpdateVisibleQuadKeys(const std::vector<TileKey>& visibleLeaves);
    
    // Sprint 2: Set current viewport generation (invalidates stale requests)
    void SetViewportVersion(uint64_t version);
    
    // Process pending uploads (call from main thread, GL context active)
    // Returns true if work was done
    bool ProcessUploads(double budgetMs);
    
    // Render all uploaded meshes
    void Render();
    
    // Check if any work pending (for request-driven frame)
    bool HasPendingWork() const;
    
    // Sprint 2: Check if any inflight work (fetching or uploading)
    bool HasInflightWork() const;
    
    // Get count of uploaded meshes
    size_t GetUploadedCount() const;
    
    // Sprint 2: Get active node keys snapshot (for debugging)
    std::vector<std::string> GetActiveNodeKeysSnapshot() const;
    
    // Sprint 2: Get debug stats
    struct Stats {
        int requestedCount = 0;
        int enqueuedCount = 0;
        int staleDropCount = 0;
        int uploadedCount = 0;
        int failureCount = 0;
        int cachedCount = 0;  // Stale but cached for quick return
    };
    Stats GetStats() const;
    
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
    
    // Worker main loop
    void WorkerLoop();
    
    // Build CPU mesh from parsed data
    RockMeshCpu BuildMesh(const std::string& nodeKey, const ParsedNodeData& parsed);
    
    // Create fallback texture
    bool CreateFallbackTexture();
    
    // Sprint 2: Helper methods
    std::string TileKeyToNodeKey(const TileKey& key) const;
    void MarkStaleEntries(const std::unordered_set<std::string>& visibleKeys);
    void ProcessPriorityQueue();
    void UpdateLRU(const std::string& nodeKey);
    void EvictIfNeeded();
};

} // namespace globe
