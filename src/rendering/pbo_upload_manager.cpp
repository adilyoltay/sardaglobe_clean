// PboUploadManager.cpp
// Asynchronous texture upload implementation with proper completion tracking

#include "pbo_upload_manager.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iostream>

namespace globe {

// Platform-specific fence sync
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif

#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFull
#endif

// Check if PBO entry is ready (GPU done)
bool PboEntry::IsReady() const {
    if (!fence) return true;  // No fence = ready (immediate upload path)
    
    // Non-blocking check
    GLenum status = glClientWaitSync(fence, 0, 0);
    return (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED);
}

// Get in-flight request
const InFlightRequest* PboUploadManager::GetInFlightRequest(size_t index) const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (index >= inFlightQueue_.size()) return nullptr;
    return &inFlightQueue_[index];
}

size_t PboUploadManager::GetInFlightCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return inFlightQueue_.size();
}

PboUploadManager::PboUploadManager(const Config& config)
    : config_(config) {
}

PboUploadManager::~PboUploadManager() {
    Shutdown();
}

PboUploadManager::PboUploadManager(PboUploadManager&& other) noexcept
    : config_(other.config_)
    , pboPool_(std::move(other.pboPool_))
    , pboAvailable_(std::move(other.pboAvailable_))
    , pendingQueue_(std::move(other.pendingQueue_))
    , inFlightQueue_(std::move(other.inFlightQueue_))
    , currentFrame_(other.currentFrame_)
    , initialized_(other.initialized_)
    , stats_(other.stats_) {
    other.initialized_ = false;
}

PboUploadManager& PboUploadManager::operator=(PboUploadManager&& other) noexcept {
    if (this != &other) {
        Shutdown();
        
        config_ = other.config_;
        pboPool_ = std::move(other.pboPool_);
        pboAvailable_ = std::move(other.pboAvailable_);
        pendingQueue_ = std::move(other.pendingQueue_);
        inFlightQueue_ = std::move(other.inFlightQueue_);
        currentFrame_ = other.currentFrame_;
        initialized_ = other.initialized_;
        stats_ = other.stats_;
        
        other.initialized_ = false;
    }
    return *this;
}

bool PboUploadManager::Initialize() {
    if (initialized_) {
        return true;
    }
    
    // Check PBO support
    if (config_.usePbo) {
        if (!GLAD_GL_ARB_pixel_buffer_object && !GLAD_GL_EXT_pixel_buffer_object) {
            std::cerr << "[PboUploadManager] PBO not supported, falling back to immediate uploads\n";
            config_.usePbo = false;
        }
    }
    
    // Check fence support
    if (config_.useFences) {
        if (!GLAD_GL_ARB_sync) {
            std::cerr << "[PboUploadManager] Sync fences not supported, using fallback completion\n";
            config_.useFences = false;
        }
    }
    
    if (config_.usePbo) {
        if (!CreatePboPool()) {
            return false;
        }
    }
    
    // Reserve queue capacity
    pendingQueue_.reserve(config_.maxPendingUploads);
    inFlightQueue_.reserve(config_.maxPendingUploads);
    
    initialized_ = true;
    return true;
}

void PboUploadManager::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Wait for all pending uploads
    WaitForAllUploads();
    
    // Destroy PBO pool
    DestroyPboPool();
    
    // Clear queues
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingQueue_.clear();
        inFlightQueue_.clear();
    }
    
    initialized_ = false;
}

bool PboUploadManager::CreatePboPool() {
    if (config_.maxPboCount == 0) {
        return true;
    }
    
    pboPool_.resize(config_.maxPboCount);
    pboAvailable_.resize(config_.maxPboCount, true);
    
    // Generate PBO names safely
    std::vector<GLuint> pboNames(config_.maxPboCount);
    glGenBuffers(static_cast<GLsizei>(config_.maxPboCount), pboNames.data());
    
    if (glGetError() != GL_NO_ERROR) {
        std::cerr << "[PboUploadManager] Failed to generate PBO buffers\n";
        return false;
    }
    
    for (size_t i = 0; i < config_.maxPboCount; ++i) {
        pboPool_[i].pbo = pboNames[i];
        pboPool_[i].capacity = config_.defaultPboSize;
        pboPool_[i].inUse = false;
        pboPool_[i].lastUsedFrame = 0;
        pboPool_[i].fence = nullptr;
        pboPool_[i].poolIndex = static_cast<int>(i);
        
        // Allocate initial storage
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboPool_[i].pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, config_.defaultPboSize, nullptr, GL_STREAM_DRAW);
    }
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    return glGetError() == GL_NO_ERROR;
}

void PboUploadManager::DestroyPboPool() {
    if (pboPool_.empty()) {
        return;
    }
    
    // Delete fences first
    for (auto& entry : pboPool_) {
        if (entry.fence) {
            glDeleteSync(entry.fence);
            entry.fence = nullptr;
        }
    }
    
    // Collect PBO names
    std::vector<GLuint> pbos;
    pbos.reserve(pboPool_.size());
    
    for (auto& entry : pboPool_) {
        if (entry.pbo != 0) {
            pbos.push_back(entry.pbo);
            entry.pbo = 0;
        }
    }
    
    if (!pbos.empty()) {
        glDeleteBuffers(static_cast<GLsizei>(pbos.size()), pbos.data());
    }
    
    pboPool_.clear();
    pboAvailable_.clear();
}

PboEntry* PboUploadManager::AcquirePbo(size_t requiredSize) {
    if (!config_.usePbo || pboPool_.empty()) {
        return nullptr;
    }
    
    // Find available PBO with sufficient capacity and age
    for (size_t i = 0; i < pboPool_.size(); ++i) {
        if (!pboAvailable_[i] || pboPool_[i].inUse) {
            continue;
        }
        
        // Check age threshold to avoid implicit sync
        if (currentFrame_ - pboPool_[i].lastUsedFrame < config_.pboAgeThreshold) {
            continue;
        }
        
        // Check if GPU is actually done (fence-based)
        if (config_.useFences && !pboPool_[i].IsReady()) {
            continue;
        }
        
        if (pboPool_[i].capacity >= requiredSize) {
            pboAvailable_[i] = false;
            pboPool_[i].inUse = true;
            return &pboPool_[i];
        }
    }
    
    // Try to find any available PBO and resize it
    for (size_t i = 0; i < pboPool_.size(); ++i) {
        if (!pboAvailable_[i] || pboPool_[i].inUse) {
            continue;
        }
        
        // Check fence status
        if (config_.useFences && !pboPool_[i].IsReady()) {
            continue;
        }
        
        pboAvailable_[i] = false;
        pboPool_[i].inUse = true;
        
        // Resize if needed
        if (pboPool_[i].capacity < requiredSize) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboPool_[i].pbo);
            glBufferData(GL_PIXEL_UNPACK_BUFFER, requiredSize, nullptr, GL_STREAM_DRAW);
            pboPool_[i].capacity = requiredSize;
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        }
        
        return &pboPool_[i];
    }
    
    return nullptr;  // No available PBO
}

void PboUploadManager::ReleasePbo(PboEntry* entry) {
    if (!entry || !entry->IsValid() || entry->poolIndex < 0) {
        return;
    }
    
    entry->inUse = false;
    entry->lastUsedFrame = currentFrame_;
    
    // Delete old fence
    if (entry->fence) {
        glDeleteSync(entry->fence);
        entry->fence = nullptr;
    }
    
    // Mark as available in pool
    pboAvailable_[entry->poolIndex] = true;
}

void PboUploadManager::OrphanPbo(PboEntry* entry) {
    if (!entry || !entry->IsValid()) {
        return;
    }
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, entry->pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, entry->capacity, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.pboOrphans++;
}

void PboUploadManager::InsertFence(PboEntry* entry) {
    if (!config_.useFences || !entry) return;
    
    // Delete old fence if exists
    if (entry->fence) {
        glDeleteSync(entry->fence);
    }
    
    // Insert new fence
    entry->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

bool PboUploadManager::CheckFence(PboEntry* entry) {
    if (!entry || !entry->fence) return true;
    
    GLenum status = glClientWaitSync(entry->fence, 0, 0);
    return (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED);
}

size_t PboUploadManager::CalculateDataSize(GLsizei width, GLsizei height, GLenum format, GLenum type) {
    int components = 0;
    switch (format) {
        case GL_RED:
        case GL_R8:
        case GL_R16:
        case GL_R16F:
        case GL_R32F:
            components = 1;
            break;
        case GL_RG:
        case GL_RG8:
            components = 2;
            break;
        case GL_RGB:
        case GL_BGR:
            components = 3;
            break;
        case GL_RGBA:
        case GL_BGRA:
            components = 4;
            break;
        default:
            components = 4;  // Assume RGBA for unknown
    }
    
    int typeSize = 1;
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
            typeSize = 1;
            break;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:
            typeSize = 2;
            break;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:
            typeSize = 4;
            break;
        default:
            typeSize = 1;
    }
    
    return static_cast<size_t>(width) * height * components * typeSize;
}

bool PboUploadManager::SubmitUpload(UploadRequest&& request) {
    if (!initialized_ || !request.IsValid()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    if (pendingQueue_.size() >= config_.maxPendingUploads) {
        return false;  // Queue full
    }
    
    QueuedRequest qr;
    qr.request = std::move(request);
    qr.queueTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    qr.submitFrame = currentFrame_;
    
    pendingQueue_.push_back(std::move(qr));
    
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    stats_.totalUploads++;
    stats_.pendingCount = static_cast<uint32_t>(pendingQueue_.size() + inFlightQueue_.size());
    
    return true;
}

bool PboUploadManager::SubmitUploadExternal(GLuint texture, GLsizei width, GLsizei height,
                                           GLenum format, GLenum type,
                                           const void* data, size_t dataSize,
                                           UploadCompleteCallback callback,
                                           void* userData, uint64_t priority,
                                           bool generateMipmap) {
    UploadRequest req;
    req.targetTexture = texture;
    req.width = width;
    req.height = height;
    req.format = format;
    req.type = type;
    req.externalData = data;
    req.dataSize = dataSize;
    req.ownsData = false;
    req.onComplete = callback;
    req.userData = userData;
    req.priority = priority;
    req.internalFormat = GL_RGBA8;
    req.generateMipmap = generateMipmap;
    
    return SubmitUpload(std::move(req));
}

bool PboUploadManager::SubmitUploadOwned(GLuint texture, GLsizei width, GLsizei height,
                                        GLenum format, GLenum type,
                                        std::vector<uint8_t>&& data,
                                        UploadCompleteCallback callback,
                                        void* userData, uint64_t priority,
                                        bool generateMipmap) {
    UploadRequest req;
    req.targetTexture = texture;
    req.width = width;
    req.height = height;
    req.format = format;
    req.type = type;
    req.pixelData = std::move(data);
    req.ownsData = true;
    req.onComplete = callback;
    req.userData = userData;
    req.priority = priority;
    req.internalFormat = GL_RGBA8;
    req.generateMipmap = generateMipmap;
    
    return SubmitUpload(std::move(req));
}

// P0: Submit with stale protection (resource key + generation token)
bool PboUploadManager::SubmitUploadOwnedWithToken(GLuint texture, GLsizei width, GLsizei height,
                                                  GLenum format, GLenum type,
                                                  std::vector<uint8_t>&& data,
                                                  const std::string& resourceKey,
                                                  uint64_t generationToken,
                                                  UploadCompleteCallback callback,
                                                  void* userData, uint64_t priority,
                                                  bool generateMipmap) {
    UploadRequest req;
    req.targetTexture = texture;
    req.width = width;
    req.height = height;
    req.format = format;
    req.type = type;
    req.pixelData = std::move(data);
    req.ownsData = true;
    req.onComplete = callback;
    req.userData = userData;
    req.priority = priority;
    req.internalFormat = GL_RGBA8;
    req.generateMipmap = generateMipmap;
    req.generationToken = generationToken;  // P0: Set token for stale detection
    req.resourceKey = resourceKey;           // P0: Set resource key for validation
    
    return SubmitUpload(std::move(req));
}

bool PboUploadManager::ExecuteUploadPbo(UploadRequest& req, PboEntry* pbo) {
    if (!pbo || !pbo->IsValid()) {
        return false;
    }
    
    const void* data = req.GetData();
    size_t dataSize = req.GetDataSize();
    
    if (!data || dataSize == 0) {
        return false;
    }
    
    // Bind PBO
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo->pbo);
    
    // Orphan if configured (prevents implicit sync)
    if (config_.orphanUnusedPbos) {
        glBufferData(GL_PIXEL_UNPACK_BUFFER, pbo->capacity, nullptr, GL_STREAM_DRAW);
    }
    
    // Ensure buffer is large enough
    if (pbo->capacity < dataSize) {
        glBufferData(GL_PIXEL_UNPACK_BUFFER, dataSize, nullptr, GL_STREAM_DRAW);
        pbo->capacity = dataSize;
    }
    
    // Map buffer and copy data
    void* mapped = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    if (!mapped) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return false;
    }
    
    std::memcpy(mapped, data, dataSize);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    
    // Bind target texture and upload from PBO
    glBindTexture(req.target, req.targetTexture);
    
    if (req.compressed) {
        glCompressedTexImage2D(req.target, req.level, req.internalFormat,
                               req.width, req.height, 0,
                               static_cast<GLsizei>(dataSize), nullptr);
    } else {
        glTexImage2D(req.target, req.level, req.internalFormat,
                     req.width, req.height, 0,
                     req.format, req.type, nullptr);
    }
    
    if (req.generateMipmap) {
        glGenerateMipmap(req.target);
    }
    
    // Insert fence for completion tracking
    InsertFence(pbo);
    
    glBindTexture(req.target, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    
    bool success = glGetError() == GL_NO_ERROR;
    
    if (success) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.bytesUploaded += dataSize;
        stats_.pboReuses++;
    }
    
    return success;
}

bool PboUploadManager::ExecuteUploadImmediate(const UploadRequest& req) {
    const void* data = req.GetData();
    size_t dataSize = req.GetDataSize();
    
    if (!data || dataSize == 0) {
        return false;
    }
    
    glBindTexture(req.target, req.targetTexture);
    
    if (req.compressed) {
        glCompressedTexImage2D(req.target, req.level, req.internalFormat,
                               req.width, req.height, 0,
                               static_cast<GLsizei>(dataSize), data);
    } else {
        glTexImage2D(req.target, req.level, req.internalFormat,
                     req.width, req.height, 0,
                     req.format, req.type, data);
    }
    
    if (req.generateMipmap) {
        glGenerateMipmap(req.target);
    }
    
    glBindTexture(req.target, 0);
    
    bool success = glGetError() == GL_NO_ERROR;
    
    if (success) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.bytesUploaded += dataSize;
        stats_.immediateUploads++;
    }
    
    return success;
}

void PboUploadManager::CompleteRequest(InFlightRequest& inflight, bool success) {
    // P0: Stale callback detection - check if request is still valid
    // If token validation fails, skip GL operations and count as stale
    bool isStale = false;
    if (inflight.generationToken != 0 && tokenValidator_) {
        // Token is set and validator exists - check validity
        if (!tokenValidator_(inflight.resourceKey, inflight.generationToken)) {
            isStale = true;
        }
    }
    
    if (isStale) {
        // P0: Stale upload - don't touch GL, just clean up and count
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.staleUploadSkips++;
        stats_.staleUploadBytes += inflight.request.GetDataSize();
        
        // Release PBO if used (but don't invoke callback)
        if (inflight.pboEntry) {
            ReleasePbo(inflight.pboEntry);
            inflight.pboEntry = nullptr;
        }
        
        // Don't invoke callback - resource has been evicted/invalidated
        return;
    }
    
    // Normal path - invoke callback
    if (inflight.request.onComplete) {
        inflight.request.onComplete(inflight.request.targetTexture, success, inflight.request.userData);
    }
    
    // Release PBO if used
    if (inflight.pboEntry) {
        ReleasePbo(inflight.pboEntry);
        inflight.pboEntry = nullptr;
    }
    
    // Update stats
    std::lock_guard<std::mutex> lock(statsMutex_);
    if (success) {
        stats_.successfulUploads++;
    } else {
        stats_.failedUploads++;
    }
    stats_.inFlightRequests = static_cast<uint32_t>(inFlightQueue_.size());
}

void PboUploadManager::PollGpuCompletion() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    auto it = inFlightQueue_.begin();
    while (it != inFlightQueue_.end()) {
        if (it->IsReady()) {
            // P0: Check if request was invalidated before completion
            if (!it->isValid) {
                // Request was marked invalid - treat as stale
                CompleteRequest(*it, false);
                it = inFlightQueue_.erase(it);
                
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.inFlightRequests = static_cast<uint32_t>(inFlightQueue_.size());
            } else {
                // Normal completion
                CompleteRequest(*it, true);
                it = inFlightQueue_.erase(it);
                
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.fenceWaits++;
                stats_.inFlightRequests = static_cast<uint32_t>(inFlightQueue_.size());
            }
        } else {
            ++it;
        }
    }
}

// P0: Drain all pending uploads (for shutdown)
void PboUploadManager::DrainAllPendingUploads() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // Mark all in-flight as invalid (they'll be cleaned up as stale)
    for (auto& inflight : inFlightQueue_) {
        inflight.isValid = false;
    }
    
    // Clear pending queue
    pendingQueue_.clear();
}

int PboUploadManager::ProcessUploads() {
    if (!initialized_) {
        return 0;
    }
    
    // First, check for completed uploads
    PollGpuCompletion();
    
    int processedCount = 0;
    int attemptedCount = 0;        // P0 FIX: Track all attempts (success or fail)
    uint64_t attemptedBytes = 0;   // P0 FIX: Track all attempted bytes
    auto frameStartTime = std::chrono::high_resolution_clock::now();
    bool budgetHit = false;
    
    // Process pending uploads with budget controls
    std::vector<QueuedRequest> toProcess;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        if (pendingQueue_.empty()) {
            return processedCount;
        }
        
        // Sort by priority (lower = higher priority)
        std::sort(pendingQueue_.begin(), pendingQueue_.end(),
                  [](const QueuedRequest& a, const QueuedRequest& b) {
                      return a.request.priority < b.request.priority;
                  });
        
        // P0: Budget-aware batch selection
        size_t maxBatch = config_.usePbo ? config_.maxPboCount : 8;
        size_t maxByCount = std::min(static_cast<size_t>(config_.maxUploadsPerFrame), maxBatch);
        
        size_t selected = 0;
        uint64_t selectedBytes = 0;
        
        for (auto& qr : pendingQueue_) {
            if (selected >= maxByCount) break;
            
            size_t reqBytes = qr.request.GetDataSize();
            
            // Byte budget check - P0: Check all items, but allow first even if over budget
            if (selectedBytes + reqBytes > config_.maxBytesPerFrame) {
                if (selected == 0) {
                    // First item exceeds budget alone - still process it (must make progress)
                    // But mark budget hit so we don't process anything after
                    budgetHit = true;
                } else {
                    // Budget hit on subsequent items - defer remaining
                    budgetHit = true;
                    break;
                }
            }
            
            selectedBytes += reqBytes;
            selected++;
        }
        
        // Move selected to processing list
        toProcess.insert(toProcess.end(), 
                        std::make_move_iterator(pendingQueue_.begin()),
                        std::make_move_iterator(pendingQueue_.begin() + selected));
        
        // P0: Deferred requests stay in pending queue
        pendingQueue_.erase(pendingQueue_.begin(), pendingQueue_.begin() + selected);
    }
    
    // Execute uploads with time budget check
    for (auto& qr : toProcess) {
        auto& req = qr.request;
        
        // Try PBO path first
        PboEntry* pbo = nullptr;
        if (config_.usePbo) {
            pbo = AcquirePbo(req.GetDataSize());
        }
        
        bool success;
        if (pbo) {
            success = ExecuteUploadPbo(req, pbo);
            if (success) {
                // Move to in-flight with PBO reference
                InFlightRequest inflight;
                inflight.request = std::move(req);
                inflight.pboEntry = pbo;
                inflight.submitTimeUs = qr.queueTimeUs;
                inflight.submitFrame = qr.submitFrame;
                // P0: Set resource key and token for stale detection
                inflight.resourceKey = req.resourceKey;
                inflight.generationToken = req.generationToken;
                inflight.isValid = true;
                
                std::lock_guard<std::mutex> lock(queueMutex_);
                inFlightQueue_.push_back(std::move(inflight));
                
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.inFlightRequests = static_cast<uint32_t>(inFlightQueue_.size());
            } else {
                ReleasePbo(pbo);
                // Complete with failure
                if (req.onComplete) {
                    req.onComplete(req.targetTexture, false, req.userData);
                }
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.failedUploads++;
            }
        } else {
            // Fallback to immediate upload (completes synchronously)
            // P0: Check for stale upload before executing
            bool isStale = false;
            if (req.generationToken != 0 && tokenValidator_) {
                // Validate synchronously using resourceKey
                isStale = !tokenValidator_(req.resourceKey, req.generationToken);
            }
            
            if (isStale) {
                // Skip upload, count as stale
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.staleUploadSkips++;
                stats_.staleUploadBytes += req.GetDataSize();
                if (req.onComplete) {
                    req.onComplete(req.targetTexture, false, req.userData);
                }
            } else {
                success = ExecuteUploadImmediate(req);
                
                // Complete immediately
                if (req.onComplete) {
                    req.onComplete(req.targetTexture, success, req.userData);
                }
                
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                if (success) {
                    stats_.successfulUploads++;
                } else {
                    stats_.failedUploads++;
                }
            }
        }
        
        // P0 FIX: Track attempted regardless of success
        ++attemptedCount;
        attemptedBytes += req.GetDataSize();
        
        if (success) {
            ++processedCount;
        }
        
        // P0: Time budget check after each upload
        auto now = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(now - frameStartTime).count();
        if (elapsedMs >= config_.maxMsPerFrame) {
            // Time budget exhausted - defer remaining requests
            budgetHit = true;
            auto it = toProcess.begin() + (&qr - &toProcess[0]) + 1;
            if (it != toProcess.end()) {
                std::lock_guard<std::mutex> lock(queueMutex_);
                // Move deferred requests back to pending queue
                for (auto& deferredReq : std::vector<QueuedRequest>(std::make_move_iterator(it), 
                                                                    std::make_move_iterator(toProcess.end()))) {
                    pendingQueue_.push_back(std::move(deferredReq));
                }
            }
            break;
        }
    }
    
    // P0: Update budget stats
    if (budgetHit) {
        // P0 FIX v3: Calculate only for truly deferred (unattempted) items
        size_t deferredCount = toProcess.size() - attemptedCount;
        
        // Calculate total bytes in toProcess
        uint64_t totalBytes = 0;
        for (const auto& qr : toProcess) {
            totalBytes += qr.request.GetDataSize();
        }
        
        // P0 FIX v3: deferredBytes = total - attempted (not just successful)
        uint64_t deferredBytes = (attemptedBytes > totalBytes) ? 0 : (totalBytes - attemptedBytes);
        
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.skippedByBudget += deferredCount;  // P0 FIX v3: Only truly deferred count
        stats_.deferredBytes += deferredBytes;    // P0 FIX v3: Only truly deferred bytes
        stats_.budgetHits++;
    }
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.pendingCount = static_cast<uint32_t>(pendingQueue_.size() + inFlightQueue_.size());
        stats_.activePbos = 0;
        for (const auto& pbo : pboPool_) {
            if (pbo.inUse) {
                stats_.activePbos++;
            }
        }
    }
    
    return processedCount;
}

void PboUploadManager::WaitForAllUploads() {
    if (!initialized_) {
        return;
    }
    
    // Process all pending uploads immediately
    while (HasPendingUploads()) {
        ProcessUploads();
        
        // Wait for all in-flight uploads
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            for (auto& inflight : inFlightQueue_) {
                if (inflight.pboEntry && inflight.pboEntry->fence) {
                    glClientWaitSync(inflight.pboEntry->fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
                }
            }
        }
        
        // Poll again
        PollGpuCompletion();
        
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (pendingQueue_.empty() && inFlightQueue_.empty()) {
                break;
            }
        }
    }
    
    // Ensure GPU is done
    glFinish();
}

bool PboUploadManager::HasPendingUploads() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return !pendingQueue_.empty() || !inFlightQueue_.empty();
}

bool PboUploadManager::IsUploadReady(int inFlightIndex) const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (inFlightIndex < 0 || static_cast<size_t>(inFlightIndex) >= inFlightQueue_.size()) {
        return true;  // Invalid index = ready/error
    }
    return inFlightQueue_[inFlightIndex].IsReady();
}

UploadStats PboUploadManager::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void PboUploadManager::ResetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = UploadStats{};
}

void PboUploadManager::SetConfig(const Config& config) {
    // Only update values that don't require re-initialization
    config_.maxPendingUploads = config.maxPendingUploads;
    config_.orphanUnusedPbos = config.orphanUnusedPbos;
    config_.pboAgeThreshold = config.pboAgeThreshold;
    config_.useFences = config.useFences;
    
    // P0: Budget controls can be updated at runtime
    config_.maxUploadsPerFrame = config.maxUploadsPerFrame;
    config_.maxBytesPerFrame = config.maxBytesPerFrame;
    config_.maxMsPerFrame = config.maxMsPerFrame;
}

void PboUploadManager::BeginFrame(uint64_t frameNumber) {
    currentFrame_ = frameNumber;
}

void PboUploadManager::EndFrame() {
    // Frame boundary - process any remaining uploads
    ProcessUploads();
}

void PboUploadManager::DumpState() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    std::lock_guard<std::mutex> statsLock(statsMutex_);
    
    std::cerr << "[PboUploadManager] State:\n";
    std::cerr << "  Initialized: " << (initialized_ ? "yes" : "no") << "\n";
    std::cerr << "  Current frame: " << currentFrame_ << "\n";
    std::cerr << "  PBO pool: " << pboPool_.size() << " entries\n";
    std::cerr << "  Pending queue: " << pendingQueue_.size() << "\n";
    std::cerr << "  In-flight queue: " << inFlightQueue_.size() << "\n";
    std::cerr << "  Stats: uploads=" << stats_.totalUploads 
              << " success=" << stats_.successfulUploads 
              << " failed=" << stats_.failedUploads 
              << " inFlight=" << stats_.inFlightRequests << "\n";
}

} // namespace globe
