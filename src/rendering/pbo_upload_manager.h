// PboUploadManager.h
// Asynchronous texture upload manager using OpenGL PBOs
// Reference: Google Earth WASM mirth engine async upload pipeline

#ifndef GLOBE_PBO_UPLOAD_MANAGER_H_
#define GLOBE_PBO_UPLOAD_MANAGER_H_

#include <glad/glad.h>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <cstdint>
#include <map>
#include <atomic>

namespace globe {

// Upload completion callback
// Parameters: textureId, success, userdata
using UploadCompleteCallback = std::function<void(GLuint, bool, void*)>;

// Upload request - safe ownership semantics
struct UploadRequest {
    GLuint targetTexture = 0;           // Destination texture ID
    GLenum target = GL_TEXTURE_2D;      // Texture target
    GLint level = 0;                    // Mipmap level
    GLint internalFormat = GL_RGBA8;    // Internal format
    GLsizei width = 0;                  // Texture width
    GLsizei height = 0;                 // Texture height
    GLenum format = GL_RGBA;            // Pixel format
    GLenum type = GL_UNSIGNED_BYTE;     // Pixel type
    size_t dataSize = 0;                // Size of pixel data in bytes
    
    // Data ownership options:
    // 1. Use pixelData (copied/moved into request)
    // 2. Use externalData (borrowed pointer, caller ensures lifetime)
    std::vector<uint8_t> pixelData;     // Owned pixel data (preferred)
    const void* externalData = nullptr; // Borrowed pointer (caller must keep alive until callback)
    bool ownsData = false;              // true if using pixelData, false if using externalData
    
    UploadCompleteCallback onComplete;  // Completion callback
    void* userData = nullptr;           // User data for callback
    uint64_t priority = 0;              // Priority for queue ordering (lower = higher priority)
    uint64_t submitFrame = 0;           // Frame number when submitted
    bool compressed = false;            // Is compressed format (DXT, ETC, etc.)
    bool generateMipmap = false;        // Generate mipmaps after upload
    
    // P0: Generation token for stale callback detection
    // Upload is only valid if token matches current generation for the resource
    uint64_t generationToken = 0;       // Resource generation (0 = no validation)
    
    // Constructors for convenience
    UploadRequest() = default;
    
    // Move constructor
    UploadRequest(UploadRequest&&) = default;
    UploadRequest& operator=(UploadRequest&&) = default;
    
    // Disable copy (expensive)
    UploadRequest(const UploadRequest&) = delete;
    UploadRequest& operator=(const UploadRequest&) = delete;
    
    // Validation
    bool IsValid() const {
        if (targetTexture == 0 || width == 0 || height == 0) return false;
        
        size_t actualDataSize = ownsData ? pixelData.size() : dataSize;
        return actualDataSize > 0;
    }
    
    // Get data pointer (abstracts ownership)
    const void* GetData() const {
        return ownsData ? pixelData.data() : externalData;
    }
    
    // Get data size
    size_t GetDataSize() const {
        return ownsData ? pixelData.size() : dataSize;
    }
};

// PBO entry in ring buffer
struct PboEntry {
    GLuint pbo = 0;                     // PBO name
    size_t capacity = 0;                // Buffer capacity in bytes
    bool inUse = false;                 // Is currently being used for upload
    uint64_t lastUsedFrame = 0;         // Last frame this PBO was used
    GLsync fence = nullptr;             // GPU fence for completion tracking
    int poolIndex = -1;                 // Index in pboPool_
    
    bool IsValid() const { return pbo != 0; }
    bool IsReady() const;               // Check if GPU is done with this PBO
};

// Upload statistics
struct UploadStats {
    uint64_t totalUploads = 0;          // Total uploads attempted
    uint64_t successfulUploads = 0;     // Successful uploads
    uint64_t failedUploads = 0;         // Failed uploads
    uint64_t bytesUploaded = 0;         // Total bytes uploaded
    uint64_t pboOrphans = 0;            // Number of PBO orphans created
    uint64_t pboReuses = 0;             // Number of PBO reuses
    uint64_t immediateUploads = 0;      // Uploads done immediately (no PBO)
    uint64_t fenceWaits = 0;            // Number of fence sync waits
    double avgUploadTimeMs = 0.0;       // Average upload time
    uint32_t pendingCount = 0;          // Currently pending uploads
    uint32_t activePbos = 0;            // Number of PBOs currently in flight
    uint32_t inFlightRequests = 0;      // Number of requests in flight
    
    // P0: Budget tracking
    uint64_t skippedByBudget = 0;       // Uploads skipped due to budget limits
    uint64_t deferredBytes = 0;         // Bytes deferred to next frames
    uint64_t budgetHits = 0;            // Number of frames where budget was hit
    
    // P0: Stale upload tracking (stale callback prevention)
    uint64_t staleUploadSkips = 0;      // Uploads skipped due to stale token
    uint64_t staleUploadBytes = 0;      // Bytes of stale uploads skipped
};

// Forward declaration
class PboUploadManager;

// In-flight request tracking
struct InFlightRequest {
    UploadRequest request;
    PboEntry* pboEntry = nullptr;       // Associated PBO entry (null for immediate uploads)
    uint64_t submitTimeUs = 0;          // Microseconds when submitted
    uint64_t submitFrame = 0;           // Frame when submitted
    bool completed = false;             // Set true when GPU signals completion
    
    // P0: Stale upload detection
    std::string resourceKey;            // Resource identifier (e.g., nodeKey)
    uint64_t generationToken = 0;       // Expected generation for validation
    bool isValid = true;                // Set false if invalidated before completion
    
    bool IsReady() const {
        if (!pboEntry) return true;  // Immediate uploads are ready
        return pboEntry->IsReady();
    }
};

// PBO Upload Manager
// Manages async texture uploads using OpenGL Pixel Buffer Objects
class PboUploadManager {
public:
    // Configuration
    struct Config {
        uint32_t maxPboCount;               // Maximum PBOs in ring buffer
        size_t defaultPboSize;              // Default PBO size
        uint32_t maxPendingUploads;         // Maximum pending upload queue size
        bool usePbo;                        // Enable PBO upload path
        bool orphanUnusedPbos;              // Orphan PBOs after use (prevents implicit sync)
        uint32_t pboAgeThreshold;           // Frames before a PBO can be reused
        bool useFences;                     // Use GL sync fences for completion
        
        // P0: Frame budget controls to prevent stutter
        uint32_t maxUploadsPerFrame;        // Max uploads per frame
        size_t maxBytesPerFrame;            // Max bytes uploaded per frame
        double maxMsPerFrame;               // Max milliseconds spent on uploads per frame
        
        Config()
            : maxPboCount(8)
            , defaultPboSize(4 * 1024 * 1024)
            , maxPendingUploads(64)
            , usePbo(true)
            , orphanUnusedPbos(true)
            , pboAgeThreshold(3)
            , useFences(true)
            , maxUploadsPerFrame(8)
            , maxBytesPerFrame(64 * 1024 * 1024)  // 64 MB default
            , maxMsPerFrame(2.0)  // 2ms default budget
        {}
    };

    explicit PboUploadManager(const Config& config = Config{});
    ~PboUploadManager();

    // Non-copyable, movable
    PboUploadManager(const PboUploadManager&) = delete;
    PboUploadManager& operator=(const PboUploadManager&) = delete;
    PboUploadManager(PboUploadManager&&) noexcept;
    PboUploadManager& operator=(PboUploadManager&&) noexcept;

    // Initialize OpenGL resources
    bool Initialize();
    
    // Shutdown and cleanup
    void Shutdown();

    // Submit an upload request (moves data for efficiency)
    // Returns true if queued, false if queue full
    // Callback is invoked when upload is GPU-complete
    bool SubmitUpload(UploadRequest&& request);
    
    // Helper: Submit with external data (caller must keep data alive until callback)
    bool SubmitUploadExternal(GLuint texture, GLsizei width, GLsizei height,
                              GLenum format, GLenum type,
                              const void* data, size_t dataSize,
                              UploadCompleteCallback callback = nullptr,
                              void* userData = nullptr,
                              uint64_t priority = 0,
                              bool generateMipmap = false);
    
    // Helper: Submit with owned data (moves data)
    bool SubmitUploadOwned(GLuint texture, GLsizei width, GLsizei height,
                           GLenum format, GLenum type,
                           std::vector<uint8_t>&& data,
                           UploadCompleteCallback callback = nullptr,
                           void* userData = nullptr,
                           uint64_t priority = 0,
                           bool generateMipmap = false);

    // Process pending uploads and check GPU completion
    // Call once per frame before rendering
    // Returns number of newly completed uploads this frame
    int ProcessUploads();

    // Wait for all pending uploads to complete
    // Use sparingly - can cause GPU/CPU sync
    void WaitForAllUploads();

    // Check if any uploads are pending
    bool HasPendingUploads() const;
    
    // Check if specific upload is ready (by checking fences)
    bool IsUploadReady(int inFlightIndex) const;

    // Get current statistics
    UploadStats GetStats() const;
    
    // Reset statistics
    void ResetStats();

    // Get/set configuration
    const Config& GetConfig() const { return config_; }
    void SetConfig(const Config& config);

    // Frame management
    void BeginFrame(uint64_t frameNumber);
    void EndFrame();
    
    // Debug: Dump internal state
    void DumpState() const;
    
    // Get in-flight request for external tracking
    const InFlightRequest* GetInFlightRequest(size_t index) const;
    size_t GetInFlightCount() const;
    
    // P0: Stale upload prevention
    // Check if request is still valid (token matches current generation)
    using TokenValidator = std::function<bool(const std::string& key, uint64_t token)>;
    void SetTokenValidator(TokenValidator validator) { tokenValidator_ = validator; }
    
    // P0: Drain all pending uploads (for shutdown)
    void DrainAllPendingUploads();

private:
    // Internal request queue entry
    struct QueuedRequest {
        UploadRequest request;
        uint64_t queueTimeUs = 0;           // Microseconds when queued
        uint64_t submitFrame = 0;           // Frame when submitted
    };

    // PBO pool management
    bool CreatePboPool();
    void DestroyPboPool();
    PboEntry* AcquirePbo(size_t requiredSize);
    void ReleasePbo(PboEntry* entry);
    
    // Upload execution
    bool ExecuteUploadPbo(UploadRequest& req, PboEntry* pbo);
    bool ExecuteUploadImmediate(const UploadRequest& req);
    
    // Check GPU completion of PBO operations
    void PollGpuCompletion();
    bool CheckFence(PboEntry* entry);
    void InsertFence(PboEntry* entry);
    
    // Complete an in-flight request (invoke callback, release resources)
    void CompleteRequest(InFlightRequest& inflight, bool success);
    
    // Orphan PBO buffer (discard old contents)
    void OrphanPbo(PboEntry* entry);
    
    // Calculate total size required for upload
    static size_t CalculateDataSize(GLsizei width, GLsizei height, GLenum format, GLenum type);

private:
    Config config_;
    
    // PBO ring buffer
    std::vector<PboEntry> pboPool_;
    std::vector<bool> pboAvailable_;
    
    // Request queue
    std::vector<QueuedRequest> pendingQueue_;
    std::vector<InFlightRequest> inFlightQueue_;
    mutable std::mutex queueMutex_;
    
    // P0: Token validation for stale callback prevention
    TokenValidator tokenValidator_;
    
    // State
    uint64_t currentFrame_ = 0;
    bool initialized_ = false;
    
    // Statistics
    mutable std::mutex statsMutex_;
    UploadStats stats_;
};

} // namespace globe

#endif // GLOBE_PBO_UPLOAD_MANAGER_H_
