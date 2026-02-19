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

    // Create rate limiter
    rateLimiter_ = std::make_unique<GeRateLimiter>(config_.geRateLimitMs);

    // Initialize octree index asynchronously (non-blocking)
    // Octree init makes HTTP requests to kh.google.com which can be slow
    if (config_.geOctreeEnabled) {
        octreeIndex_ = std::make_shared<RockTreeOctreeIndex>(config_, rateLimiter_.get());
        std::cout << "[RockMesh] Starting octree discovery (async)...\n";
        octreeWorkerThread_ = std::thread(&RockMeshManager::OctreeWorkerLoop, this);
    }

    // Use config epoch as initial fallback (will be updated after octree init)
    if (!config_.geEpoch.empty()) {
        std::lock_guard<std::mutex> lock(epochMutex_);
        resolvedEpoch_ = config_.geEpoch;
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

    // Join octree worker
    if (octreeWorkerThread_.joinable()) {
        octreeWorkerThread_.join();
    }
    
    // Phase 6.4: Cleanup any remaining entries in non-terminal states
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
    
    // Phase 6.4: Clear all tracking sets for deterministic state
    {
        std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
        inFlightSet_.clear();
    }
    {
        std::lock_guard<std::mutex> queuedLock(queuedMutex_);
        queuedOrPendingKeys_.clear();
    }
    // Clear upload epoch tracking
    {
        std::lock_guard<std::mutex> epochLock(uploadEpochMutex_);
        uploadEpochByNode_.clear();
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

std::string RockMeshManager::GetResolvedEpoch() const {
    std::lock_guard<std::mutex> lock(epochMutex_);
    return resolvedEpoch_;
}

bool RockMeshManager::HasResolvedEpoch() const {
    std::lock_guard<std::mutex> lock(epochMutex_);
    return !resolvedEpoch_.empty();
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

    // Octree mode: use deterministic octree path mapping
    if (octreeIndex_ && octreeIndex_->IsInitialized()) {
        for (const auto& leaf : visibleLeaves) {
            // Map TMS tile to approximate octree paths
            std::string tileQK = TileKeyToNodeKey(leaf);
            auto candidates = octreeIndex_->TileQuadKeyToOctreePaths(tileQK);
            for (const auto& path : candidates) {
                if (octreeIndex_->HasNodeData(path)) {
                    visibleKeys.insert(path);
                }

                // Request BulkMetadata for deeper exploration if not yet fetched
                if (!octreeIndex_->IsBulkMetadataFetched(path) &&
                    path.length() >= 2) {
                    // Request BulkMetadata for the parent prefix that covers this depth
                    std::string prefix = path.substr(0, std::min(path.length(), size_t(4)));
                    octreeIndex_->RequestBulkMetadata(prefix);
                }

                // Get children with data for close tiles
                if (config_.geMeshEnableChildLod &&
                    leaf.level >= 10 &&
                    childRequestsThisFrame < config_.geMeshMaxChildRequestsPerFrame) {
                    auto children = octreeIndex_->GetChildrenWithData(path);
                    for (const auto& childPath : children) {
                        if (visibleKeys.find(childPath) == visibleKeys.end()) {
                            visibleKeys.insert(childPath);
                            childRequestsThisFrame++;
                        }
                    }
                }
            }

            // Also add parent-level octree nodes
            if (leaf.level > 0 && config_.geMeshMaxLodMargin > 0) {
                std::string parentQK = TileKeyToNodeKey(leaf.Parent());
                auto parentCandidates = octreeIndex_->TileQuadKeyToOctreePaths(parentQK);
                for (const auto& path : parentCandidates) {
                    if (octreeIndex_->HasNodeData(path)) {
                        visibleKeys.insert(path);
                    }
                }
            }
        }
    } else {
        // Legacy mode: direct TMS quadkey mapping
        for (const auto& leaf : visibleLeaves) {
            // Add the leaf itself
            visibleKeys.insert(TileKeyToNodeKey(leaf));

            // Add parent if within margin
            if (leaf.level > 0 && config_.geMeshMaxLodMargin > 0) {
                visibleKeys.insert(TileKeyToNodeKey(leaf.Parent()));
            }

            // Sprint 3: Child-LOD proximity selection with distance check
            if (config_.geMeshEnableChildLod &&
                leaf.level >= 10 &&
                leaf.level < config_.maxZoom &&
                childRequestsThisFrame < config_.geMeshMaxChildRequestsPerFrame) {

                bool isNearCamera = true;
                if (cameraPosEcef != glm::dvec3(0.0)) {
                    glm::dvec3 tileCenter = leaf.CenterEcef();
                    double distMeters = glm::length(tileCenter - cameraPosEcef) * 1000.0;
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
            
            // Update state to Fetching and allocate upload epoch
            it->second.state = RockMeshState::Fetching;
            it->second.generation = req.generation;
            it->second.uploadEpoch = AllocateUploadEpoch(req.nodeKey);
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
        
        // Check upload epoch - drop if a newer request has been dispatched for this key
        if (cpu.uploadEpoch != 0 && !IsUploadEpochCurrent(cpu.id, cpu.uploadEpoch)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Stale;
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.staleUploadSkips++;
                stats_.staleUploadBytes += static_cast<int>(
                    cpu.vertices.size() * sizeof(float) +
                    cpu.indices.size() * sizeof(uint32_t) +
                    cpu.rgba.size());
            }
            didWork = true;
            continue;  // guard destructor will clean in-flight
        }
        
        // Create GPU mesh
        if (config_.rockMeshRuntimeDebug) {
            std::cout << "[RockMesh:Upload] GPU CREATE: " << cpu.id
                      << " verts=" << cpu.vertices.size() / 9
                      << " idx=" << cpu.indices.size()
                      << " tex=" << (cpu.hasTexture ? "yes" : "no")
                      << " rgba=" << cpu.rgba.size() << "B\n";
        }
        RockMeshGpu gpu;
        if (gpu.Create(cpu, fallbackTexture_)) {
            if (config_.rockMeshRuntimeDebug) {
                std::cout << "[RockMesh:Upload] GPU OK: " << cpu.id << "\n";
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.gpu = std::move(gpu);
                it->second.state = RockMeshState::Uploaded;
                // Sprint 3: Start with fade=0 for seamless transition
                it->second.fade = 0.0f;
                uploaded++;
                // P1: Track fallback texture usage
                if (!cpu.hasTexture) {
                    stats_.fallbackTextureUsed++;
                }
            }
            guard.clear();  // Explicit clear for success path
        } else {
            // GPU failures always log (rare, indicates real problem)
            std::cerr << "[RockMesh:Upload] GPU FAILED: " << cpu.id << "\n";
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

// Terminal error blacklist helpers
bool RockMeshManager::IsBlacklisted(const std::string& key) const {
    std::lock_guard<std::mutex> lock(blacklistMutex_);
    auto it = blacklistedKeys_.find(key);
    if (it == blacklistedKeys_.end()) return false;
    return glfwGetTime() < it->second;  // Still within cooldown
}

void RockMeshManager::BlacklistKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(blacklistMutex_);
    blacklistedKeys_[key] = glfwGetTime() + BLACKLIST_COOLDOWN_SEC;
}

// Phase 6: Added shaderProgram parameter for per-mesh fade
void RockMeshManager::Render(GLuint shaderProgram, bool useRte) {
    // Sprint 3: Snapshot pattern with fade support
    // Faz 1C: Added RTE origin for jitter-free rendering
    struct DrawCmd {
        GLuint vao;
        GLuint texture;
        uint32_t indexCount;
        float fade;
        glm::vec3 originHi;
        glm::vec3 originLo;
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
                    entry.fade,
                    entry.gpu.originEcefHi,
                    entry.gpu.originEcefLo
                });
                // Update access time for LRU
                const_cast<RockMeshEntry&>(entry).lastAccessTime = glfwGetTime();
            }
        }
    }
    
    // Phase 6: Get uniform locations if shader program provided
    GLint fadeLoc = -1;
    GLint originHiLoc = -1;
    GLint originLoLoc = -1;
    GLint useRteLoc = -1;
    GLint texScaleOffsetLoc = -1;
    if (shaderProgram != 0) {
        fadeLoc = glGetUniformLocation(shaderProgram, "uFade");
        // Faz 1C: RTE uniforms (same as tile shader)
        originHiLoc = glGetUniformLocation(shaderProgram, "uTileOriginECEFHi");
        originLoLoc = glGetUniformLocation(shaderProgram, "uTileOriginECEFLo");
        useRteLoc = glGetUniformLocation(shaderProgram, "uUseRTE");
        texScaleOffsetLoc = glGetUniformLocation(shaderProgram, "uTexScaleOffsetMain");
    }
    
    // Draw outside lock
    for (const auto& cmd : cmds) {
        if (cmd.vao == 0 || cmd.indexCount == 0) continue;
        
        // Phase 6: Set per-mesh fade uniform
        if (fadeLoc >= 0) {
            glUniform1f(fadeLoc, cmd.fade);
        }
        
        // Faz 1C: Set RTE uniforms for jitter-free rendering
        if (originHiLoc >= 0) {
            glUniform3f(originHiLoc, cmd.originHi.x, cmd.originHi.y, cmd.originHi.z);
        }
        if (originLoLoc >= 0) {
            glUniform3f(originLoLoc, cmd.originLo.x, cmd.originLo.y, cmd.originLo.z);
        }
        if (useRteLoc >= 0) {
            glUniform1i(useRteLoc, useRte ? 1 : 0);  // Respect config flag
        }
        // Texture scale/offset = identity (rockmesh UVs are already correct)
        if (texScaleOffsetLoc >= 0) {
            glUniform4f(texScaleOffsetLoc, 1.0f, 1.0f, 0.0f, 0.0f);
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
// Phase 6.4: Returns false during shutdown for deterministic state
bool RockMeshManager::HasInflightWork() const {
    if (shutdown_.load()) return false;
    
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
// Phase 6.4: Returns 0 in-flight during shutdown for deterministic state
RockMeshManager::Stats RockMeshManager::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    Stats s = stats_;
    s.cachedCount = static_cast<int>(lruList_.size());
    // Calculate in-flight count on-demand (proper locking order)
    if (shutdown_.load()) {
        s.inFlightCount = 0;
    } else {
        std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
        s.inFlightCount = static_cast<int>(inFlightSet_.size());
    }
    return s;
}

// P0: Upload epoch management for stale callback prevention
uint64_t RockMeshManager::AllocateUploadEpoch(const std::string& nodeKey) {
    std::lock_guard<std::mutex> lock(uploadEpochMutex_);
    uint64_t epoch = nextUploadEpoch_++;
    uploadEpochByNode_[nodeKey] = epoch;
    return epoch;
}

uint64_t RockMeshManager::InvalidateUploadEpoch(const std::string& nodeKey) {
    std::lock_guard<std::mutex> lock(uploadEpochMutex_);
    auto it = uploadEpochByNode_.find(nodeKey);
    if (it != uploadEpochByNode_.end()) {
        // Increment to invalidate - any upload with old epoch will be rejected
        it->second = ++nextUploadEpoch_;
        return it->second;
    }
    return 0;
}

bool RockMeshManager::IsUploadEpochCurrent(const std::string& nodeKey, uint64_t epoch) const {
    std::lock_guard<std::mutex> lock(uploadEpochMutex_);
    auto it = uploadEpochByNode_.find(nodeKey);
    if (it == uploadEpochByNode_.end()) {
        // Node not found - treat as stale
        return false;
    }
    return it->second == epoch;
}

void RockMeshManager::RemoveUploadEpoch(const std::string& nodeKey) {
    std::lock_guard<std::mutex> lock(uploadEpochMutex_);
    uploadEpochByNode_.erase(nodeKey);
}

#ifdef NATIVE_GLOBE_TESTING
// P0: Test-only API for direct BuildMesh testing (no GL/worker needed)
RockMeshCpu RockMeshManager::BuildMeshForTest(const std::string& nodeKey, const ParsedNodeData& parsed) {
    return BuildMesh(nodeKey, parsed);
}
#endif

void RockMeshManager::OctreeWorkerLoop() {
    // Phase 1: Initialize octree with retry + exponential backoff
    // PlanetoidMetadata or BulkMetadata may fail due to CAPTCHA block (403)
    const int maxRetries = 5;
    int retryDelaySec = 2;  // Start at 2s, double each retry

    if (octreeIndex_ && !octreeIndex_->IsInitialized()) {
        bool initOk = false;
        for (int attempt = 0; attempt < maxRetries && !shutdown_.load(); attempt++) {
            if (attempt > 0) {
                std::cout << "[RockMesh] Octree init retry " << attempt << "/" << maxRetries
                          << " in " << retryDelaySec << "s...\n";
                // Sleep in small increments so we can check shutdown
                for (int i = 0; i < retryDelaySec * 10 && !shutdown_.load(); i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (shutdown_.load()) break;
            }

            if (octreeIndex_->Init()) {
                {
                    std::lock_guard<std::mutex> lock(epochMutex_);
                    resolvedEpoch_ = octreeIndex_->GetEpochString();
                }
                std::cout << "[RockMesh] Octree initialized: epoch=" << resolvedEpoch_
                          << " earthRadius=" << octreeIndex_->GetEarthRadiusM() << "m"
                          << " nodes=" << octreeIndex_->GetKnownNodeCount()
                          << " meshNodes=" << octreeIndex_->GetMeshNodeCount() << "\n";
                initOk = true;
                break;
            }

            retryDelaySec = std::min(retryDelaySec * 2, 30);  // Cap at 30s
        }

        if (!initOk) {
            std::cerr << "[RockMesh] Octree init failed after " << maxRetries
                      << " attempts, falling back to legacy mode\n";
            octreeIndex_.reset();
        }
    }

    // Phase 2: Process pending BulkMetadata fetches
    while (!shutdown_.load()) {
        if (octreeIndex_) {
            int fetched = octreeIndex_->ProcessPendingFetches();
            if (fetched > 0) {
                std::cout << "[Octree] Fetched " << fetched << " BulkMetadata, "
                          << octreeIndex_->GetKnownNodeCount() << " nodes known\n";
            }
        }
        // Sleep between iterations when no pending work
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void RockMeshManager::WorkerLoop() {
    // Create client for this thread
    GoogleEarthNodeDataClient client(config_);

    // Set resolved epoch on client (from octree or config)
    {
        std::lock_guard<std::mutex> lock(epochMutex_);
        if (!resolvedEpoch_.empty()) {
            client.SetEpoch(resolvedEpoch_);
        }
    }

    if (!client.IsEnabled()) {
        std::cerr << "[RockMesh] Worker: client not enabled\n";
        return;
    }

    std::string lastEpoch;
    {
        std::lock_guard<std::mutex> lock(epochMutex_);
        lastEpoch = resolvedEpoch_;
    }

    std::string nodeKey;
    while (requestQueue_.Pop(nodeKey)) {
        // Check if octree resolved a new epoch (async init race fix)
        std::string currentEpoch;
        {
            std::lock_guard<std::mutex> lock(epochMutex_);
            currentEpoch = resolvedEpoch_;
        }
        if (currentEpoch != lastEpoch && !currentEpoch.empty()) {
            lastEpoch = currentEpoch;
            client.SetEpoch(lastEpoch);
            std::cout << "[RockMesh] Worker updated epoch to " << lastEpoch << "\n";
        }
        // Phase 6.4: Handle shutdown immediately with proper cleanup
        if (shutdown_) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end() && 
                (it->second.state == RockMeshState::Fetching || 
                 it->second.state == RockMeshState::Queued)) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "Shutdown";
            }
            // Remove from tracking sets
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            {
                std::lock_guard<std::mutex> queuedLock(queuedMutex_);
                queuedOrPendingKeys_.erase(nodeKey);
            }
            break;
        }
        
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
        
        // Check blacklist before fetching (avoid wasting bandwidth on known-bad keys)
        if (IsBlacklisted(nodeKey)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "blacklisted (terminal HTTP error)";
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.blacklistSkipCount++;
            }
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            continue;
        }
        
        // Fetch from network if cache miss (with rate limiting)
        if (!cacheHit) {
            if (config_.rockMeshRuntimeDebug) {
                std::cout << "[RockMesh:Worker] FETCHING: " << nodeKey << "\n";
                std::cout.flush();
            }
            if (rateLimiter_) rateLimiter_->WaitForSlot();
            fetchResult = client.FetchNodeData(nodeKey);
        }
        if (!fetchResult.success) {
            // Classify HTTP error
            int httpCode = fetchResult.httpCode;
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.failureCount++;
                if (httpCode == 400) stats_.failedHttp400Count++;
                else if (httpCode == 404) stats_.failedHttp404Count++;
                else if (httpCode > 0) stats_.failedHttpOtherCount++;
                else stats_.failedNetworkCount++;
            }
            // Blacklist terminal errors (400/404 won't change on retry)
            if (httpCode == 400 || httpCode == 404) {
                BlacklistKey(nodeKey);
            }
            if (config_.rockMeshRuntimeDebug) {
                std::cerr << "[RockMesh:Worker] FETCH FAILED: " << nodeKey 
                          << " http=" << httpCode
                          << " err=" << fetchResult.errorMessage << "\n";
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = fetchResult.errorMessage;
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
        if (config_.rockMeshRuntimeDebug) {
            std::cout << "[RockMesh:Worker] FETCH OK: " << nodeKey 
                      << " bytes=" << fetchResult.data.size()
                      << (cacheHit ? " (cache)" : " (network)") << "\n";
        }
        ParsedNodeData parsed = RockTreeNodeDataParser::Parse(fetchResult.data);
        if (!parsed.success) {
            if (config_.rockMeshRuntimeDebug) {
                std::cerr << "[RockMesh:Worker] PARSE FAILED: " << nodeKey
                          << " err=" << parsed.error << "\n";
            }
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
        if (config_.rockMeshRuntimeDebug) {
            std::cout << "[RockMesh:Worker] PARSE OK: " << nodeKey
                      << " V=" << parsed.vertexCount
                      << " T=" << parsed.triangleCount
                      << " tex=" << (parsed.texture.valid ? "yes" : "no")
                      << " uv=" << (parsed.hasTexCoords ? "yes" : "no")
                      << " normals=" << (parsed.hasNormals ? "yes" : "no")
                      << "\n";
        }
        
        // Build CPU mesh and carry upload epoch from entry
        RockMeshCpu cpu = BuildMesh(nodeKey, parsed);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                cpu.uploadEpoch = it->second.uploadEpoch;
            }
        }
        
        // Check epoch freshness after build (may have gone stale during fetch+parse).
        // This runs regardless of cpu.valid — a stale build that also failed should
        // still be counted as stale rather than as a build failure.
        if (!IsUploadEpochCurrent(nodeKey, cpu.uploadEpoch)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Stale;
            }
            {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.staleUploadSkips++;
                stats_.staleUploadBytes += static_cast<int>(
                    cpu.vertices.size() * sizeof(float) +
                    cpu.indices.size() * sizeof(uint32_t) +
                    cpu.rgba.size());
            }
            {
                std::lock_guard<std::mutex> inFlightLock(inFlightMutex_);
                inFlightSet_.erase(nodeKey);
            }
            continue;
        }
        
        if (!cpu.valid) {
            if (config_.rockMeshRuntimeDebug) {
                std::cerr << "[RockMesh:Worker] BUILD FAILED: " << nodeKey
                          << " err=" << (cpu.error.empty() ? "unknown" : cpu.error) << "\n";
            }
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
        if (config_.rockMeshRuntimeDebug) {
            std::cout << "[RockMesh:Worker] BUILD OK: " << nodeKey
                      << " verts=" << cpu.vertices.size() / 9
                      << " idx=" << cpu.indices.size()
                      << " tex=" << (cpu.hasTexture ? "yes" : "no")
                      << " rgba=" << cpu.rgba.size() << "B\n";
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
    
    // P0-P2: Sanity check - validate transform matrix for NaN/INF
    if (!config_.rockMeshSanityEnabled) {
        // Skip sanity checks if disabled
    } else {
        // Check transform matrix for non-finite values
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(M[col][row])) {
                    cpu.error = "Invalid transform: non-finite matrix element";
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.discardInvalidTransform++;
                    return cpu;
                }
            }
        }
    }
    
    // Extract translation and compute scale
    glm::dvec3 t(M[3][0], M[3][1], M[3][2]);
    double tLen = glm::length(t);
    
    // P0-P2: Sanity check - validate translation/scale
    if (config_.rockMeshSanityEnabled) {
        if (!std::isfinite(tLen) || tLen < 1e-10) {
            cpu.error = "Invalid transform: non-finite or zero-length translation";
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.discardInvalidScale++;
            return cpu;
        }
    } else if (tLen < 1e-10) {
        cpu.error = "Invalid transform: translation vector too small";
        return cpu;
    }
    
    double kmPerRockUnit = Ellipsoid::WGS84_A_KM / tLen;
    
    // P0-P2: Sanity check - validate kmPerRockUnit is reasonable
    if (config_.rockMeshSanityEnabled) {
        if (!std::isfinite(kmPerRockUnit) || kmPerRockUnit <= 0.0) {
            cpu.error = "Invalid scale: non-finite or negative km per rock unit";
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.discardInvalidScale++;
            return cpu;
        }
    }
    
    // Resize output
    cpu.vertices.resize(V * 9);  // stride 9
    cpu.indices = parsed.indices;
    cpu.triangleCount = T;
    
    // Build positions (world space, then convert to relative for RTE)
    std::vector<glm::dvec3> worldPositions(V);
    glm::dvec3 bboxMin(1e18), bboxMax(-1e18);
    
    // P0-P2: Track if any vertex fails sanity check
    bool hasInvalidVertex = false;
    
    for (int i = 0; i < V; ++i) {
        // Local position (uint8 / 255, delta-decoded from GE vertex stream)
        double lx = parsed.positions[i * 3 + 0] / 255.0;
        double ly = parsed.positions[i * 3 + 1] / 255.0;
        double lz = parsed.positions[i * 3 + 2] / 255.0;
        
        // Transform to world (km)
        glm::dvec4 local(lx, ly, lz, 1.0);
        glm::dvec3 world = glm::dvec3(M * local) * kmPerRockUnit;
        
        // P0-P2: Sanity check - validate vertex position
        if (config_.rockMeshSanityEnabled) {
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
                hasInvalidVertex = true;
                // Continue processing other vertices for complete bbox
            }
        }
        
        worldPositions[i] = world;
        
        // Track bounding box for origin selection
        bboxMin = glm::min(bboxMin, world);
        bboxMax = glm::max(bboxMax, world);
    }
    
    // P0-P2: Check for invalid vertices
    if (config_.rockMeshSanityEnabled && hasInvalidVertex) {
        cpu.error = "Invalid mesh: non-finite vertex positions detected";
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.discardNonFiniteVertex++;
        return cpu;
    }
    
    // P0 Fix: Compute mesh origin (center of bounding box) - needed for both sanity check and RTE
    glm::dvec3 originEcef = (bboxMin + bboxMax) * 0.5;
    
    // P0-P2: Sanity check - validate bounding box
    if (config_.rockMeshSanityEnabled) {
        // Check for non-finite bounds
        if (!std::isfinite(bboxMin.x) || !std::isfinite(bboxMin.y) || !std::isfinite(bboxMin.z) ||
            !std::isfinite(bboxMax.x) || !std::isfinite(bboxMax.y) || !std::isfinite(bboxMax.z)) {
            cpu.error = "Invalid mesh: non-finite bounding box";
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.discardInvalidBounds++;
            return cpu;
        }
        
        // Check AABB diagonal against threshold
        glm::dvec3 bboxDiagonal = bboxMax - bboxMin;
        double bboxDiagonalKm = glm::length(bboxDiagonal);
        if (bboxDiagonalKm > config_.rockMeshMaxBboxDiagonalKm) {
            cpu.error = "Invalid mesh: AABB diagonal exceeds threshold (" + 
                       std::to_string(static_cast<int>(bboxDiagonalKm)) + " km > " +
                       std::to_string(static_cast<int>(config_.rockMeshMaxBboxDiagonalKm)) + " km)";
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.discardAabbExceeded++;
            return cpu;
        }
        
        // P0 Fix: Check vertex distance from mesh origin (not world origin)
        for (int i = 0; i < V; ++i) {
            double distFromMeshOrigin = glm::length(worldPositions[i] - originEcef);
            if (distFromMeshOrigin > config_.rockMeshMaxVertexDistanceFromOriginKm) {
                cpu.error = "Invalid mesh: vertex distance from mesh origin exceeds threshold (" +
                           std::to_string(static_cast<int>(distFromMeshOrigin)) + " km > " +
                           std::to_string(static_cast<int>(config_.rockMeshMaxVertexDistanceFromOriginKm)) + " km)";
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.discardVertexDistanceExceeded++;
                return cpu;
            }
        }
    }
    
    // P0 Fix: originEcef computed above for both distance check and RTE origin
    cpu.originEcefHi = glm::vec3(originEcef);
    cpu.originEcefLo = glm::vec3(originEcef - glm::dvec3(cpu.originEcefHi));
    
    // Store relative positions (RTE/RTC for jitter-free rendering)
    for (int i = 0; i < V; ++i) {
        glm::vec3 relativePos = glm::vec3(worldPositions[i] - originEcef);
        cpu.vertices[i * 9 + 0] = relativePos.x;
        cpu.vertices[i * 9 + 1] = relativePos.y;
        cpu.vertices[i * 9 + 2] = relativePos.z;
    }
    
    // Build UVs from decoded texture coordinates (field 7 or field 2)
    if (parsed.hasTexCoords && !parsed.texCoords.empty()) {
        for (int i = 0; i < V; ++i) {
            uint16_t u16 = parsed.texCoords[i * 2 + 0];
            uint16_t v16 = parsed.texCoords[i * 2 + 1];
            float u, v;
            parsed.uvQuant.Decode(u16, v16, u, v);
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
    
    // Build normals: prefer GE pre-computed normals, fallback to cross-product
    if (parsed.hasNormals && static_cast<int>(parsed.normals.size()) >= V * 3) {
        // GE octahedral-decoded normals: uint8 [0-255], 128 = zero
        for (int i = 0; i < V; ++i) {
            float nx = (parsed.normals[i * 3 + 0] - 127.0f) / 127.0f;
            float ny = (parsed.normals[i * 3 + 1] - 127.0f) / 127.0f;
            float nz = (parsed.normals[i * 3 + 2] - 127.0f) / 127.0f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.0f; ny = 0.0f; nz = 1.0f; }
            cpu.vertices[i * 9 + 3] = nx;
            cpu.vertices[i * 9 + 4] = ny;
            cpu.vertices[i * 9 + 5] = nz;
        }
    } else {
        // Fallback: compute normals from triangle list cross products
        std::vector<glm::vec3> normals(V, glm::vec3(0.0f));
        std::vector<int> normalCounts(V, 0);
        
        for (int t = 0; t < T; ++t) {
            uint32_t i0 = parsed.indices[t * 3 + 0];
            uint32_t i1 = parsed.indices[t * 3 + 1];
            uint32_t i2 = parsed.indices[t * 3 + 2];
            
            if (i0 >= static_cast<uint32_t>(V) || i1 >= static_cast<uint32_t>(V) || i2 >= static_cast<uint32_t>(V)) continue;
            
            glm::vec3 p0(cpu.vertices[i0 * 9 + 0], cpu.vertices[i0 * 9 + 1], cpu.vertices[i0 * 9 + 2]);
            glm::vec3 p1(cpu.vertices[i1 * 9 + 0], cpu.vertices[i1 * 9 + 1], cpu.vertices[i1 * 9 + 2]);
            glm::vec3 p2(cpu.vertices[i2 * 9 + 0], cpu.vertices[i2 * 9 + 1], cpu.vertices[i2 * 9 + 2]);
            
            glm::vec3 e0 = p1 - p0;
            glm::vec3 e1 = p2 - p0;
            glm::vec3 cross = glm::cross(e0, e1);
            float crossLen2 = glm::dot(cross, cross);
            if (crossLen2 < 1e-10f) continue;
            
            glm::vec3 n = glm::normalize(cross);
            
            // Outward enforce: use world-space triangle center as radial reference
            glm::dvec3 centerWorld = (worldPositions[i0] + worldPositions[i1] + worldPositions[i2]) / 3.0;
            glm::vec3 outwardDir = glm::normalize(glm::vec3(centerWorld));
            if (glm::dot(n, outwardDir) < 0.0f) {
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
    
    // P1: Use config fallback color
    uint8_t pixel[4] = {
        config_.rockMeshFallbackR,
        config_.rockMeshFallbackG,
        config_.rockMeshFallbackB,
        255
    };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    
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
                InvalidateUploadEpoch(nodeKey);
            } else if (entry.state == RockMeshState::Queued || 
                       entry.state == RockMeshState::Fetching) {
                // Invalidate epoch so in-flight work for this key is discarded
                InvalidateUploadEpoch(nodeKey);
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
            // Cleanup epoch tracking
            RemoveUploadEpoch(nodeKey);
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
