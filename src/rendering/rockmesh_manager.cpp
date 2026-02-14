// RockMesh Manager Implementation
// Sprint 1: Single quadkey vertical slice
// Sprint 2: LOD-aware mesh management
// Sprint 2.2: HTTP/2 + disk cache

#include "rockmesh_manager.h"
#include "../core/ellipsoid.h"
#include "../io/node_data_cache.h"
#include "../io/providers/google_earth_nodedata_client.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <chrono>
#include <iostream>
#include <algorithm>

namespace globe {

RockMeshManager::RockMeshManager(const Config& config)
    : config_(config),
      requestQueue_(config.geMeshMaxInFlight),
      uploadQueue_(8) {
}

RockMeshManager::~RockMeshManager() {
    Shutdown();
}

bool RockMeshManager::Init() {
    if (!CreateFallbackTexture()) {
        std::cerr << "[RockMesh] Failed to create fallback texture\n";
        return false;
    }
    
    // Sprint 2.2: Initialize disk cache if enabled
    if (config_.useDiskCache && !config_.cacheDir.empty()) {
        cacheDir_ = config_.cacheDir + "/ge_mesh";
        diskCache_ = std::make_unique<NodeDataDiskCache>(cacheDir_);
        if (!diskCache_->Init()) {
            std::cerr << "[RockMesh] Failed to initialize disk cache at " << cacheDir_ << "\n";
            diskCache_.reset();  // Continue without cache
        } else {
            std::cout << "[RockMesh] Disk cache initialized at " << cacheDir_ << "\n";
        }
    }
    
    workerThread_ = std::thread(&RockMeshManager::WorkerLoop, this);
    return true;
}

void RockMeshManager::Shutdown() {
    shutdown_ = true;
    requestQueue_.Close();
    uploadQueue_.Close();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    // Phase 6: Cleanup any remaining entries in non-terminal states
    // to prevent visual/log artifacts during shutdown
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& [id, entry] : entries_) {
            if (entry.state == RockMeshState::Fetching || 
                entry.state == RockMeshState::Queued) {
                entry.state = RockMeshState::Failed;
                entry.error = "Shutdown";
            }
        }
    }
    
    // Destroy all GPU meshes
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& [id, entry] : entries_) {
        entry.gpu.Destroy();
    }
    entries_.clear();
    
    // Destroy fallback texture
    if (fallbackTexture_) {
        glDeleteTextures(1, &fallbackTexture_);
        fallbackTexture_ = 0;
    }
}

// Sprint 2: Internal request - adds to priority queue, not directly to worker queue
void RockMeshManager::Request(const std::string& nodeKey) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto it = entries_.find(nodeKey);
    if (it != entries_.end()) {
        // Already known - update generation and access time
        it->second.generation = currentGeneration_.load();
        it->second.lastAccessTime = glfwGetTime();
        UpdateLRU(nodeKey);
        
        // If already uploaded or in-flight, no need to re-queue
        if (it->second.state == RockMeshState::Uploaded ||
            it->second.state == RockMeshState::Fetching ||
            it->second.state == RockMeshState::Queued) {
            return;
        }
        
        // If stale/failed, reset to queue again
        if (it->second.state == RockMeshState::Stale ||
            it->second.state == RockMeshState::Failed) {
            it->second.state = RockMeshState::Queued;
            it->second.error.clear();
        }
    } else {
        // New entry
        RockMeshEntry entry;
        entry.state = RockMeshState::Queued;
        entry.generation = currentGeneration_.load();
        entry.lastAccessTime = glfwGetTime();
        entry.priority = 0;  // Will be set by UpdateVisibleQuadKeys
        entries_[nodeKey] = std::move(entry);
        UpdateLRU(nodeKey);
    }
}

// Sprint 2: Update visible tile set and generate mesh requests
// Sprint 2.3: Added global dedupe via queuedOrPendingKeys_
// Sprint 3: Added camera position for distance-based LOD selection
void RockMeshManager::UpdateVisibleQuadKeys(const std::vector<TileKey>& visibleLeaves,
                                            const glm::dvec3& cameraPosEcef) {
    uint64_t generation = currentGeneration_.load();
    std::unordered_set<std::string> visibleKeys;
    visibleKeys.reserve(visibleLeaves.size() * 2);  // Parent + self
    
    // Generate candidate keys from visible leaves plus margin
    int childRequestsThisFrame = 0;
    for (const auto& leaf : visibleLeaves) {
        // Add the leaf itself
        visibleKeys.insert(TileKeyToNodeKey(leaf));
        
        // Add parent if within margin
        if (leaf.level > 0 && config_.geMeshMaxLodMargin > 0) {
            visibleKeys.insert(TileKeyToNodeKey(leaf.Parent()));
        }
        
        // Sprint 3: Child-LOD proximity selection with distance check
        // Request children for high-detail tiles near camera
        // Phase 6: Clamp to max zoom bounds to prevent over-expansion
        if (config_.geMeshEnableChildLod && 
            leaf.level >= 10 &&  // Only for detailed tiles
            leaf.level < config_.maxZoom &&  // Don't expand beyond max zoom
            childRequestsThisFrame < config_.geMeshMaxChildRequestsPerFrame) {
            
            // Calculate distance from camera to tile center
            // Phase 6 Fix: Both cameraPosEcef and CenterEcef() are in km
            bool isNearCamera = true;  // Default to true if no camera position
            if (cameraPosEcef != glm::dvec3(0.0)) {
                glm::dvec3 tileCenter = leaf.CenterEcef();  // km
                double distMeters = glm::length(tileCenter - cameraPosEcef) * 1000.0;  // km -> m
                isNearCamera = distMeters <= config_.geMeshChildLodDistance;
            }
            
            if (isNearCamera) {
                auto children = leaf.Children();
                for (const auto& child : children) {
                    std::string childKey = TileKeyToNodeKey(child);
                    if (visibleKeys.find(childKey) == visibleKeys.end()) {
                        visibleKeys.insert(childKey);
                        childRequestsThisFrame++;
                    }
                }
            }
        }
    }
    
    // Sprint 2.3: Update final visible mesh keys
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        visibleMeshKeys_ = visibleKeys;
    }
    
    // Mark entries not in visible set as stale
    MarkStaleEntries(visibleKeys);
    
    // Add visible keys to priority queue (not directly to requestQueue)
    int requested = 0;
    int deduped = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        std::lock_guard<std::mutex> reqLock(requestMutex_);
        std::lock_guard<std::mutex> queuedLock(queuedMutex_);
        
        for (const auto& key : visibleKeys) {
            // Sprint 2.3: Check global dedupe set first
            if (queuedOrPendingKeys_.find(key) != queuedOrPendingKeys_.end()) {
                // Already queued or in-flight - update generation
                auto it = entries_.find(key);
                if (it != entries_.end()) {
                    it->second.generation = generation;
                }
                deduped++;
                continue;
            }
            
            // Check existing entries for state
            auto it = entries_.find(key);
            if (it != entries_.end()) {
                if (it->second.state == RockMeshState::Fetching ||
                    it->second.state == RockMeshState::Uploaded) {
                    // Update generation for existing entry
                    it->second.generation = generation;
                    deduped++;
                    continue;
                }
            }
            
            // Create entry if needed
            if (it == entries_.end()) {
                RockMeshEntry entry;
                entry.state = RockMeshState::Queued;
                entry.generation = generation;
                entry.lastAccessTime = glfwGetTime();
                entry.priority = static_cast<int>(visibleLeaves.size()) - requested;
                entries_[key] = std::move(entry);
                UpdateLRU(key);
            } else {
                it->second.state = RockMeshState::Queued;
                it->second.generation = generation;
            }
            
            // Add to global dedupe set
            queuedOrPendingKeys_.insert(key);
            
            // Add to priority queue
            MeshRequest req;
            req.nodeKey = key;
            req.priority = static_cast<int>(visibleLeaves.size()) - requested;
            req.lod = 0;
            req.generation = generation;
            pendingRequests_.push(req);
            requested++;
        }
    }
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.requestedCount += requested + deduped;
        stats_.dedupedCount += deduped;
    }
}

// Sprint 2: Process priority queue and dispatch to worker
int RockMeshManager::ProcessPriorityQueue(double budgetMs) {
    auto start = std::chrono::steady_clock::now();
    int dispatched = 0;
    uint64_t currentGen = currentGeneration_.load();
    
    while (true) {
        // Check budget
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - start).count();
        if (elapsed >= budgetMs) {
            break;
        }
        
        // Check in-flight limit
        {
            std::lock_guard<std::mutex> lock(inFlightMutex_);
            if (static_cast<int>(inFlightSet_.size()) >= config_.geMeshMaxInFlight) {
                break;
            }
        }
        
        // Get next request from priority queue
        MeshRequest req;
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            if (pendingRequests_.empty()) {
                break;
            }
            req = pendingRequests_.top();
            pendingRequests_.pop();
        }
        
        // Check generation (skip stale)
        if (req.generation < currentGen - 1) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.staleDropCount++;
            // Phase 6 Fix: Remove from dedupe set so visible tiles can be re-queued
            {
                std::lock_guard<std::mutex> queuedLock(queuedMutex_);
                queuedOrPendingKeys_.erase(req.nodeKey);
            }
            continue;
        }
        
        // Check if still valid in entries
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(req.nodeKey);
            if (it == entries_.end() || 
                (it->second.state != RockMeshState::Queued &&
                 it->second.state != RockMeshState::Stale)) {
                // Phase 6 Fix: Remove from dedupe set
                {
                    std::lock_guard<std::mutex> queuedLock(queuedMutex_);
                    queuedOrPendingKeys_.erase(req.nodeKey);
                }
                continue;
            }
            
            // Update state to Fetching
            it->second.state = RockMeshState::Fetching;
            it->second.generation = req.generation;
        }
        
        // Try non-blocking push to requestQueue
        if (!requestQueue_.TryPush(req.nodeKey)) {
            // Queue full - revert state and requeue for later
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto it = entries_.find(req.nodeKey);
                if (it != entries_.end()) {
                    // Revert to Queued (or Stale if generation outdated)
                    if (req.generation < currentGeneration_.load() - 1) {
                        it->second.state = RockMeshState::Stale;
                    } else {
                        it->second.state = RockMeshState::Queued;
                    }
                }
            }
            // Add back to priority queue with slightly lower priority
            // Phase 6: Key stays in queuedOrPendingKeys_ to prevent re-queueing by UpdateVisibleQuadKeys
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                MeshRequest retryReq = req;
                retryReq.priority -= 10;  // Lower priority for retry
                pendingRequests_.push(retryReq);
            }
            std::lock_guard<std::mutex> statsLock(statsMutex_);
            stats_.droppedFullQueueCount++;
            continue;
        }
        
        // Track in-flight and enqueued
        {
            std::lock_guard<std::mutex> lock(inFlightMutex_);
            inFlightSet_.insert(req.nodeKey);
        }
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.enqueuedCount++;
        }
        
        // Sprint 2.3: Remove from global dedupe (now tracked by inFlightSet_)
        {
            std::lock_guard<std::mutex> queuedLock(queuedMutex_);
            queuedOrPendingKeys_.erase(req.nodeKey);
        }
        
        dispatched++;
    }
    
    // Update stats (with proper locking)
    if (dispatched > 0) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.priorityDispatchedCount += dispatched;
    }
    // inFlightCount is calculated on-demand in GetStats()
    
    return dispatched;
}

// Sprint 2: Set current viewport generation
void RockMeshManager::SetViewportVersion(uint64_t version) {
    currentGeneration_.store(version);
}

bool RockMeshManager::ProcessUploads(double budgetMs) {
    auto start = std::chrono::steady_clock::now();
    bool didWork = false;
    int uploaded = 0;
    int failed = 0;
    
    RockMeshCpu cpu;
    while (uploadQueue_.TryPop(cpu)) {
        // Always remove from in-flight at the end, regardless of outcome
        struct InFlightGuard {
            std::unordered_set<std::string>* set;
            std::mutex* mutex;
            std::string id;
            bool cleared = false;
            
            void clear() {
                if (!cleared && set && mutex) {
                    std::lock_guard<std::mutex> lock(*mutex);
                    set->erase(id);
                    cleared = true;
                }
            }
            ~InFlightGuard() { clear(); }
        } guard;
        guard.set = &inFlightSet_;
        guard.mutex = &inFlightMutex_;
        guard.id = cpu.id;
        
        if (!cpu.valid) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = cpu.error.empty() ? "Build failed" : cpu.error;
            }
            failed++;
            didWork = true;
            continue;  // guard destructor will clean in-flight
        }
        
        // Check generation - drop stale uploads
        uint64_t currentGen = currentGeneration_.load();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end() && it->second.generation < currentGen - 2) {
                // Stale - drop silently
                it->second.state = RockMeshState::Stale;
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.staleDropCount++;
                }
                continue;  // guard destructor will clean in-flight
            }
        }
        
        // Create GPU mesh
        RockMeshGpu gpu;
        if (gpu.Create(cpu, fallbackTexture_)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.gpu = std::move(gpu);
                it->second.state = RockMeshState::Uploaded;
                // Sprint 3: Start with fade=0 for seamless transition
                it->second.fade = 0.0f;
                uploaded++;
            }
            guard.clear();  // Explicit clear for success path
        } else {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "GPU upload failed";
                failed++;
            }
            guard.clear();  // Explicit clear for failure path
        }
        
        didWork = true;
        
        // Check budget
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - start).count();
        if (elapsed >= budgetMs) {
            break;
        }
    }
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.uploadedCount += uploaded;
        stats_.failureCount += failed;
    }
    
    return didWork;
}

// Sprint 3: Per-frame fade update
void RockMeshManager::UpdateFades(float deltaTime) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Uploaded) {
            // Fade in new meshes
            if (entry.fade < 1.0f) {
                entry.fade += entry.fadeRate * deltaTime;
                if (entry.fade > 1.0f) entry.fade = 1.0f;
            }
        } else if (entry.state == RockMeshState::Stale) {
            // Fade out stale meshes
            if (entry.fade > 0.0f) {
                entry.fade -= entry.fadeRate * deltaTime;
                if (entry.fade < 0.0f) entry.fade = 0.0f;
            }
        }
    }
}

// Phase 6: Added shaderProgram parameter for per-mesh fade
void RockMeshManager::Render(GLuint shaderProgram) {
    // Sprint 3: Snapshot pattern with fade support
    struct DrawCmd {
        GLuint vao;
        GLuint texture;
        uint32_t indexCount;
        float fade;
    };
    std::vector<DrawCmd> cmds;
    
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        cmds.reserve(entries_.size());
        
        for (const auto& [id, entry] : entries_) {
            // Sprint 3: Render if uploaded and visible (fade > 0)
            if ((entry.state == RockMeshState::Uploaded || entry.state == RockMeshState::Stale) 
                && entry.gpu.valid && entry.fade > 0.01f) {
                cmds.push_back({
                    entry.gpu.vao,
                    entry.gpu.texture,
                    entry.gpu.indexCount,
                    entry.fade
                });
                // Update access time for LRU
                const_cast<RockMeshEntry&>(entry).lastAccessTime = glfwGetTime();
            }
        }
    }
    
    // Phase 6: Get fade uniform location if shader program provided
    GLint fadeLoc = -1;
    if (shaderProgram != 0) {
        fadeLoc = glGetUniformLocation(shaderProgram, "uFade");
    }
    
    // Draw outside lock
    for (const auto& cmd : cmds) {
        if (cmd.vao == 0 || cmd.indexCount == 0) continue;
        
        // Phase 6: Set per-mesh fade uniform
        if (fadeLoc >= 0) {
            glUniform1f(fadeLoc, cmd.fade);
        }
        
        glBindVertexArray(cmd.vao);
        if (cmd.texture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, cmd.texture);
        }
        glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

bool RockMeshManager::HasPendingWork() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Queued ||
            entry.state == RockMeshState::Fetching ||
            entry.state == RockMeshState::ReadyToUpload) {
            return true;
        }
    }
    return false;
}

// Sprint 2: Check inflight work
bool RockMeshManager::HasInflightWork() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Fetching) {
            return true;
        }
    }
    return !requestQueue_.Empty() || !uploadQueue_.Empty();
}

size_t RockMeshManager::GetUploadedCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    size_t count = 0;
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Uploaded) {
            ++count;
        }
    }
    return count;
}

// Sprint 2: Get active node keys snapshot
std::vector<std::string> RockMeshManager::GetActiveNodeKeysSnapshot() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    std::vector<std::string> keys;
    keys.reserve(entries_.size());
    
    for (const auto& [id, entry] : entries_) {
        if (entry.state != RockMeshState::None && 
            entry.state != RockMeshState::Stale) {
            keys.push_back(id);
        }
    }
    return keys;
}

// Sprint 2: Get debug stats
RockMeshManager::Stats RockMeshManager::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    Stats s = stats_;
    s.cachedCount = static_cast<int>(lruList_.size());
    // Calculate in-flight count on-demand (proper locking order)
    {
        std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
        s.inFlightCount = static_cast<int>(inFlightSet_.size());
    }
    return s;
}

void RockMeshManager::WorkerLoop() {
    // Create client for this thread
    GoogleEarthNodeDataClient client(config_);
    
    if (!client.IsEnabled()) {
        std::cerr << "[RockMesh] Worker: client not enabled\n";
        return;
    }
    
    std::string nodeKey;
    while (requestQueue_.Pop(nodeKey)) {
        if (shutdown_) break;
        
        // Check generation - skip stale requests
        uint64_t currentGen = currentGeneration_.load();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                if (it->second.generation < currentGen - 2) {
                    // Stale - mark and skip
                    it->second.state = RockMeshState::Stale;
                    {
                        std::lock_guard<std::mutex> statsLock(statsMutex_);
                        stats_.generationDrops++;
                    }
                    // Remove from in-flight
                    std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                    inFlightSet_.erase(nodeKey);
                    continue;
                }
                it->second.state = RockMeshState::Fetching;
            }
        }
        
        // Sprint 2.2: Try disk cache first
        NodeDataResult fetchResult;
        bool cacheHit = false;
        
        if (diskCache_) {
            std::vector<uint8_t> cachedData = diskCache_->Read(config_.geMeshEndpoint, nodeKey);
            if (!cachedData.empty()) {
                fetchResult.success = true;
                fetchResult.data = std::move(cachedData);
                fetchResult.fromCache = true;
                cacheHit = true;
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.diskCacheHits++;
                }
            } else {
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.diskCacheMisses++;
                }
            }
        }
        
        // Fetch from network if cache miss
        if (!cacheHit) {
            fetchResult = client.FetchNodeData(nodeKey);
        }
        if (!fetchResult.success) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = fetchResult.errorMessage;
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.failureCount++;
            }
            // Remove from in-flight
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            continue;
        }
        
        // Sprint 2.2: Write to disk cache on network success (not from cache)
        if (!cacheHit && diskCache_ && !fetchResult.data.empty()) {
            if (diskCache_->Write(config_.geMeshEndpoint, nodeKey, fetchResult.data)) {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.diskCacheWrites++;
            } else {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.diskCacheErrors++;
            }
        }
        
        // Check generation again after fetch
        currentGen = currentGeneration_.load();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end() && it->second.generation < currentGen - 2) {
                it->second.state = RockMeshState::Stale;
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.generationDrops++;
                }
                // Remove from in-flight
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
                continue;
            }
        }
        
        // Parse
        ParsedNodeData parsed = RockTreeNodeDataParser::Parse(fetchResult.data);
        if (!parsed.success) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = parsed.error;
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.failureCount++;
            }
            // Remove from in-flight
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            continue;
        }
        
        // Build CPU mesh
        RockMeshCpu cpu = BuildMesh(nodeKey, parsed);
        
        if (!cpu.valid) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = cpu.error.empty() ? "BuildMesh failed" : cpu.error;
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.failureCount++;
            }
            // Remove from in-flight
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            continue;
        }
        
        // Queue for upload
        if (!uploadQueue_.Push(std::move(cpu))) {
            // Upload queue full or closed
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "Upload queue full or closed";
            }
            std::cerr << "[RockMesh] Failed to queue upload for " << nodeKey << "\n";
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.uploadQueueDrops++;
                stats_.failureCount++;
            }
            // Remove from in-flight
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
        }
        // Note: Successful uploads will have in-flight removed in ProcessUploads
    }
}

RockMeshCpu RockMeshManager::BuildMesh(const std::string& nodeKey, const ParsedNodeData& parsed) {
    RockMeshCpu cpu;
    cpu.id = nodeKey;
    
    // Validate
    if (!parsed.hasTransform) {
        cpu.error = "No transform matrix";
        return cpu;
    }
    if (parsed.vertexCount <= 0) {
        cpu.error = "No vertices";
        return cpu;
    }
    if (parsed.indices.empty() || parsed.indices.size() % 3 != 0) {
        cpu.error = "Invalid indices";
        return cpu;
    }
    
    const int V = parsed.vertexCount;
    const int T = parsed.triangleCount;
    
    // Build transform matrix (column-major to glm)
    glm::dmat4 M;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            M[col][row] = parsed.transform[col * 4 + row];
        }
    }
    
    // Extract translation and compute scale
    glm::dvec3 t(M[3][0], M[3][1], M[3][2]);
    double tLen = glm::length(t);
    if (tLen < 1e-10) {
        cpu.error = "Invalid transform: translation vector too small";
        return cpu;
    }
    double kmPerRockUnit = Ellipsoid::WGS84_A_KM / tLen;
    
    // Resize output
    cpu.vertices.resize(V * 9);  // stride 9
    cpu.indices = parsed.indices;
    cpu.triangleCount = T;
    
    // Build positions
    for (int i = 0; i < V; ++i) {
        // Local position (int16 / 32768)
        double lx = parsed.positions[i * 3 + 0] / 32768.0;
        double ly = parsed.positions[i * 3 + 1] / 32768.0;
        double lz = parsed.positions[i * 3 + 2] / 32768.0;
        
        // Transform to world (km)
        glm::dvec4 local(lx, ly, lz, 1.0);
        glm::dvec3 world = glm::dvec3(M * local) * kmPerRockUnit;
        
        cpu.vertices[i * 9 + 0] = static_cast<float>(world.x);
        cpu.vertices[i * 9 + 1] = static_cast<float>(world.y);
        cpu.vertices[i * 9 + 2] = static_cast<float>(world.z);
    }
    
    // Build UVs
    if (parsed.hasUvQuant && !parsed.uv.empty()) {
        for (int i = 0; i < V; ++i) {
            uint16_t u16 = parsed.uv[i * 2 + 0];
            uint16_t v16 = parsed.uv[i * 2 + 1];
            float u, v;
            parsed.uvQuant.Decode(u16, v16, u, v, config_.geMeshFlipV);
            cpu.vertices[i * 9 + 6] = u;
            cpu.vertices[i * 9 + 7] = v;
        }
    } else {
        // Default UVs
        for (int i = 0; i < V; ++i) {
            cpu.vertices[i * 9 + 6] = 0.0f;
            cpu.vertices[i * 9 + 7] = 0.0f;
        }
    }
    
    // Build normals (from triangle list)
    std::vector<glm::vec3> normals(V, glm::vec3(0.0f));
    std::vector<int> normalCounts(V, 0);
    
    for (int t = 0; t < T; ++t) {
        uint32_t i0 = parsed.indices[t * 3 + 0];
        uint32_t i1 = parsed.indices[t * 3 + 1];
        uint32_t i2 = parsed.indices[t * 3 + 2];
        
        if (i0 >= V || i1 >= V || i2 >= V) continue;  // Safety
        
        glm::vec3 p0(cpu.vertices[i0 * 9 + 0], cpu.vertices[i0 * 9 + 1], cpu.vertices[i0 * 9 + 2]);
        glm::vec3 p1(cpu.vertices[i1 * 9 + 0], cpu.vertices[i1 * 9 + 1], cpu.vertices[i1 * 9 + 2]);
        glm::vec3 p2(cpu.vertices[i2 * 9 + 0], cpu.vertices[i2 * 9 + 1], cpu.vertices[i2 * 9 + 2]);
        
        glm::vec3 e0 = p1 - p0;
        glm::vec3 e1 = p2 - p0;
        glm::vec3 cross = glm::cross(e0, e1);
        float crossLen2 = glm::dot(cross, cross);
        
        // Skip degenerate triangles
        if (crossLen2 < 1e-10f) continue;
        
        glm::vec3 n = glm::normalize(cross);
        
        // Outward enforce
        glm::vec3 centerPos = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(n, centerPos) < 0.0f) {
            n = -n;
        }
        
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
        normalCounts[i0]++;
        normalCounts[i1]++;
        normalCounts[i2]++;
    }
    
    for (int i = 0; i < V; ++i) {
        if (normalCounts[i] > 0) {
            glm::vec3 n = glm::normalize(normals[i] / static_cast<float>(normalCounts[i]));
            cpu.vertices[i * 9 + 3] = n.x;
            cpu.vertices[i * 9 + 4] = n.y;
            cpu.vertices[i * 9 + 5] = n.z;
        } else {
            cpu.vertices[i * 9 + 3] = 0.0f;
            cpu.vertices[i * 9 + 4] = 0.0f;
            cpu.vertices[i * 9 + 5] = 1.0f;
        }
    }
    
    // HeightKm (always 0 for rockmesh)
    for (int i = 0; i < V; ++i) {
        cpu.vertices[i * 9 + 8] = 0.0f;
    }
    
    // Decode texture
    if (parsed.texture.valid && !parsed.texture.jpegBytes.empty()) {
        stbi_set_flip_vertically_on_load_thread(0);  // We handle flip in UV
        
        int w, h, channels;
        uint8_t* pixels = stbi_load_from_memory(
            parsed.texture.jpegBytes.data(),
            static_cast<int>(parsed.texture.jpegBytes.size()),
            &w, &h, &channels, 4  // Force RGBA
        );
        
        if (pixels && w > 0 && h > 0) {
            // Size validation: max 8192x8192, prevent overflow
            constexpr int MAX_TEX_SIZE = 8192;
            if (w <= MAX_TEX_SIZE && h <= MAX_TEX_SIZE) {
                size_t numPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
                size_t bytesNeeded = numPixels * 4;
                // Check for overflow (simple: if w*h would overflow, bytesNeeded would be wrong)
                if (bytesNeeded / 4 == numPixels) {
                    cpu.rgba.resize(bytesNeeded);
                    memcpy(cpu.rgba.data(), pixels, bytesNeeded);
                    cpu.texWidth = w;
                    cpu.texHeight = h;
                    cpu.hasTexture = true;
                }
            }
            stbi_image_free(pixels);
        }
    }
    
    cpu.valid = true;
    return cpu;
}

bool RockMeshManager::CreateFallbackTexture() {
    glGenTextures(1, &fallbackTexture_);
    if (!fallbackTexture_) return false;
    
    glBindTexture(GL_TEXTURE_2D, fallbackTexture_);
    
    // 1x1 gray pixel
    uint8_t gray[4] = {128, 128, 128, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// Sprint 2: Helper methods
// Sprint 2.3: Use canonical TileKey::ToQuadKey() for consistency
std::string RockMeshManager::TileKeyToNodeKey(const TileKey& key) const {
    return key.ToQuadKey();
}

void RockMeshManager::MarkStaleEntries(const std::unordered_set<std::string>& visibleKeys) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (auto& [nodeKey, entry] : entries_) {
        if (visibleKeys.find(nodeKey) == visibleKeys.end()) {
            // Not in visible set
            if (entry.state == RockMeshState::Uploaded) {
                // Mark as stale for LRU eviction
                entry.state = RockMeshState::Stale;
            } else if (entry.state == RockMeshState::Queued || 
                       entry.state == RockMeshState::Fetching) {
                // Will be dropped by generation check in worker
            }
        }
    }
    
    // Evict old stale entries if over cache size limit
    EvictIfNeeded();
}

void RockMeshManager::UpdateLRU(const std::string& nodeKey) {
    // Assumes stateMutex_ is held by caller
    auto it = lruMap_.find(nodeKey);
    if (it != lruMap_.end()) {
        // Move to front (most recently used)
        lruList_.erase(it->second);
        lruList_.push_front(nodeKey);
        it->second = lruList_.begin();
    } else {
        // Add new entry
        lruList_.push_front(nodeKey);
        lruMap_[nodeKey] = lruList_.begin();
    }
}

void RockMeshManager::EvictIfNeeded() {
    // Assumes stateMutex_ is held by caller
    if (entries_.size() <= static_cast<size_t>(config_.geMeshCacheSize)) {
        return;
    }
    
    // Evict oldest stale entries first
    int toEvict = static_cast<int>(entries_.size()) - config_.geMeshCacheSize;
    auto it = lruList_.rbegin();
    
    while (toEvict > 0 && it != lruList_.rend()) {
        const std::string& nodeKey = *it;
        auto entryIt = entries_.find(nodeKey);
        
        if (entryIt != entries_.end() && entryIt->second.state == RockMeshState::Stale) {
            // Destroy GPU resources
            entryIt->second.gpu.Destroy();
            // Remove from maps
            lruMap_.erase(nodeKey);
            entries_.erase(entryIt);
            // Remove from LRU list (need to convert reverse iterator)
            it = std::reverse_iterator<std::list<std::string>::iterator>(
                lruList_.erase(std::next(it).base()));
            toEvict--;
        } else {
            ++it;
        }
    }
}

} // namespace globe
