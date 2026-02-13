// RockMesh Manager
// Manages RockTree NodeData fetch, decode, and GPU upload
// Sprint 1: Single quadkey vertical slice

#pragma once

#include "rockmesh_runtime.h"
#include "../io/providers/google_earth_nodedata_client.h"
#include "../io/providers/rocktree_node_data_parser.h"
#include "../core/config.h"
#include "../core/bounded_queue.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace globe {

// State machine for mesh loading
enum class RockMeshState {
    None,
    Queued,
    Fetching,
    ReadyToUpload,
    Uploaded,
    Failed
};

struct RockMeshEntry {
    RockMeshState state = RockMeshState::None;
    RockMeshGpu gpu;
    std::string error;
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
    
    // Request a mesh by nodeKey (Sprint 1: from CLI quadkeys)
    void Request(const std::string& nodeKey);
    
    // Process pending uploads (call from main thread, GL context active)
    // Returns true if work was done
    bool ProcessUploads(double budgetMs);
    
    // Render all uploaded meshes
    void Render();
    
    // Check if any work pending (for request-driven frame)
    bool HasPendingWork() const;
    
    // Get count of uploaded meshes
    size_t GetUploadedCount() const;
    
private:
    const Config& config_;
    
    // State machine
    mutable std::mutex stateMutex_;
    std::unordered_map<std::string, RockMeshEntry> entries_;
    
    // Worker queue (nodeKeys to fetch)
    BoundedQueue<std::string> requestQueue_;
    
    // Upload queue (CPU meshes ready for GPU)
    BoundedQueue<RockMeshCpu> uploadQueue_;
    
    // Worker thread
    std::thread workerThread_;
    std::atomic<bool> shutdown_{false};
    
    // Fallback texture (1x1 gray)
    GLuint fallbackTexture_ = 0;
    
    // Worker main loop
    void WorkerLoop();
    
    // Build CPU mesh from parsed data
    RockMeshCpu BuildMesh(const std::string& nodeKey, const ParsedNodeData& parsed);
    
    // Create fallback texture
    bool CreateFallbackTexture();
};

} // namespace globe
