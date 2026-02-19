#pragma once

#include "../core/config.h"
#include "../core/tile.h"
#include "../core/layer_manager.h"
#include "../scheduling/tile_scheduler.h"
#include "../scheduling/tile_pyramid.h"
#include "../scheduling/job_system.h"
#include "../rendering/texture_manager.h"
#include "../rendering/shader_manager.h"
#include "../rendering/tile_renderer.h"
#include "../rendering/tile_mesh_scheduler.h"
#include "../rendering/render_frame.h"
#include "../rendering/rockmesh_manager.h"
#include "../rendering/atmosphere_renderer.h"
#include "../io/dem_manager.h"
#include "../core/frame_time_tracker.h"
#include "../camera/earth_camera.h"
#include "../camera/flight_controller.h"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <array>     // P0-3: Double-buffered snapshots
#include <atomic>    // P0-3: Lock-free handoff
#include <mutex>     // P0-3: Snapshot swap protection

struct GLFWwindow;

namespace globe {

// Main globe engine - orchestrates all subsystems
// Google Earth parity: Modular architecture with clean separation
class GlobeEngine {
public:
    explicit GlobeEngine(const Config& config);
    ~GlobeEngine();
    
    // Initialize (creates window and GL context)
    bool Init();
    
    // Main loop
    void Run();
    
    // Shutdown
    void Shutdown();
    
    // Camera API (Google Earth parity)
    void FlyTo(double lat, double lon, double altMeters, 
               double heading = 0.0, double tilt = 0.0, double duration = 1.5);
    void LookAt(double lat, double lon, double altitude);
    
    // Query
    void GetCameraLatLonAlt(double& lat, double& lon, double& alt) const;
    int GetCurrentZoom() const;
    
    // Access camera for external control
    earth::PerspectiveCamera& GetCamera() { return *camera_; }
    earth::FlightController& GetFlightController() { return *flightController_; }
    
    // Layer API (FAZ 4)
    LayerManager& GetLayerManager() { return layerManager_; }
    
    // Screen <-> Geo conversion (FAZ 4.3)
    bool GetGeoFromScreenPoint(double screenX, double screenY, double& outLon, double& outLat);
    bool GetScreenPointFromGeo(double lon, double lat, double& outScreenX, double& outScreenY);
    
    // Screenshot capture (Visual Testing)
    bool SaveScreenshot(const std::string& filename);
    
    // Visual test mode
    void RunVisualLodTest();

    // Smoke test: exercises zoom in/out + tile/DEM pipelines and exits with pass/fail.
    bool RunSmokeTest();

    // Profile pan at zoom 6-7: prints per-frame timing breakdown to identify bottlenecks.
    void RunPanProfile();
    
private:
    // Frame phases (Google Earth style game loop)
    void ProcessInput(double currentTime);
    void Update(double dt, double currentTime);
    void Render();
    
    // Tile management
    void BuildTileMesh(Tile& tile);
    void RenderTile(const Tile& tile, const glm::mat4& mvp);
    void PreloadBaseTiles();  // LOD0-1 bootstrap coverage
    
    // Globe picking (for navigation)
    bool PickGlobe(double screenX, double screenY, glm::dvec3& outPoint);
    
    // Input callbacks
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    
    Config config_;
    GLFWwindow* window_ = nullptr;
    
    // Camera system (from backup - Google Earth parity)
    std::unique_ptr<earth::PerspectiveCamera> camera_;
    std::unique_ptr<earth::FlightController> flightController_;
    
    // Subsystems
    std::unique_ptr<TileScheduler> scheduler_;
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<ShaderManager> shaderManager_;
    std::unique_ptr<TileRenderer> tileRenderer_;
    bool textureArrayRequested_ = false;
    bool textureArrayEffective_ = false;
    int textureArrayMaxLayers_ = 0;
    std::unique_ptr<TileMeshScheduler> meshScheduler_;
    std::unique_ptr<DemManager> demManager_;
    int demProviderEffectiveMaxZoom_ = 15;  // Runtime effective DEM cap after provider clamp
    bool didAutoFallback_ = false;          // Per-instance fallback guard (resets on Init)
    static constexpr int kMaxNoDataFloorSkipLogs = 8;
    int noDataFloorSkipLogCount_ = 0;       // DEM debug log limiter for no-data floor skip
    std::unique_ptr<RockMeshManager> rockMeshManager_;
    TilePyramid tilePyramid_;
    JobSystem jobSystem_;
    std::unique_ptr<RenderFrame> renderFrame_;
    std::unique_ptr<AtmosphereRenderer> atmosphereRenderer_;  // P0-1: Sky dome renderer
    LayerManager layerManager_;
    
    // Tiles
    std::unordered_map<TileKey, Tile> tiles_;
    std::unordered_set<TileKey> baseTileKeys_;

    // P0-3: BuildNextScene -> RenderScene snapshot (double-buffered, thread-safe).
    // SceneSnapshot is immutable after handoff - only pure-copy fields, no pointers.
    struct SceneSnapshot {
        bool valid = false;
        uint64_t frameId = 0;           // P0-3: Frame correlation ID
        uint64_t version = 0;           // P0-3: Monotonic version for ordering
        glm::mat4 mvp{1.0f};
        glm::vec3 cameraPos{0.0f};
        std::unordered_set<TileKey> leafSet;
        double currentTime = 0.0;
        uint32_t loadingTexture = 0;
        bool wireframe = false;
        bool useLogDepth = false;
        float logDepthFarKm = 1.0f;
        
        // P0-1: Atmosphere settings (immutable snapshot)
        bool atmosphereEnabled = true;
        float atmosphereTurbidity = 2.0f;
        float atmosphereIntensity = 1.0f;
        float atmosphereGroundColor[3] = {0.05f, 0.06f, 0.09f};
        
        // P1-5: GPU Terrain Morph settings (immutable snapshot)
        bool useDistanceBasedTerrainMorph = true;
        float terrainMorphDistanceRangeKm = 0.2f;
        
        // P0-3: Immutable snapshot - deep copy semantics
        SceneSnapshot() = default;
        SceneSnapshot(const SceneSnapshot&) = default;
        SceneSnapshot& operator=(const SceneSnapshot&) = default;
        SceneSnapshot(SceneSnapshot&&) = default;
        SceneSnapshot& operator=(SceneSnapshot&&) = default;
    };
    
    // P0-3: Double-buffered snapshots for lock-free handoff
    // Index 0: Buffer A
    // Index 1: Buffer B
    // Atomic readBufferIndex_ tells Render which buffer to read from
    // Update always writes to the OTHER buffer, then atomically swaps
    std::array<SceneSnapshot, 2> sceneSnapshots_;
    std::atomic<size_t> readBufferIndex_{0};   // Which buffer Render reads from (0 or 1)
    std::atomic<uint64_t> snapshotVersion_{0}; // Monotonic version for ordering
    std::mutex snapshotMutex_;                 // Protects write sequence and version increment
    
    // Current leaf set for render filtering (updated each frame in Update)
    std::unordered_set<TileKey> currentLeafSet_;
    // Render leaf set with temporal hold (prevents gaps during fast pan/zoom)
    std::unordered_set<TileKey> renderLeafSet_;
    std::unordered_set<TileKey> prevResetCornerLodKeys_;  // Previous frame's leaf keys for cornerLods cleanup
    std::unordered_map<TileKey, double> lastLeafSeenTime_;
    double leafHoldSeconds_ = 0.5;
    uint64_t leafUnderflowFrames_ = 0;
    int consecutiveLeafUnderflow_ = 0;
    std::unordered_map<TileKey, double> demMeshWaitStartSec_;
    double demWaitAccumMs_ = 0.0;
    uint64_t demWaitSamples_ = 0;
    size_t demTriggeredMeshRebuilds_ = 0;
    glm::dvec3 prevCameraPos_{0.0};
    bool hasPrevCameraPos_ = false;
    glm::vec3 cameraVelocityKmPerSec_{0.0f};
    float cameraSpeedKmPerSec_ = 0.0f;
    float currentNearPlaneKm_ = 0.001f;
    float currentFarPlaneKm_ = 30000.0f;
    bool frameRequested_ = true;
    
    // Mesh rebuild via JobSystem (time-budgeted, visible-priority)
    static constexpr int MAX_MESH_REBUILDS_PER_FRAME = 4;
    std::unordered_set<TileKey> rebuildPending_;  // Prevent duplicate queue entries
    
    bool QueueMeshBuild(const TileKey& key, bool isVisible);
    // Returns number of mesh uploads applied this frame (for request-driven frame invalidation).
    int ProcessMeshResults();
    
    // Frame timing
    double lastFrameTime_ = 0.0;
    int frameCount_ = 0;
    float fps_ = 0.0f;
    
    // Debug stats (for ImGui panel)
    struct DebugStats {
        float fps = 0.0f;
        double frameAvgMs = 0.0;
        double frameP95Ms = 0.0;
        double frameP99Ms = 0.0;
        double updateMs = 0.0;
        double renderMs = 0.0;
        double lodSelectMs = 0.0;
        double requestLoopMs = 0.0;
        double schedulerUpdateMs = 0.0;
        double textureUploadMs = 0.0;
        int pinnedTileCount = 0;  // FAZ 6: Cache pinning visibility
        double meshBuildMs = 0.0;
        int tileCount = 0;
        int leafCount = 0;
        int requiredCount = 0;
        int pendingFetches = 0;
        int pendingDecodes = 0;
        int activeFetches = 0;
        size_t fetchQueueSize = 0;
        size_t decodeQueueSize = 0;
        double avgFetchMs = 0.0;
        double avgDecodeMs = 0.0;
        size_t droppedFetchResults = 0;
        size_t droppedDecodeResults = 0;
        size_t decodedCacheReadHits = 0;
        size_t decodedCacheReadMisses = 0;
        size_t decodedCacheWrites = 0;
        size_t decodedCacheWriteRejects = 0;
        size_t decodedCacheEvictions = 0;
        size_t decodedCacheEntries = 0;
        size_t decodedCacheBytesUsed = 0;
        size_t decodeBypassHits = 0;
        size_t memoryCacheReadHits = 0;
        size_t memoryCacheReadMisses = 0;
        size_t memoryCacheWrites = 0;
        size_t memoryCacheWriteRejects = 0;
        size_t memoryCacheEvictions = 0;
        size_t memoryCacheEntries = 0;
        size_t memoryCacheBytesUsed = 0;
        size_t diskCacheReadHits = 0;
        size_t diskCacheReadMisses = 0;
        size_t diskCacheWrites = 0;
        size_t diskCacheWriteFails = 0;
        size_t networkFetches = 0;
        size_t totalFetchRequests = 0;
        size_t fetchAuthFails = 0;
        size_t fetchRateLimited = 0;
        size_t fetchHttpErrors = 0;
        int visibleTiles = 0;
        int currentZoom = 0;
        double altitude = 0.0;
        double latitude = 0.0;
        double longitude = 0.0;
        double heading = 0.0;
        double tilt = 0.0;
        size_t textureMemoryMB = 0;
        // Gap-free telemetry
        int renderableLeaves = 0;   // Leaves rendered normally
        int crossfadingLeaves = 0;  // Leaves currently blending from parent
        int fallbackTiles = 0;      // Parent fallback tiles
        int placeholderTiles = 0;   // Last-resort placeholder
        int leafNoMesh = 0;         // Leaves without mesh
        int leafNoTexture = 0;      // Leaves with mesh but no texture
        int leafNoTerrain = 0;      // Leaves with mesh+texture but missing terrain data (DEM)
        int renderFallbackDivergenceLeaves = 0;  // Render leaf set still needed fallback (should converge to 0)
        int demUsedButCoverageMismatchLeaves = 0; // DEM coverage exists but tile.demUsed is false
        // Render-time child quorum (post-selection): how often we had to collapse children to an ancestor
        // due to missing render prerequisites (prevents mixed-LOD tearing/cliff walls at joins).
        int renderQuorumDowngrades = 0;
        int renderQuorumNoMesh = 0;
        int renderQuorumNoTexture = 0;
        int renderQuorumNoTerrain = 0;
        int missingTiles = 0;       // True gaps
        int drawCalls = 0;
        int trianglesRendered = 0;
        bool atlasEnabled = false;
        int atlasPages = 0;
        int atlasUsedSlots = 0;
        int atlasCapacitySlots = 0;
        bool textureArrayRequested = false;
        bool textureArrayEffective = false;
        int textureArrayMaxLayers = 0;
        int instancedBatches = 0;
        int instancedTiles = 0;
        int instancedArrayBatches = 0;
        int instancedArrayTiles = 0;
        int instancedArraySkipsNotArray = 0;
        int instancedArraySkipsMissingLayer = 0;
        int arrayMetadataInvalidSkips = 0;
        int arrayCrossfadeTo2dFallbacks = 0;
        int arraySinglePathFallbacks = 0;
        float cameraSpeedKmPerSec = 0.0f;
        double demWaitMs = 0.0;
        size_t meshRebuildCount = 0;
        uint64_t leafUnderflowFrames = 0;
        int seamEdgeCount = 0;
        double avgEdgeHeightDeltaM = 0.0;
        int demFlatLeaves = 0;         // Visible leaves with no DEM baked (flat ellipsoid mesh)
        int demPendingLeaves = 0;      // Visible leaves still awaiting exact DEM rebuild
        int demCoarseningCascadeTiles = 0;         // Unstable-path tiles clamped by cascade guard
        int demPendingMissingOwnTarget = 0;        // demPending reason: missing target key
        int demPendingMissingEdgeCoherent = 0;     // demPending reason: missing coherent edge key
        int demPendingMissingNeighborParent = 0;   // demPending reason: missing coarser-neighbor parent
        int demPendingParentOnlyBlocks = 0;        // edgeCoarserMask==0 but neighbor-parent bit set
        int edgePackAtomicRebuilds = 0;            // Atomic edge-pack + availability rebuilds
        int seamLatchResetCount = 0;               // Latch resets after stable structural change
        int meshRevisionBumpsFrame = 0;            // Tiles bumped once in single-commit stage
        int meshRevisionDoubleBumpTiles = 0;       // Tiles with >1 revision reason bit in a frame
        int tilesUsingAncestorDem = 0;
        std::string terrainMode;                   // Resolved startup terrain mode
        std::string terrainModeReason;             // Why that terrain mode was chosen
        size_t demCoEvictions = 0;
        double seamGapP95M = 0.0;
        double seamGapMaxM = 0.0;
        int cliffEdgeCount = 0;
        double ancestorDemRatio = 0.0;
        // Request-stall diagnostics
        int maxLeafLevel = 0;
        float sseEffectiveThreshold = 0.0f;
        float tiltFactor = 0.0f;
        int staleTileCount = 0;
        uint64_t stallFrames = 0;
        // RockMesh (Google Earth 3D Buildings) telemetry
        int rockMeshUploaded = 0;        // Successfully uploaded meshes
        int rockMeshPending = 0;         // Pending requests in queue
        int rockMeshInFlight = 0;        // Currently fetching
        int rockMeshFailed = 0;          // Failed to load
        int rockMeshDiskCacheHits = 0;   // Served from disk cache
        int rockMeshDiskCacheMisses = 0; // Network fetch required
        int rockMeshStaleDrops = 0;      // Dropped due to stale generation
        float rockMeshFade = 1.0f;       // Current fade value (0.0-1.0)
        bool rockMeshEnabled = false;    // Whether RockMesh system is active
        // P0-P2: RockMesh vertex explosion mitigation counters
        int rockMeshDiscardInvalidTransform = 0;    // Discarded: non-finite transform matrix
        int rockMeshDiscardInvalidScale = 0;        // Discarded: invalid scale
        int rockMeshDiscardInvalidBounds = 0;       // Discarded: AABB too large
        int rockMeshDiscardNonFiniteVertex = 0;     // Discarded: non-finite vertex positions
        int rockMeshDiscardAabbExceeded = 0;        // Discarded: AABB diagonal exceeds threshold
        int rockMeshDiscardVertexDistanceExceeded = 0; // Discarded: vertex distance from origin exceeds threshold
        int rockMeshFallbackTextureUsed = 0;        // Fallback texture used for textureless meshes
    };
    DebugStats debugStats_;
    bool showDebugPanel_ = true;
    double lastRenderStatsLogTimeSec_ = 0.0;
    double lastViewDebugLogTimeSec_ = 0.0;

    // Render-time quorum counters (computed during Update; displayed in RenderDebugPanel).
    int renderQuorumDowngrades_ = 0;
    int renderQuorumNoMesh_ = 0;
    int renderQuorumNoTexture_ = 0;
    int renderQuorumNoTerrain_ = 0;
    double lastFetchFailResetTime_ = 0.0;
    // Request-stall detection
    int prevLeafCount_ = 0;
    double prevAltitudeKm_ = 0.0;
    uint64_t stallFrameCounter_ = 0;
    int staleTileCount_ = 0;
    uint64_t frameSerial_ = 0;
    int adaptiveBaseMaxInFlightFetches_ = 0;
    double adaptiveBaseUploadBudgetMs_ = 0.0;
    double adaptiveBaseMeshUploadBudgetMs_ = 0.0;
    double adaptivePressure_ = 0.0;
    size_t demCoEvictions_ = 0;
    int demCoarseningCascadeTilesFrame_ = 0;
    int demPendingMissingOwnTargetFrame_ = 0;
    int demPendingMissingEdgeCoherentFrame_ = 0;
    int demPendingMissingNeighborParentFrame_ = 0;
    int demPendingParentOnlyBlocksFrame_ = 0;
    int edgePackAtomicRebuildsFrame_ = 0;
    int seamLatchResetCountFrame_ = 0;
    int renderFallbackDivergenceLeavesFrame_ = 0;
    int meshRevisionBumpsFrame_ = 0;
    int meshRevisionDoubleBumpTilesFrame_ = 0;

    FrameTimings frameTimings_;
    FrameTimeTracker frameTimeTracker_;
    
    // ImGui
    void InitImGui();
    void ShutdownImGui();
    void RenderDebugPanel();
    
    // Pivot gizmo (Google Earth style target icon)
    void InitPivotGizmo();
    void RenderPivot(const glm::mat4& viewProj);
    unsigned int pivotVao_ = 0;
    unsigned int pivotVbo_ = 0;
    unsigned int pivotProgram_ = 0;
    int pivotMvpLoc_ = -1;
    int pivotColorLoc_ = -1;
    int pivotVertexCount_ = 0;

    // Lifecycle guards
    bool shutdown_ = false;
    bool glfwInitialized_ = false;
    bool glReady_ = false;
    
    // P0-2: Instance-local callback setup flag (replaces static bool)
    bool maxHeightCallbackSet_ = false;
};

} // namespace globe
