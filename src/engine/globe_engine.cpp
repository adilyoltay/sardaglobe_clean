#include "globe_engine.h"
#include "../core/ellipsoid.h"
#include "../math/tile_math.h"
#include "../rendering/texture_manager.h"
#include "../rendering/shader_manager.h"
#include "../rendering/tile_renderer.h"
#include "../rendering/tile_mesh_builder.h"
#include "../rendering/corner_lod.h"
#include "../rendering/mesh_template.h"
#include "../scheduling/tile_state_machine.h"
#include "../math/frustum.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <numeric>

// ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// Network debug panel
#include "../debug/network_panel.h"

namespace globe {

GlobeEngine::GlobeEngine(const Config& config)
    : config_(config) {
}

GlobeEngine::~GlobeEngine() {
    Shutdown();
}

bool GlobeEngine::Init() {
    // Init GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return false;
    }
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    
    window_ = glfwCreateWindow(config_.windowWidth, config_.windowHeight, 
                               "Native Globe", nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    glfwMakeContextCurrent(window_);
    glfwSetWindowUserPointer(window_, this);
    
    // Set callbacks
    glfwSetKeyCallback(window_, KeyCallback);
    glfwSetScrollCallback(window_, ScrollCallback);
    glfwSetMouseButtonCallback(window_, MouseButtonCallback);
    glfwSetCursorPosCallback(window_, CursorPosCallback);
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    
    // Init GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD" << std::endl;
        return false;
    }
    
    std::cout << "OpenGL: " << glGetString(GL_VERSION) << std::endl;
    
    // Init camera system (Google Earth parity)
    camera_ = std::make_unique<earth::PerspectiveCamera>();
    camera_->SetFov(config_.fovDegrees);
    camera_->SetAspectRatio(static_cast<double>(config_.windowWidth) / config_.windowHeight);
    camera_->SetLatLonAlt(39.0, 35.0, 25000000.0);  // Turkey, default view
    
    flightController_ = std::make_unique<earth::FlightController>(*camera_);
    flightController_->OnWindowResize(config_.windowWidth, config_.windowHeight);
    
    // Set pick callback for terrain interaction
    flightController_->SetPickCallback([this](double x, double y, glm::dvec3& outPoint) {
        return PickGlobe(x, y, outPoint);
    });
    
    // Init subsystems
    scheduler_ = std::make_unique<TileScheduler>(config_);
    textureManager_ = std::make_unique<TextureManager>(config_);
    shaderManager_ = std::make_unique<ShaderManager>();
    tileRenderer_ = std::make_unique<TileRenderer>(*shaderManager_);
    renderFrame_ = std::make_unique<RenderFrame>(*tileRenderer_, *shaderManager_);
    
    // Init DEM manager for terrain elevation
    if (config_.demEnabled) {
        DemManager::Config demConfig;
        demConfig.baseUrl = config_.demUrl.empty() ? config_.demBaseUrl : config_.demUrl;
        demConfig.meshN = config_.demMeshN;
        demConfig.cacheSize = config_.demCacheSize;
        demConfig.debug = config_.demDebug;
        demConfig.timeoutSec = 30;
        demConfig.connectTimeoutSec = 10;
        demManager_ = std::make_unique<DemManager>(demConfig);
        
        // Startup health check
        auto health = demManager_->CheckHealth();
        if (health != DemHealthStatus::Healthy) {
            std::cerr << "[DEM] WARNING: DEM endpoint not healthy (" 
                      << DemHealthStatusToString(health) 
                      << "). Terrain will be flat until DEM becomes available." << std::endl;
        }
        
        // Init heightmap manager for GPU terrain displacement
        heightmapManager_ = std::make_unique<HeightmapManager>();
    }

    meshScheduler_ = std::make_unique<TileMeshScheduler>(config_, demManager_.get());
    
    // Set scheduler upload callback
    scheduler_->SetUploadCallback([this](Tile& tile) {
        textureManager_->QueueUpload(tile);
    });
    
    // Set eviction callback for heightmap cleanup
    if (heightmapManager_) {
        textureManager_->SetEvictionCallback([this](const TileKey& key) {
            heightmapManager_->Release(key);
        });
    }
    
    // GL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);  // Use LEQUAL for better z-fighting handling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    // Enable VSync to prevent tearing
    glfwSwapInterval(1);
    
    // Initialize ImGui
    InitImGui();
    
    // Initialize pivot gizmo
    InitPivotGizmo();

    // Preload base tiles (LOD 0-1) for gap-free startup coverage
    PreloadBaseTiles();
    
    lastFrameTime_ = glfwGetTime();
    return true;
}

void GlobeEngine::Run() {
    while (!glfwWindowShouldClose(window_)) {
        double currentTime = glfwGetTime();
        double dt = currentTime - lastFrameTime_;
        lastFrameTime_ = currentTime;
        
        // FPS calculation
        frameCount_++;
        static double fpsTime = 0.0;
        fpsTime += dt;
        if (fpsTime >= 1.0) {
            fps_ = frameCount_ / static_cast<float>(fpsTime);
            frameCount_ = 0;
            fpsTime = 0.0;
            
            // Debug output
            double lat, lon, alt;
            camera_->GetLatLonAlt(lat, lon, alt);
            std::cout << "[FPS: " << static_cast<int>(fps_) << "] "
                      << "Tiles: " << tiles_.size() 
                      << " | Pending: " << scheduler_->GetPendingFetches()
                      << " | Alt: " << static_cast<int>(alt/1000) << "km"
                      << std::endl;
        }
        
        glfwPollEvents();
        ProcessInput(currentTime);
        Update(dt, currentTime);

        bool shouldRender = !config_.requestDrivenFrame || frameRequested_;
        if (shouldRender) {
            Render();
            glfwSwapBuffers(window_);
            frameRequested_ = false;
        } else {
            // Reduce idle CPU/GPU usage when no frame is requested.
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
}

void GlobeEngine::Shutdown() {
    ShutdownImGui();
    
    if (demManager_) demManager_->Shutdown();
    if (meshScheduler_) meshScheduler_->Shutdown();
    if (heightmapManager_) heightmapManager_->Clear();
    scheduler_.reset();
    textureManager_.reset();
    shaderManager_.reset();
    meshScheduler_.reset();
    demManager_.reset();
    flightController_.reset();
    camera_.reset();
    
    // Clear tiles
    for (auto& [key, tile] : tiles_) {
        if (tile.ownsTexture && tile.textureId != 0) {
            glDeleteTextures(1, &tile.textureId);
        }
        if (tile.vao != 0) glDeleteVertexArrays(1, &tile.vao);
        if (tile.vbo != 0) glDeleteBuffers(1, &tile.vbo);
        if (tile.ebo != 0 && tile.ownsEBO) glDeleteBuffers(1, &tile.ebo);
    }
    tiles_.clear();

    MeshTemplate::Clear();
    
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void GlobeEngine::ProcessInput(double currentTime) {
    // Flight controller handles all mouse/keyboard navigation
    // Keyboard events are handled via callbacks
}

bool GlobeEngine::GetGeoFromScreenPoint(double screenX, double screenY, double& outLon, double& outLat) {
    return camera_->ScreenToGeo(screenX, screenY, config_.windowWidth, config_.windowHeight, outLon, outLat);
}

bool GlobeEngine::GetScreenPointFromGeo(double lon, double lat, double& outScreenX, double& outScreenY) {
    return camera_->GeoToScreen(lon, lat, 0.0, config_.windowWidth, config_.windowHeight, outScreenX, outScreenY);
}

void GlobeEngine::Update(double dt, double currentTime) {
    double updateStartMs = glfwGetTime() * 1000.0;

    // Update flight controller (handles momentum, animations)
    flightController_->Update(dt, currentTime);
    
    // Get camera position (in km from EarthCamera)
    glm::dvec3 cameraPosD = camera_->GetPositionECEF();
    glm::vec3 cameraPos = glm::vec3(cameraPosD);

    // Camera speed telemetry for unpop speed limit.
    cameraSpeedKmPerSec_ = 0.0f;
    cameraVelocityKmPerSec_ = glm::vec3(0.0f);
    if (hasPrevCameraPos_ && dt > 1e-6) {
        glm::dvec3 deltaKm = cameraPosD - prevCameraPos_;
        double distKm = glm::length(deltaKm);
        double speedKmPerSec = distKm / dt;
        if (std::isfinite(speedKmPerSec)) {
            cameraSpeedKmPerSec_ = static_cast<float>(std::clamp(speedKmPerSec, 0.0, 1.0e6));
        }
        if (std::isfinite(deltaKm.x) && std::isfinite(deltaKm.y) && std::isfinite(deltaKm.z)) {
            glm::dvec3 vel = deltaKm / dt;
            cameraVelocityKmPerSec_ = glm::vec3(vel);
        }
    }
    prevCameraPos_ = cameraPosD;
    hasPrevCameraPos_ = true;
    
    // Dynamic near/far planes based on altitude (Google Earth style)
    // This prevents z-fighting at both close and far distances
    double cameraDistKm = glm::length(cameraPosD);
    double altitudeKm = cameraDistKm - EARTH_RADIUS_KM;
    
    // Near plane: 1% of altitude, minimum 0.001 km (1 meter)
    // Far plane: enough to see the Earth + some margin
    double nearPlane = std::max(0.001, altitudeKm * 0.01);
    double farPlane = cameraDistKm + EARTH_RADIUS_KM * 2.0;
    camera_->SetNearFar(nearPlane, farPlane);
    camera_->SetReverseZEnabled(config_.reversedZEnabled);
    currentNearPlaneKm_ = static_cast<float>(nearPlane);
    currentFarPlaneKm_ = static_cast<float>(farPlane);
    
    // Get camera matrices
    glm::dmat4 view = camera_->GetViewMatrix();
    glm::dmat4 proj = camera_->GetProjectionMatrix();
    glm::dmat4 mvpD = proj * view;
    glm::mat4 mvp = glm::mat4(mvpD);
    
    // LOD selection via TilePyramid (GE-style centralized management)
    auto& lodSettings = tilePyramid_.GetSettings();
    lodSettings.minZoom = config_.minZoom;
    lodSettings.maxZoom = config_.maxZoom;
    lodSettings.disableFrustumCull = config_.disableFrustumCull;
    lodSettings.disableHorizonCull = config_.disableHorizonCull;
    lodSettings.lodChildQuorum = config_.lodChildQuorum;
    
    // TiltFactor: reduce detail when camera is tilted toward horizon
    // tilt=0 (looking down) → tiltFactor=1.0 (full detail)
    // tilt=90 (looking at horizon) → tiltFactor=0.0 (reduced detail)
    double tilt = camera_->GetTilt();  // degrees, 0=looking down, 90=looking at horizon
    lodSettings.tiltFactor = static_cast<float>(1.0 - std::clamp(tilt / 90.0, 0.0, 1.0));

    // Adaptive SSE tuning (phase 4):
    // - Low altitude: request more detail (smaller threshold).
    // - High tilt: preserve extra detail to avoid excessive blur in oblique views.
    double altitudeNorm = std::clamp(altitudeKm / 2000.0, 0.0, 1.0);
    double tiltNorm = std::clamp(tilt / 75.0, 0.0, 1.0);
    double altitudeScale = (0.55 * (1.0 - altitudeNorm)) + (1.0 * altitudeNorm);
    double tiltScale = (1.0 * (1.0 - tiltNorm)) + (0.85 * tiltNorm);
    double adaptiveSse = static_cast<double>(config_.sseThreshold) * altitudeScale * tiltScale;
    adaptiveSse = std::clamp(adaptiveSse,
                             static_cast<double>(config_.sseThreshold) * 0.45,
                             static_cast<double>(config_.sseThreshold) * 1.15);
    lodSettings.sseThreshold = static_cast<float>(adaptiveSse);
    
    // CRITICAL: Pass FOV directly from camera, not extracted from MVP
    float fovDegrees = static_cast<float>(camera_->GetFov());
    float tiltDegrees = static_cast<float>(camera_->GetTilt());
    
    // Get view direction from camera for center bias scoring
    glm::dvec3 forward, up, right;
    camera_->GetBasisVectors(forward, up, right);
    glm::vec3 viewDir = glm::vec3(forward);
    
    double lodStartMs = glfwGetTime() * 1000.0;
    const LodSelection& selection = tilePyramid_.Select(
        cameraPos, cameraVelocityKmPerSec_, viewDir, mvp, fovDegrees, tiltDegrees,
        config_.windowWidth, config_.windowHeight,
        tiles_
    );
    frameTimings_.lodSelectMs = (glfwGetTime() * 1000.0) - lodStartMs;
    
    // Store leafSet for render filtering.
    // Safety: if selector underflows once, reuse previous non-empty leaves for one frame.
    std::unordered_set<TileKey> previousLeafSet = currentLeafSet_;
    if (selection.leafSet.empty() && !selection.required.empty()) {
        ++leafUnderflowFrames_;
    }
    if (selection.leafSet.empty() && !previousLeafSet.empty() && consecutiveLeafUnderflow_ == 0) {
        currentLeafSet_ = previousLeafSet;
        consecutiveLeafUnderflow_ = 1;
    } else {
        currentLeafSet_ = selection.leafSet;
        if (selection.leafSet.empty()) {
            ++consecutiveLeafUnderflow_;
        } else {
            consecutiveLeafUnderflow_ = 0;
        }
    }
    
    // Temporal leaf hold: keep recent leaves to avoid gaps during fast pan/zoom
    double nowTime = glfwGetTime();
    for (const TileKey& key : currentLeafSet_) {
        lastLeafSeenTime_[key] = nowTime;
    }
    renderLeafSet_.clear();
    for (auto it = lastLeafSeenTime_.begin(); it != lastLeafSeenTime_.end(); ) {
        if (nowTime - it->second <= leafHoldSeconds_) {
            renderLeafSet_.insert(it->first);
            ++it;
        } else {
            it = lastLeafSeenTime_.erase(it);
        }
    }
    // If no leaves are available (startup edge case), render base tiles as a fallback
    if (renderLeafSet_.empty() && !baseTileKeys_.empty()) {
        renderLeafSet_ = baseTileKeys_;
    }
    
    double requestStartMs = glfwGetTime() * 1000.0;
    const uint32_t loadingTextureId = textureManager_ ? textureManager_->GetLoadingTexture() : 0;
    const TileScheduler::SchedulerStats schedulerStatsBefore = scheduler_
        ? scheduler_->GetStats()
        : TileScheduler::SchedulerStats{};
    const bool schedulerIdleBefore =
        (schedulerStatsBefore.pendingFetches == 0) &&
        (schedulerStatsBefore.pendingDecodes == 0) &&
        (schedulerStatsBefore.activeFetches == 0) &&
        (schedulerStatsBefore.fetchResultQueue == 0) &&
        (schedulerStatsBefore.decodeResultQueue == 0);

    // Child-quorum starvation prevention:
    // When a visible leaf wants to subdivide but its children aren't ready yet, the selector
    // keeps the parent as a leaf and adds all 4 children to the required set. Those children
    // must be fetched with Urgent priority; otherwise they can be starved by backpressure and
    // the engine appears to "stop loading new tiles" on zoom.
    std::unordered_set<TileKey> quorumUrgentChildren;
    if (config_.lodChildQuorum) {
        quorumUrgentChildren.reserve(selection.leafSet.size() * 4);
        for (const TileKey& leaf : selection.leafSet) {
            if (leaf.level >= tilePyramid_.GetSettings().maxZoom) {
                continue;
            }
            auto children = leaf.Children();
            bool allChildrenRequired = true;
            for (const TileKey& child : children) {
                if (selection.required.count(child) == 0) {
                    allChildrenRequired = false;
                    break;
                }
            }
            if (!allChildrenRequired) {
                continue;
            }
            for (const TileKey& child : children) {
                quorumUrgentChildren.insert(child);
            }
        }
    }

    // Request required tiles using ranked list (GE-style SSE + center bias priority)
    for (const RankedTile& ranked : tilePyramid_.GetRankedRequired()) {
        const TileKey& key = ranked.key;
        
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            // Create tile entry
            Tile tile(key);
            tile.center = TileCenterWorld(key);
            tile.boundingRadius = TileBoundingRadius(key);
            tile.angularRadius = TileAngularRadius(key);
            tile.demTargetLevel = static_cast<uint8_t>(std::clamp(key.level, 0, 255));
            tile.demEffectiveLevel = tile.demTargetLevel;
            {
                const uint32_t lvl = static_cast<uint32_t>(tile.demTargetLevel);
                tile.demEdgeLevelPack = lvl | (lvl << 8) | (lvl << 16) | (lvl << 24);
            }
            tiles_.emplace(key, std::move(tile));
            it = tiles_.find(key);
        }
        
        Tile& tile = it->second;
        tile.lastAccessTime = currentTime;
        tile.accessCount++;
        tile.importance = ranked.score;  // Store score for eviction decisions
        bool isLeaf = selection.leafSet.count(key) > 0;
        Priority priority = isLeaf ? Priority::Urgent : Priority::Normal;
        if (!isLeaf && config_.lodChildQuorum && quorumUrgentChildren.count(key) > 0) {
            priority = Priority::Urgent;
        }
        tile.requestPriority = static_cast<uint8_t>(priority);
        
        if (tile.state == TileState::Unloaded) {
            if (scheduler_->Request(key, priority, ranked.score)) {
                TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
            }
        }
        else if (tile.IsLoading()) {
            // Stale intermediate state recovery: if a tile is stuck in
            // Scheduled/Fetching/Decoding/Uploading for >5s with no worker
            // activity, reset it to Unloaded so it can be re-fetched.
            constexpr double kStaleScheduledSec = 3.0;
            constexpr double kStaleFetchingSec = 20.0;
            constexpr double kStaleDecodingSec = 12.0;
            constexpr double kStaleUploadingSec = 12.0;
            if (tile.stateEnterTime <= 0.0) {
                tile.stateEnterTime = currentTime;
            }
            double age = std::max(0.0, currentTime - tile.stateEnterTime);
            double timeoutSec = kStaleFetchingSec;
            if (tile.state == TileState::Scheduled) timeoutSec = kStaleScheduledSec;
            else if (tile.state == TileState::Decoding) timeoutSec = kStaleDecodingSec;
            else if (tile.state == TileState::Uploading) timeoutSec = kStaleUploadingSec;
            // If the scheduler is totally idle but we still have tiles in loading states,
            // assume an orphaned in-flight marker and recover faster.
            if (schedulerIdleBefore) {
                timeoutSec = std::min(timeoutSec, 6.0);
            }
            if (age > timeoutSec) {
                TileStateMachine::Advance(tile, TileStateMachine::Event::Evict, currentTime);
                if (scheduler_->Request(key, priority, ranked.score)) {
                    TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                    TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
                }
            }
        }
        else if (tile.state == TileState::Failed) {
            // Exponential backoff for failed tiles (prevents hammering failed servers)
            // Backoff: 1s, 2s, 4s, 8s, 16s, max 32s
            double backoffSeconds = std::min(32.0, std::pow(2.0, tile.retryCount));
            double timeSinceLastRetry = currentTime - tile.lastRetryTime;

            const bool hasPlaceholderTexture =
                (tile.textureId != 0 && tile.textureId == loadingTextureId);
            // Keep retrying tiles that still have no real imagery (especially coarse/base tiles).
            // A hard retry cap here can leave permanent black placeholder patches.
            const int retryLimit = hasPlaceholderTexture ? 1000000 : 5;

            if (timeSinceLastRetry >= backoffSeconds && tile.retryCount < retryLimit) {
                if (scheduler_->Request(key, priority, ranked.score)) {
                    TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                    TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
                }
            }
        }

        // P5.3: Coordinate DEM request with imagery request in the same traversal.
        if (demManager_) {
            int demPriority = isLeaf ? 2 : 1;
            demManager_->Request(key, demPriority, ranked.score);
        }
    }
    
    // Prefetch tiles using ranked list (low priority, score-ordered)
    int prefetchCount = 0;
    int availablePrefetch = config_.maxTiles - static_cast<int>(tiles_.size());
    if (availablePrefetch < 0) {
        availablePrefetch = 0;
    }
    const int maxPrefetch = std::min(8, availablePrefetch);
    for (const RankedTile& ranked : tilePyramid_.GetRankedPrefetch()) {
        if (prefetchCount >= maxPrefetch) break;
        
        const TileKey& key = ranked.key;
        
        // Skip if already required
        if (tilePyramid_.IsRequired(key)) continue;
        
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            // Create tile entry for prefetch
            Tile tile(key);
            tile.center = TileCenterWorld(key);
            tile.boundingRadius = TileBoundingRadius(key);
            tile.angularRadius = TileAngularRadius(key);
            tile.demTargetLevel = static_cast<uint8_t>(std::clamp(key.level, 0, 255));
            tile.demEffectiveLevel = tile.demTargetLevel;
            {
                const uint32_t lvl = static_cast<uint32_t>(tile.demTargetLevel);
                tile.demEdgeLevelPack = lvl | (lvl << 8) | (lvl << 16) | (lvl << 24);
            }
            tiles_.emplace(key, std::move(tile));
            it = tiles_.find(key);
        }
        
        Tile& tile = it->second;
        tile.importance = ranked.score;
        tile.requestPriority = static_cast<uint8_t>(Priority::Low);
        
        if (tile.state == TileState::Unloaded) {
            tile.lastAccessTime = currentTime;
            tile.accessCount++;
            if (scheduler_->Request(key, Priority::Low, ranked.score)) {  // Low priority prefetch with score
                TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
                ++prefetchCount;
            }
        }
    }
    frameTimings_.requestLoopMs = (glfwGetTime() * 1000.0) - requestStartMs;
    
    // Process scheduler (fetch/decode results)
    double schedulerStartMs = glfwGetTime() * 1000.0;
    scheduler_->Update(tiles_, currentTime);
    frameTimings_.schedulerUpdateMs = (glfwGetTime() * 1000.0) - schedulerStartMs;

    // Reset recent fetch-fail metric (every 2 seconds)
    if (nowTime - lastFetchFailResetTime_ >= 2.0) {
        scheduler_->ResetRecentFetchFails();
        lastFetchFailResetTime_ = nowTime;
    }
    
    // Process texture uploads (time-budgeted)
    double uploadStartMs = glfwGetTime() * 1000.0;
    textureManager_->ProcessUploads(tiles_, config_.uploadBudgetMs);
    frameTimings_.textureUploadMs = (glfwGetTime() * 1000.0) - uploadStartMs;
    
    // DEM update + optional heightmap upload.
    // Requests are already coordinated in the required imagery traversal above (P5.3).
    if (demManager_) {
        // Pin visible DEM chain (leaves + 1-ring neighbors) against cache eviction.
        std::vector<TileKey> demPinnedKeys;
        demPinnedKeys.reserve(static_cast<size_t>(config_.demVisiblePinBudget));
        std::unordered_set<TileKey> demPinnedSet;
        demPinnedSet.reserve(static_cast<size_t>(config_.demVisiblePinBudget));
        auto pushPinned = [&](const TileKey& key) {
            if (config_.demVisiblePinBudget <= 0) {
                return;
            }
            if (static_cast<int>(demPinnedKeys.size()) >= config_.demVisiblePinBudget) {
                return;
            }
            if (demPinnedSet.insert(key).second) {
                demPinnedKeys.push_back(key);
            }
        };

        auto keyAtLevel = [](TileKey k, int targetLevel) -> TileKey {
            int lvl = std::clamp(targetLevel, 0, k.level);
            while (k.level > lvl) {
                k = k.Parent();
            }
            return k;
        };

        // Always pin coarse base DEM tiles to guarantee global ancestor fallback.
        for (const TileKey& base : baseTileKeys_) {
            pushPinned(base);
        }

        // Pin DEM keys actually used by visible leaf meshes first.
        // NOTE: DEM requests are issued for the *imagery* required set, but mesh sampling can
        // intentionally lock to ancestor keys (demTargetLevel / demEdgeLevelPack) for coherence.
        // Pinning leaf keys alone is insufficient; the cache would evict the shared ancestors
        // and reintroduce "each tile lifts independently" cliffs/cracks.
        for (const TileKey& leaf : selection.leafSet) {
            auto it = tiles_.find(leaf);
            if (it == tiles_.end()) continue;
            const Tile& tile = it->second;
            int lvl = std::clamp(static_cast<int>(tile.demTargetLevel), 0, leaf.level);
            pushPinned(keyAtLevel(leaf, lvl));
        }

        // Pin edge-coherent DEM keys (often coarser, shared across neighbors) if budget allows.
        for (const TileKey& leaf : selection.leafSet) {
            if (static_cast<int>(demPinnedKeys.size()) >= config_.demVisiblePinBudget) {
                break;
            }
            auto it = tiles_.find(leaf);
            if (it == tiles_.end()) continue;
            const Tile& tile = it->second;

            const uint32_t pack = tile.demEdgeLevelPack;
            int edgeLvls[4] = {
                static_cast<int>(pack & 0xFFu),
                static_cast<int>((pack >> 8) & 0xFFu),
                static_cast<int>((pack >> 16) & 0xFFu),
                static_cast<int>((pack >> 24) & 0xFFu)
            };
            for (int dir = 0; dir < 4; ++dir) {
                int lvl = std::clamp(edgeLvls[dir], 0, leaf.level);
                pushPinned(keyAtLevel(leaf, lvl));
            }
        }
        demManager_->SetPinnedTiles(demPinnedKeys);

        demManager_->Update();
        
        // Queue DEM data for GPU heightmap texture upload only when DEM endpoint is healthy.
        // Otherwise mixed displaced/non-displaced tiles cause visible artifacts.
        bool gpuDisplacementAllowed =
            (config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE) &&
            (demManager_->GetHealthStatus() == DemHealthStatus::Healthy);
        if (heightmapManager_ && gpuDisplacementAllowed) {
            for (const TileKey& key : selection.required) {
                if (demManager_->HasData(key) && !heightmapManager_->HasTexture(key)) {
                    DemGridData demData;
                    if (demManager_->GetGridData(key, demData)) {
                        heightmapManager_->QueueUpload(key, demData, static_cast<float>(config_.demHeightScale));
                    }
                }
            }
            // Process pending heightmap uploads
            heightmapManager_->ProcessUploads(2.0);  // 2ms budget
        }
    }
    
    // Compute edge masks + DEM effective level for seam/cliff fixes.
    // An edge is "coarser" if the neighbor at same level is NOT a leaf but its parent IS.
    const int expectedSegments = demManager_ ? std::max(config_.meshSegments, config_.demMeshN - 1) : config_.meshSegments;

    // Reset corner LOD uniforms each frame; only current leaves get non-zero values.
    for (auto& [_, tile] : tiles_) {
        tile.cornerLods = glm::vec4(0.0f);
    }

    auto resolveBestAvailableDemLevel = [&](const TileKey& tileKey) -> int {
        if (!demManager_) {
            return tileKey.level;
        }
        int bestLevel = tileKey.level;
        if (demManager_->GetBestAvailableLevel(tileKey, bestLevel)) {
            return std::clamp(bestLevel, 0, tileKey.level);
        }
        // No cached DEM for this tile or any ancestor yet. Target the global root
        // so meshes can immediately fall back once base DEM arrives.
        return 0;
    };

    auto leafRegionHasCoverage = [&](const TileKey& regionKey) -> bool {
        for (const TileKey& leaf : selection.leafSet) {
            if (leaf.level < regionKey.level) {
                continue;
            }
            TileKey probe = leaf;
            while (probe.level > regionKey.level) {
                probe = probe.Parent();
            }
            if (probe == regionKey) {
                return true;
            }
        }
        return false;
    };

    for (const TileKey& key : selection.leaves) {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) continue;
        
        uint8_t newEdgeCoarserMask = 0;
        uint8_t newSkirtMask = 0;
        if (key.level > 0) {  // Level 0 has no coarser neighbors
            // Check 4 cardinal directions: N(0,-1), E(1,0), S(0,1), W(-1,0)
            static const int dx[] = {0, 1, 0, -1};
            static const int dy[] = {-1, 0, 1, 0};
            static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST, 
                                               Tile::EDGE_SOUTH, Tile::EDGE_WEST};
            
            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) {
                    newSkirtMask |= edgeBits[dir];
                    continue;
                }
                
                // Neighbor is coarser if: neighborSame NOT in leafSet AND neighborSame.Parent() IS in leafSet
                bool neighborSameIsLeaf = selection.leafSet.count(neighborSame) > 0;
                if (!neighborSameIsLeaf) {
                    TileKey neighborParent = neighborSame.Parent();
                    bool neighborParentIsLeaf = selection.leafSet.count(neighborParent) > 0;
                    if (neighborParentIsLeaf) {
                        newEdgeCoarserMask |= edgeBits[dir];
                        // If stitch-mask topology is enabled, rely on crack-free index stitching
                        // instead of skirts on delta-LOD boundaries. Skirts on these edges tend to
                        // produce visible dark grids / walls at oblique views.
                        if (!config_.edgeStitching) {
                            newSkirtMask |= edgeBits[dir];
                        }
                        continue;
                    }

                    // For finer-neighbor coverage, avoid redundant skirts (visible black seams).
                    // Keep skirts only for true coverage holes.
                    if (!leafRegionHasCoverage(neighborSame)) {
                        newSkirtMask |= edgeBits[dir];
                    }
                }
            }
        }

        Tile& tile = it->second;
        int effectiveDemLevel = key.level;
        if (demManager_) {
            const bool selfDemUnstable = tile.demPending || !tile.demUsed;
            effectiveDemLevel = resolveBestAvailableDemLevel(key);
            static const int dx[] = {0, 1, 0, -1};
            static const int dy[] = {-1, 0, 1, 0};
            static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST,
                                               Tile::EDGE_SOUTH, Tile::EDGE_WEST};
            for (int dir = 0; dir < 4; ++dir) {
                bool coarserLodEdge = (newEdgeCoarserMask & edgeBits[dir]) != 0;
                bool openCoverageEdge = (newSkirtMask & edgeBits[dir]) != 0;

                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) {
                    continue;
                }

                // Neighbor leaf coverage can be:
                // - same-LOD leaf (neighborSame)
                // - coarser leaf (neighborSame.Parent()) when we are refined against it
                // - refined leaves (two children) when neighbor region is finer than us.
                //
                // DEM coherence must consider the *rendered* neighbor leaf, otherwise delta-LOD
                // borders can mismatch by kilometers (observed as "each tile lifts independently").
                std::vector<TileKey> neighborLeaves;
                neighborLeaves.reserve(2);
                if (selection.leafSet.count(neighborSame) > 0) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 && selection.leafSet.count(neighborSame.Parent()) > 0) {
                    neighborLeaves.push_back(neighborSame.Parent());
                } else {
                    auto children = neighborSame.Children();
                    int a = -1, b = -1;
                    // Children order: 0=NW,1=NE,2=SW,3=SE.
                    if (edgeBits[dir] == Tile::EDGE_NORTH) { a = 2; b = 3; }
                    else if (edgeBits[dir] == Tile::EDGE_EAST) { a = 0; b = 2; }
                    else if (edgeBits[dir] == Tile::EDGE_SOUTH) { a = 0; b = 1; }
                    else if (edgeBits[dir] == Tile::EDGE_WEST) { a = 1; b = 3; }
                    if (a >= 0) {
                        if (selection.leafSet.count(children[static_cast<std::size_t>(a)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (selection.leafSet.count(children[static_cast<std::size_t>(b)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(b)]);
                        }
                    }
                }

                if (neighborLeaves.empty()) {
                    continue;
                }

                for (const TileKey& neighborLeaf : neighborLeaves) {
                    int neighborLevel = resolveBestAvailableDemLevel(neighborLeaf);

                    bool neighborDemUnstable = true;
                    auto nit = tiles_.find(neighborLeaf);
                    if (nit != tiles_.end()) {
                        const Tile& neighborTile = nit->second;
                        neighborDemUnstable = neighborTile.demPending || !neighborTile.demUsed;
                    }

                    // DEM coherence policy (GE-style):
                    // While either side is still waiting on exact DEM, lock both tiles to the
                    // common ancestor level to avoid large cliffs/walls on shared borders.
                    // Once stable, allow limited mismatch to preserve detail.
                    if (coarserLodEdge || openCoverageEdge || selfDemUnstable || neighborDemUnstable) {
                        effectiveDemLevel = std::min(effectiveDemLevel, neighborLevel);
                    } else if (effectiveDemLevel > neighborLevel + 1) {
                        // Same-LOD neighbors: allow at most one-level mismatch to reduce cliffs
                        // without collapsing whole regions to a deep ancestor level.
                        effectiveDemLevel = neighborLevel + 1;
                    }
                }
            }
            effectiveDemLevel = std::clamp(effectiveDemLevel, 0, key.level);
        }

        // P1: Seam→skirt feedback — if the previous frame measured a significant
        // seam gap on specific edges, enable skirts only on those edges.
        // This prevents the "skirt everywhere" regression that produces visible dark grids.
        if (config_.selectiveSkirts && tile.seamGapMask != 0) {
            newSkirtMask |= tile.seamGapMask;
        }

        uint8_t stitchedMask = config_.edgeStitching ? newEdgeCoarserMask : 0;
        uint8_t resolvedSkirtMask = config_.selectiveSkirts
            ? newSkirtMask
            : static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_EAST | Tile::EDGE_SOUTH | Tile::EDGE_WEST);

        tile.cornerLods = CornerLodsFromEdgeMask(newEdgeCoarserMask);
        bool revisionChanged = false;
        if (newEdgeCoarserMask != tile.edgeCoarserMask) {
            tile.edgeCoarserMask = newEdgeCoarserMask;
            if (tile.edgeCoarserMask != tile.prevEdgeCoarserMask) {
                revisionChanged = true;
            }
        }
        if (tile.stitchMask != stitchedMask) {
            tile.stitchMask = stitchedMask;
            revisionChanged = true;
        }
        if (tile.skirtMask != resolvedSkirtMask) {
            tile.skirtMask = resolvedSkirtMask;
            revisionChanged = true;
        }
        uint8_t newEffectiveLevel = static_cast<uint8_t>(std::clamp(effectiveDemLevel, 0, 255));
        if (tile.demTargetLevel != newEffectiveLevel) {
            tile.demTargetLevel = newEffectiveLevel;
            revisionChanged = true;
        }
        
        // Check DEM availability - check edge-specific coarser neighbor parents
        if (demManager_ && tile.demPending) {
            const bool hasOwnDem = demManager_->HasData(key);
            const bool hasAnyDem = demManager_->HasDataOrAncestor(key);
            bool hasAllCoarserDem = true;
            
            // For each flagged edge, check if the neighbor's parent DEM is available
            // The neighbor's parent is the tile that provides coarser elevation data
            if (key.level > 0 && tile.edgeCoarserMask != 0) {
                static const int edgeDx[] = {0, 1, 0, -1};  // N, E, S, W
                static const int edgeDy[] = {-1, 0, 1, 0};
                static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST,
                                                   Tile::EDGE_SOUTH, Tile::EDGE_WEST};
                
                for (int dir = 0; dir < 4; ++dir) {
                    if (tile.edgeCoarserMask & edgeBits[dir]) {
                        TileKey neighbor = key.Neighbor(edgeDx[dir], edgeDy[dir]);
                        if (neighbor.IsValid()) {
                            TileKey neighborParent = neighbor.Parent();
                            if (!demManager_->HasData(neighborParent)) {
                                hasAllCoarserDem = false;
                                break;
                            }
                        }
                    }
                }
            }
            
            // If this tile was baked "flat" (no DEM at build time), rebuild as soon as
            // *any* ancestor DEM becomes available. Otherwise flat+terrain mixing creates
            // kilometer-scale cliffs and visible skirt walls at tile boundaries.
            if (!tile.demUsed) {
                if (hasAnyDem && !tile.meshPending) {
                    revisionChanged = true;
                    ++demTriggeredMeshRebuilds_;
                }
            } else if (hasOwnDem && hasAllCoarserDem) {
                revisionChanged = true;
                tile.demPending = false;
                ++demTriggeredMeshRebuilds_;
            }
        }
        
        if (tile.builtSegments != 0 && tile.builtSegments != expectedSegments) {
            revisionChanged = true;
        }
        
        if (revisionChanged) {
            ++tile.meshRevision;
        }
    }

    // DEM edge-coherence levels (P3): compute a per-edge "common ancestor" DEM level so
    // both sides of a tile border sample heights from the same DEM tile key. Without this,
    // the same geographic border coordinate can quantize to different DEM tiles (and return
    // different heights), producing residual cracks/cliffs even when LOD stitching is enabled.
    if (demManager_ && config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
        auto packEdgeLevels = [](int north, int east, int south, int west) -> uint32_t {
            uint32_t n = static_cast<uint32_t>(std::clamp(north, 0, 255));
            uint32_t e = static_cast<uint32_t>(std::clamp(east, 0, 255));
            uint32_t s = static_cast<uint32_t>(std::clamp(south, 0, 255));
            uint32_t w = static_cast<uint32_t>(std::clamp(west, 0, 255));
            return n | (e << 8) | (s << 16) | (w << 24);
        };

        auto keyAtLevel = [](TileKey k, int targetLevel) -> TileKey {
            int lvl = std::clamp(targetLevel, 0, k.level);
            while (k.level > lvl) {
                k = k.Parent();
            }
            return k;
        };

        auto commonAncestorLevel = [&](TileKey a, TileKey b) -> int {
            while (a.level > b.level) a = a.Parent();
            while (b.level > a.level) b = b.Parent();
            while (!(a == b)) {
                if (a.level == 0) break;
                a = a.Parent();
                b = b.Parent();
            }
            return a.level;
        };

        static const int dx[] = {0, 1, 0, -1};
        static const int dy[] = {-1, 0, 1, 0};
        static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST,
                                           Tile::EDGE_SOUTH, Tile::EDGE_WEST};

        for (const TileKey& key : selection.leaves) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) continue;
            Tile& tile = it->second;

            int selfDemLevel = std::clamp(static_cast<int>(tile.demTargetLevel), 0, key.level);
            TileKey selfDemKey = keyAtLevel(key, selfDemLevel);

            int edgeLevels[4] = {selfDemLevel, selfDemLevel, selfDemLevel, selfDemLevel};

            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) {
                    continue;
                }

                std::vector<TileKey> neighborLeaves;
                neighborLeaves.reserve(2);
                if (selection.leafSet.count(neighborSame) > 0) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 && selection.leafSet.count(neighborSame.Parent()) > 0) {
                    neighborLeaves.push_back(neighborSame.Parent());
                } else {
                    auto children = neighborSame.Children();
                    int a = -1, b = -1;
                    // Children order: 0=NW,1=NE,2=SW,3=SE.
                    if (edgeBits[dir] == Tile::EDGE_NORTH) { a = 2; b = 3; }
                    else if (edgeBits[dir] == Tile::EDGE_EAST) { a = 0; b = 2; }
                    else if (edgeBits[dir] == Tile::EDGE_SOUTH) { a = 0; b = 1; }
                    else if (edgeBits[dir] == Tile::EDGE_WEST) { a = 1; b = 3; }
                    if (a >= 0) {
                        if (selection.leafSet.count(children[static_cast<std::size_t>(a)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (selection.leafSet.count(children[static_cast<std::size_t>(b)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(b)]);
                        }
                    }
                }

                if (neighborLeaves.empty()) {
                    continue;
                }

                int chosenLevel = selfDemLevel;
                bool chosenInit = false;
                for (const TileKey& neighborLeaf : neighborLeaves) {
                    int neighborDemLevel = neighborLeaf.level;
                    auto nit = tiles_.find(neighborLeaf);
                    if (nit != tiles_.end()) {
                        neighborDemLevel = std::clamp(static_cast<int>(nit->second.demTargetLevel), 0, neighborLeaf.level);
                    } else {
                        neighborDemLevel = resolveBestAvailableDemLevel(neighborLeaf);
                    }
                    TileKey neighborDemKey = keyAtLevel(neighborLeaf, neighborDemLevel);
                    int lcaLevel = commonAncestorLevel(selfDemKey, neighborDemKey);
                    if (!chosenInit) {
                        chosenLevel = lcaLevel;
                        chosenInit = true;
                    } else {
                        chosenLevel = std::min(chosenLevel, lcaLevel);  // coarsest across split neighbors
                    }
                }

                // Seam feedback: if we still measure a significant gap on this edge, bias one level
                // coarser for the border band (often eliminates residual cracks on DEM tile borders).
                if (tile.seamGapMask & edgeBits[dir]) {
                    chosenLevel = std::max(0, chosenLevel - 1);
                }

                edgeLevels[dir] = std::clamp(chosenLevel, 0, selfDemLevel);
            }

            uint32_t packed = packEdgeLevels(edgeLevels[0], edgeLevels[1], edgeLevels[2], edgeLevels[3]);
            if (tile.demEdgeLevelPack != packed) {
                tile.demEdgeLevelPack = packed;
                ++tile.meshRevision;
            }
        }
    }
    
    double meshStartMs = glfwGetTime() * 1000.0;
    // Queue mesh builds for visible leaves (async)
    for (const TileKey& key : selection.leafSet) {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) continue;
        Tile& tile = it->second;
        if (!tile.hasMesh || tile.meshBuiltRevision != tile.meshRevision) {
            QueueMeshBuild(key, true);
        }
    }
    
    // Process completed mesh builds with frame budget
    ProcessMeshResults();
    frameTimings_.meshBuildMs = (glfwGetTime() * 1000.0) - meshStartMs;

    // Cleanup stale unloaded/failed tiles to prevent eviction churn/loops
    const double cleanupNow = glfwGetTime();
    constexpr double UNLOADED_STALE_SEC = 10.0;
    constexpr double FAILED_STALE_SEC = 60.0;
    for (auto it = tiles_.begin(); it != tiles_.end(); ) {
        const TileKey& key = it->first;
        Tile& tile = it->second;
        if (baseTileKeys_.count(key) > 0 ||
            selection.required.count(key) > 0 ||
            tilePyramid_.IsPrefetch(key)) {
            ++it;
            continue;
        }
        if (tile.state == TileState::Unloaded) {
            double last = tile.lastAccessTime;
            if (last <= 0.0 || (cleanupNow - last) > UNLOADED_STALE_SEC) {
                // Release heightmap texture before erasing
                if (heightmapManager_) {
                    heightmapManager_->Release(it->first);
                }
                if (textureManager_) {
                    textureManager_->ReleaseTileResources(tile);
                }
                demMeshWaitStartSec_.erase(it->first);
                it = tiles_.erase(it);
                continue;
            }
        } else if (tile.state == TileState::Failed) {
            double last = tile.lastRetryTime > 0.0 ? tile.lastRetryTime : tile.lastAccessTime;
            if (last <= 0.0 || (cleanupNow - last) > FAILED_STALE_SEC) {
                // Release heightmap texture before erasing
                if (heightmapManager_) {
                    heightmapManager_->Release(it->first);
                }
                if (textureManager_) {
                    textureManager_->ReleaseTileResources(tile);
                }
                demMeshWaitStartSec_.erase(it->first);
                it = tiles_.erase(it);
                continue;
            }
        }
        ++it;
    }
    
    // Pin visible tiles to protect from eviction (GE-style cache policy)
    // Required tiles (leaves + ancestors) + base tiles are pinned
    textureManager_->BeginPinEpoch();
    for (const TileKey& key : selection.required) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            textureManager_->PinTile(it->second);
        }
    }
    for (const TileKey& key : baseTileKeys_) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            textureManager_->PinTile(it->second);
        }
    }
    
    // Evict old tiles (respects pinned tiles)
    textureManager_->EvictIfNeeded(tiles_, config_.maxTiles);

    // BuildNextScene snapshot (P2.1): Render consumes this immutable frame input.
    sceneSnapshot_.valid = true;
    sceneSnapshot_.mvp = mvp;
    sceneSnapshot_.cameraPos = cameraPos;
    sceneSnapshot_.leafSet = renderLeafSet_;
    sceneSnapshot_.currentTime = currentTime;
    sceneSnapshot_.loadingTexture = textureManager_ ? textureManager_->GetLoadingTexture() : 0;
    sceneSnapshot_.wireframe = config_.wireframeMode;
    sceneSnapshot_.useLogDepth = config_.logDepthEnabled && !config_.reversedZEnabled;
    sceneSnapshot_.logDepthFarKm = currentFarPlaneKm_;
    sceneSnapshot_.useHeightmap =
        (config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE) &&
        demManager_ &&
        (demManager_->GetHealthStatus() == DemHealthStatus::Healthy);

    bool hasBackgroundWork = false;
    if (flightController_ && flightController_->IsMoving()) {
        hasBackgroundWork = true;
    }
    if (scheduler_) {
        auto s = scheduler_->GetStats();
        hasBackgroundWork = hasBackgroundWork ||
            (s.pendingFetches > 0) ||
            (s.pendingDecodes > 0) ||
            (s.activeFetches > 0) ||
            (s.fetchResultQueue > 0) ||
            (s.decodeResultQueue > 0);
    }
    if (textureManager_ && textureManager_->GetPendingUploads() > 0) {
        hasBackgroundWork = true;
    }
    if (meshScheduler_ && meshScheduler_->GetPendingCount() > 0) {
        hasBackgroundWork = true;
    }
    if (!rebuildPending_.empty()) {
        hasBackgroundWork = true;
    }
    if (demManager_ && demManager_->GetPendingCount() > 0) {
        hasBackgroundWork = true;
    }
    if (heightmapManager_ && heightmapManager_->GetPendingCount() > 0) {
        hasBackgroundWork = true;
    }

    // Keep rendering while tile fade or terrain morph animations are active.
    if (!hasBackgroundWork) {
        for (const TileKey& key : renderLeafSet_) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) continue;
            const Tile& tile = it->second;
            if ((tile.hasMesh && tile.textureId != 0 && !tile.fadeComplete) || tile.terrainMorphActive) {
                hasBackgroundWork = true;
                break;
            }
        }
    }

    // Step 2C: Detect LOD stall — if leaf count or altitude changed, keep rendering
    // to allow progressive refinement even when no fetch/upload is in flight.
    int currentLeafCount = static_cast<int>(selection.leafSet.size());
    if (currentLeafCount != prevLeafCount_) {
        hasBackgroundWork = true;
    }
    if (std::abs(altitudeKm - prevAltitudeKm_) > prevAltitudeKm_ * 0.02) {
        hasBackgroundWork = true;  // >2% altitude change → keep refining
    }
    // Count tiles in required set that are still loading (not Ready)
    int staleTiles = 0;
    for (const TileKey& rkey : selection.required) {
        auto rit = tiles_.find(rkey);
        if (rit != tiles_.end() && rit->second.IsLoading()) {
            ++staleTiles;
        }
        if (rit == tiles_.end()) {
            hasBackgroundWork = true;  // Required tile not yet created
        }
    }
    staleTileCount_ = staleTiles;
    if (staleTiles > 0) {
        hasBackgroundWork = true;
    }
    // Stall frame counter for telemetry
    if (currentLeafCount == prevLeafCount_ && currentLeafCount < 20 &&
        std::abs(altitudeKm - prevAltitudeKm_) > prevAltitudeKm_ * 0.05) {
        ++stallFrameCounter_;
    } else if (currentLeafCount != prevLeafCount_) {
        stallFrameCounter_ = 0;
    }
    prevLeafCount_ = currentLeafCount;
    prevAltitudeKm_ = altitudeKm;

    if (hasBackgroundWork) {
        frameRequested_ = true;
    }

    frameTimings_.totalMs = (glfwGetTime() * 1000.0) - updateStartMs;
}

void GlobeEngine::Render() {
    double renderStartMs = glfwGetTime() * 1000.0;
    if (config_.reversedZEnabled) {
        glClearDepth(0.0);
        glDepthFunc(GL_GEQUAL);
    } else {
        glClearDepth(1.0);
        glDepthFunc(GL_LEQUAL);
    }
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (!sceneSnapshot_.valid) {
        return;
    }

    // RenderScene: consume immutable snapshot produced during Update.
    const glm::mat4& mvp = sceneSnapshot_.mvp;
    HeightmapManager* hmForRender = sceneSnapshot_.useHeightmap ? heightmapManager_.get() : nullptr;
    auto drawStats = renderFrame_->DrawTiles(
        sceneSnapshot_.leafSet, tiles_, mvp, sceneSnapshot_.cameraPos,
        sceneSnapshot_.currentTime, cameraSpeedKmPerSec_,
        sceneSnapshot_.useLogDepth, sceneSnapshot_.logDepthFarKm,
        sceneSnapshot_.wireframe, sceneSnapshot_.loadingTexture,
        hmForRender
    );
    const auto& renderStats = tileRenderer_->GetStats();

    // Seam/continuity telemetry (P5 parity gates).
    int seamEdgeCount = 0;          // Legacy threshold-count metric.
    double seamEdgeDeltaSumM = 0.0; // Legacy average seam delta.
    int seamEdgeSamples = 0;        // Legacy sample count.
    int demFlatLeaves = 0;
    int demPendingLeaves = 0;
    int tilesUsingAncestorDem = 0;
    double seamGapP95M = 0.0;
    double seamGapMaxM = 0.0;
    int cliffEdgeCount = 0;
    double ancestorDemRatio = 0.0;
    if (demManager_) {
        int visibleDemTiles = 0;
        for (const TileKey& key : sceneSnapshot_.leafSet) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) {
                continue;
            }
            const Tile& tile = it->second;
            ++visibleDemTiles;
            if (!tile.demUsed) {
                ++demFlatLeaves;
            }
            if (tile.demPending) {
                ++demPendingLeaves;
            }
            if (tile.demEffectiveLevel < key.level || tile.demSourceLevelMax < key.level) {
                ++tilesUsingAncestorDem;
            }
        }
        if (visibleDemTiles > 0) {
            ancestorDemRatio = static_cast<double>(tilesUsingAncestorDem) / static_cast<double>(visibleDemTiles);
        }

        std::vector<double> seamDeltas;
        seamDeltas.reserve(sceneSnapshot_.leafSet.size() * 8);

        constexpr double kSeamWarnM = 4.0;
        constexpr double kCliffM = 15.0;

        auto edgeIndexFromBit = [](uint8_t bit) -> int {
            if (bit == Tile::EDGE_NORTH) return 0;
            if (bit == Tile::EDGE_EAST) return 1;
            if (bit == Tile::EDGE_SOUTH) return 2;
            if (bit == Tile::EDGE_WEST) return 3;
            return -1;
        };

        auto oppositeEdgeBit = [](uint8_t bit) -> uint8_t {
            if (bit == Tile::EDGE_NORTH) return Tile::EDGE_SOUTH;
            if (bit == Tile::EDGE_EAST) return Tile::EDGE_WEST;
            if (bit == Tile::EDGE_SOUTH) return Tile::EDGE_NORTH;
            if (bit == Tile::EDGE_WEST) return Tile::EDGE_EAST;
            return 0;
        };

        auto hasBorderHeights = [](const Tile& tile) -> bool {
            int seg = static_cast<int>(tile.borderSegments);
            if (seg <= 0) return false;
            std::size_t expected = static_cast<std::size_t>(4 * (seg + 1));
            return tile.borderHeightsKm.size() == expected;
        };

        auto sampleEdgeKm = [&](const Tile& tile, int edgeIndex, double t) -> double {
            int seg = static_cast<int>(tile.borderSegments);
            if (seg <= 0) return 0.0;
            int samples = seg + 1;
            std::size_t base = static_cast<std::size_t>(edgeIndex) * static_cast<std::size_t>(samples);
            double u = std::clamp(t, 0.0, 1.0) * static_cast<double>(seg);
            int i0 = static_cast<int>(std::floor(u));
            int i1 = std::min(seg, i0 + 1);
            double f = u - static_cast<double>(i0);
            double h0 = static_cast<double>(tile.borderHeightsKm[base + static_cast<std::size_t>(i0)]);
            double h1 = static_cast<double>(tile.borderHeightsKm[base + static_cast<std::size_t>(i1)]);
            return h0 + (h1 - h0) * f;
        };

        auto computeEdgeMaxDeltaM = [&](const TileKey& keyA,
                                        const Tile& tileA,
                                        uint8_t edgeBitA,
                                        const TileKey& keyB,
                                        const Tile& tileB,
                                        double& outMaxDeltaM) -> bool {
            if (!hasBorderHeights(tileA) || !hasBorderHeights(tileB)) {
                return false;
            }

            uint8_t edgeBitB = oppositeEdgeBit(edgeBitA);
            if (edgeBitB == 0) return false;

            // Always sample along the finer edge (higher level).
            const TileKey* fineKey = &keyA;
            const Tile* fineTile = &tileA;
            uint8_t fineEdgeBit = edgeBitA;
            const TileKey* coarseKey = &keyB;
            const Tile* coarseTile = &tileB;
            uint8_t coarseEdgeBit = edgeBitB;
            if (keyA.level < keyB.level) {
                fineKey = &keyB;
                fineTile = &tileB;
                fineEdgeBit = edgeBitB;
                coarseKey = &keyA;
                coarseTile = &tileA;
                coarseEdgeBit = edgeBitA;
            }

            int delta = fineKey->level - coarseKey->level;
            if (delta < 0 || delta > 1) return false;

            int fineSeg = static_cast<int>(fineTile->borderSegments);
            int coarseSeg = static_cast<int>(coarseTile->borderSegments);
            if (fineSeg <= 0 || coarseSeg <= 0) return false;

            int fineEdgeIndex = edgeIndexFromBit(fineEdgeBit);
            int coarseEdgeIndex = edgeIndexFromBit(coarseEdgeBit);
            if (fineEdgeIndex < 0 || coarseEdgeIndex < 0) return false;

            double scale = 1.0;
            double offset = 0.0;
            if (delta == 1) {
                scale = 0.5;
                // For N/S edges, the split is along X; for E/W edges, split is along Y.
                const bool verticalBoundary = (coarseEdgeBit == Tile::EDGE_EAST) || (coarseEdgeBit == Tile::EDGE_WEST);
                int rel = verticalBoundary
                    ? (fineKey->y - (coarseKey->y * 2))
                    : (fineKey->x - (coarseKey->x * 2));
                if (rel != 0 && rel != 1) {
                    return false;
                }
                offset = 0.5 * static_cast<double>(rel);
            }

            outMaxDeltaM = 0.0;
            for (int i = 0; i <= fineSeg; ++i) {
                double tFine = (fineSeg == 0) ? 0.0 : static_cast<double>(i) / static_cast<double>(fineSeg);
                double tCoarse = std::clamp(tFine * scale + offset, 0.0, 1.0);
                double hFineKm = sampleEdgeKm(*fineTile, fineEdgeIndex, tFine);
                double hCoarseKm = sampleEdgeKm(*coarseTile, coarseEdgeIndex, tCoarse);
                double deltaM = std::abs(hFineKm - hCoarseKm) * 1000.0;
                outMaxDeltaM = std::max(outMaxDeltaM, deltaM);
            }
            return true;
        };

        // Reset per-tile seam metrics before scan.
        for (const TileKey& key : sceneSnapshot_.leafSet) {
            auto resetIt = tiles_.find(key);
            if (resetIt != tiles_.end()) {
                resetIt->second.edgeGapMaxM = 0.0f;
                resetIt->second.edgeGapM = glm::vec4(0.0f);
                resetIt->second.seamGapMask = 0;
            }
        }

        // Compare leaf edges against adjacent leaf coverage (same-LOD or delta-LOD=1).
        static const int dx[] = {0, 1, 0, -1};  // N, E, S, W
        static const int dy[] = {-1, 0, 1, 0};
        static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST,
                                           Tile::EDGE_SOUTH, Tile::EDGE_WEST};

        for (const TileKey& key : sceneSnapshot_.leafSet) {
            auto tileIt = tiles_.find(key);
            if (tileIt == tiles_.end()) continue;
            Tile& tileA = tileIt->second;

            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) continue;

                std::vector<TileKey> neighborLeaves;
                neighborLeaves.reserve(2);

                if (sceneSnapshot_.leafSet.count(neighborSame) > 0) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 && sceneSnapshot_.leafSet.count(neighborSame.Parent()) > 0) {
                    // Neighbor region is covered by a coarser leaf (delta-LOD edge).
                    neighborLeaves.push_back(neighborSame.Parent());
                } else {
                    // Neighbor region may be refined (delta-LOD from the coarse side).
                    auto children = neighborSame.Children();
                    int a = -1, b = -1;
                    // Children order: 0=NW,1=NE,2=SW,3=SE.
                    if (edgeBits[dir] == Tile::EDGE_NORTH) { a = 2; b = 3; }
                    else if (edgeBits[dir] == Tile::EDGE_EAST) { a = 0; b = 2; }
                    else if (edgeBits[dir] == Tile::EDGE_SOUTH) { a = 0; b = 1; }
                    else if (edgeBits[dir] == Tile::EDGE_WEST) { a = 1; b = 3; }
                    if (a >= 0) {
                        if (sceneSnapshot_.leafSet.count(children[static_cast<std::size_t>(a)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (sceneSnapshot_.leafSet.count(children[static_cast<std::size_t>(b)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(b)]);
                        }
                    }
                }

                if (neighborLeaves.empty()) continue;

                for (const TileKey& neighborLeaf : neighborLeaves) {
                    if (neighborLeaf == key) continue;
                    // Process each shared boundary once (stable ordering avoids double-counting).
                    if (!(key < neighborLeaf)) continue;

                    auto neighborIt = tiles_.find(neighborLeaf);
                    if (neighborIt == tiles_.end()) continue;
                    Tile& tileB = neighborIt->second;

                    double edgeMaxDeltaM = 0.0;
                    if (!computeEdgeMaxDeltaM(key, tileA, edgeBits[dir], neighborLeaf, tileB, edgeMaxDeltaM)) {
                        continue;
                    }

                    seamDeltas.push_back(edgeMaxDeltaM);
                    seamEdgeDeltaSumM += edgeMaxDeltaM;
                    ++seamEdgeSamples;

                    if (edgeMaxDeltaM > kSeamWarnM) {
                        ++seamEdgeCount;
                        tileA.seamGapMask |= edgeBits[dir];
                        tileB.seamGapMask |= oppositeEdgeBit(edgeBits[dir]);
                    }
                    if (edgeMaxDeltaM > kCliffM) {
                        ++cliffEdgeCount;
                    }

                    // Per-edge max seam gap (telemetry + future per-edge policies).
                    {
                        int edgeIndexA = edgeIndexFromBit(edgeBits[dir]);
                        int edgeIndexB = edgeIndexFromBit(oppositeEdgeBit(edgeBits[dir]));
                        if (edgeIndexA >= 0) {
                            tileA.edgeGapM[edgeIndexA] = std::max(tileA.edgeGapM[edgeIndexA],
                                                                 static_cast<float>(edgeMaxDeltaM));
                        }
                        if (edgeIndexB >= 0) {
                            tileB.edgeGapM[edgeIndexB] = std::max(tileB.edgeGapM[edgeIndexB],
                                                                 static_cast<float>(edgeMaxDeltaM));
                        }
                    }

                    tileA.edgeGapMaxM = std::max(tileA.edgeGapMaxM, static_cast<float>(edgeMaxDeltaM));
                    tileB.edgeGapMaxM = std::max(tileB.edgeGapMaxM, static_cast<float>(edgeMaxDeltaM));
                }
            }
        }

        if (!seamDeltas.empty()) {
            std::sort(seamDeltas.begin(), seamDeltas.end());
            seamGapMaxM = seamDeltas.back();
            std::size_t p95Index = static_cast<std::size_t>(
                std::floor(0.95 * static_cast<double>(seamDeltas.size() - 1)));
            seamGapP95M = seamDeltas[p95Index];
        }
    }
    
    // Render pivot gizmo (Google Earth style target icon)
    RenderPivot(mvp);
    
    // Update debug stats
    debugStats_.fps = fps_;
    debugStats_.frameAvgMs = frameTimeTracker_.GetAvg();
    debugStats_.frameP95Ms = frameTimeTracker_.GetP95();
    debugStats_.frameP99Ms = frameTimeTracker_.GetP99();
    debugStats_.updateMs = frameTimings_.totalMs;
    debugStats_.renderMs = frameTimings_.renderMs;
    debugStats_.lodSelectMs = frameTimings_.lodSelectMs;
    debugStats_.requestLoopMs = frameTimings_.requestLoopMs;
    debugStats_.schedulerUpdateMs = frameTimings_.schedulerUpdateMs;
    debugStats_.textureUploadMs = frameTimings_.textureUploadMs;
    debugStats_.meshBuildMs = frameTimings_.meshBuildMs;
    debugStats_.tileCount = static_cast<int>(tiles_.size());
    auto schedulerStats = scheduler_->GetStats();
    debugStats_.pendingFetches = schedulerStats.pendingFetches;
    debugStats_.pendingDecodes = schedulerStats.pendingDecodes;
    debugStats_.activeFetches = schedulerStats.activeFetches;
    debugStats_.fetchQueueSize = schedulerStats.fetchResultQueue;
    debugStats_.decodeQueueSize = schedulerStats.decodeResultQueue;
    debugStats_.avgFetchMs = schedulerStats.avgFetchMs;
    debugStats_.avgDecodeMs = schedulerStats.avgDecodeMs;
    debugStats_.droppedFetchResults = schedulerStats.droppedFetchResults;
    debugStats_.droppedDecodeResults = schedulerStats.droppedDecodeResults;
    debugStats_.decodedCacheReadHits = schedulerStats.decodedCacheReadHits;
    debugStats_.decodedCacheReadMisses = schedulerStats.decodedCacheReadMisses;
    debugStats_.decodedCacheWrites = schedulerStats.decodedCacheWrites;
    debugStats_.decodedCacheWriteRejects = schedulerStats.decodedCacheWriteRejects;
    debugStats_.decodedCacheEvictions = schedulerStats.decodedCacheEvictions;
    debugStats_.decodedCacheEntries = schedulerStats.decodedCacheEntries;
    debugStats_.decodedCacheBytesUsed = schedulerStats.decodedCacheBytesUsed;
    debugStats_.decodeBypassHits = schedulerStats.decodeBypassHits;
    debugStats_.memoryCacheReadHits = schedulerStats.memoryCacheReadHits;
    debugStats_.memoryCacheReadMisses = schedulerStats.memoryCacheReadMisses;
    debugStats_.memoryCacheWrites = schedulerStats.memoryCacheWrites;
    debugStats_.memoryCacheWriteRejects = schedulerStats.memoryCacheWriteRejects;
    debugStats_.memoryCacheEvictions = schedulerStats.memoryCacheEvictions;
    debugStats_.memoryCacheEntries = schedulerStats.memoryCacheEntries;
    debugStats_.memoryCacheBytesUsed = schedulerStats.memoryCacheBytesUsed;
    debugStats_.diskCacheReadHits = schedulerStats.diskCacheReadHits;
    debugStats_.diskCacheReadMisses = schedulerStats.diskCacheReadMisses;
    debugStats_.diskCacheWrites = schedulerStats.diskCacheWrites;
    debugStats_.diskCacheWriteFails = schedulerStats.diskCacheWriteFails;
    debugStats_.networkFetches = schedulerStats.networkFetches;
    debugStats_.totalFetchRequests = schedulerStats.totalFetchRequests;
    debugStats_.renderableLeaves = drawStats.renderableLeaves;
    debugStats_.crossfadingLeaves = drawStats.crossfadingLeaves;
    debugStats_.fallbackTiles = drawStats.fallbackTiles;
    debugStats_.placeholderTiles = drawStats.placeholderTiles;
    debugStats_.leafNoMesh = drawStats.leafNoMesh;
    debugStats_.leafNoTexture = drawStats.leafNoTexture;
    debugStats_.missingTiles = drawStats.missing;
    debugStats_.visibleTiles = drawStats.renderableLeaves + drawStats.fallbackTiles;
    debugStats_.drawCalls = renderStats.drawCalls;
    debugStats_.trianglesRendered = renderStats.trianglesRendered;
    debugStats_.instancedBatches = renderStats.instancedBatches;
    debugStats_.instancedTiles = renderStats.instancedTiles;
    debugStats_.atlasEnabled = textureManager_ && textureManager_->IsAtlasEnabled();
    debugStats_.atlasPages = textureManager_ ? textureManager_->GetAtlasPageCount() : 0;
    debugStats_.atlasUsedSlots = textureManager_ ? textureManager_->GetAtlasUsedSlots() : 0;
    debugStats_.atlasCapacitySlots = textureManager_ ? textureManager_->GetAtlasCapacitySlots() : 0;
    debugStats_.cameraSpeedKmPerSec = cameraSpeedKmPerSec_;
    debugStats_.demWaitMs = (demWaitSamples_ > 0)
        ? (demWaitAccumMs_ / static_cast<double>(demWaitSamples_))
        : 0.0;
    debugStats_.meshRebuildCount = demTriggeredMeshRebuilds_;
    debugStats_.leafUnderflowFrames = leafUnderflowFrames_;
    debugStats_.seamEdgeCount = seamEdgeCount;
    debugStats_.avgEdgeHeightDeltaM = seamEdgeSamples > 0
        ? seamEdgeDeltaSumM / static_cast<double>(seamEdgeSamples)
        : 0.0;
    debugStats_.demFlatLeaves = demFlatLeaves;
    debugStats_.demPendingLeaves = demPendingLeaves;
    debugStats_.tilesUsingAncestorDem = tilesUsingAncestorDem;
    debugStats_.seamGapP95M = seamGapP95M;
    debugStats_.seamGapMaxM = seamGapMaxM;
    debugStats_.cliffEdgeCount = cliffEdgeCount;
    debugStats_.ancestorDemRatio = ancestorDemRatio;
    // Request-stall diagnostics
    {
        int maxLvl = 0;
        for (const TileKey& lk : sceneSnapshot_.leafSet) {
            maxLvl = std::max(maxLvl, lk.level);
        }
        debugStats_.maxLeafLevel = maxLvl;
    }
    debugStats_.sseEffectiveThreshold = tilePyramid_.GetSettings().sseThreshold /
        std::max(0.25f, tilePyramid_.GetSettings().tiltFactor);
    debugStats_.tiltFactor = tilePyramid_.GetSettings().tiltFactor;
    debugStats_.staleTileCount = staleTileCount_;
    debugStats_.stallFrames = stallFrameCounter_;
    debugStats_.leafCount = tilePyramid_.GetLeafCount();
    debugStats_.requiredCount = tilePyramid_.GetRequiredCount();
    debugStats_.currentZoom = GetCurrentZoom();
    camera_->GetLatLonAlt(debugStats_.latitude, debugStats_.longitude, debugStats_.altitude);
    debugStats_.heading = camera_->GetHeading();
    debugStats_.tilt = camera_->GetTilt();
    
    // Render ImGui debug panel
    RenderDebugPanel();

    frameTimings_.renderMs = (glfwGetTime() * 1000.0) - renderStartMs;
    frameTimings_.totalMs = frameTimings_.totalMs + frameTimings_.renderMs;
    frameTimeTracker_.Record(frameTimings_.totalMs);
}

void GlobeEngine::BuildTileMesh(Tile& tile) {
    // Delegate to TileMeshBuilder (GE-style separation)
    // Feed measured seam gap edges back into the DEM edge-equalization mask.
    // This reduces residual "tile grid" cracks by sampling one level coarser on
    // problematic borders (GE-style edge continuity).
    auto result = TileMeshBuilder::Build(tile.key, tile.extent,
                                         static_cast<uint8_t>(tile.edgeCoarserMask | tile.seamGapMask),
                                         tile.stitchMask,
                                         tile.skirtMask,
                                         static_cast<int>(tile.demTargetLevel),
                                         tile.demEdgeLevelPack,
                                         demManager_.get(), config_, true);
    result.meshRevision = tile.meshRevision;
    TileMeshBuilder::UploadToGPU(tile, result);
    tile.meshBuiltRevision = tile.meshRevision;
    tile.prevEdgeCoarserMask = tile.edgeCoarserMask;
}

void GlobeEngine::RenderTile(const Tile& tile, const glm::mat4& mvp) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tile.textureId);
    
    glBindVertexArray(tile.vao);
    glDrawElements(GL_TRIANGLES, tile.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Ray-sphere intersection helper (returns t parameter, negative if no hit)
static double RaySphereIntersect(const glm::dvec3& origin, const glm::dvec3& dir, double radius) {
    double a = glm::dot(dir, dir);
    double b = 2.0 * glm::dot(origin, dir);
    double c = glm::dot(origin, origin) - radius * radius;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return -1.0;
    double sqrtD = std::sqrt(disc);
    double t = (-b - sqrtD) / (2.0 * a);
    if (t < 0.0) t = (-b + sqrtD) / (2.0 * a);
    return t;
}

// Globe picking for navigation - terrain-aware with DEM refinement
bool GlobeEngine::PickGlobe(double screenX, double screenY, glm::dvec3& outPoint) {
    glm::dvec3 rayOrigin, rayDir;
    camera_->GetRay(screenX, screenY, config_.windowWidth, config_.windowHeight, rayOrigin, rayDir);
    
    const double R = earth::EARTH_RADIUS_KM;
    
    // Step 1: Initial ray-sphere intersection (base radius)
    double t = RaySphereIntersect(rayOrigin, rayDir, R);
    if (t < 0.0) return false;
    
    outPoint = rayOrigin + t * rayDir;
    
    // Step 2: Terrain refinement (if DEM available)
    if (demManager_ && config_.demEnabled) {
        // Convert hit point to lat/lon
        glm::dvec3 hitNorm = glm::normalize(outPoint);
        double lat = glm::degrees(std::asin(std::clamp(hitNorm.z, -1.0, 1.0)));
        double lon = glm::degrees(std::atan2(hitNorm.y, hitNorm.x));
        
        // Get camera altitude to determine DEM sample level
        double camLat, camLon, camAlt;
        camera_->GetLatLonAlt(camLat, camLon, camAlt);
        int sampleLevel = std::clamp(static_cast<int>(std::log2(40000000.0 / std::max(1.0, camAlt))), 1, 12);
        
        auto sampleWithParentFallback = [&](double sLon, double sLat, int startLevel, double& outHeightMeters) {
            for (int level = startLevel; level >= 0; --level) {
                if (demManager_->SampleHeight(sLon, sLat, level, outHeightMeters)) {
                    return true;
                }
            }
            return false;
        };

        // Iterative refinement (2 passes for convergence)
        for (int iter = 0; iter < 2; ++iter) {
            double heightMeters = 0.0;
            if (sampleWithParentFallback(lon, lat, sampleLevel, heightMeters)) {
                double heightKm = heightMeters * 0.001 * config_.demHeightScale;
                double terrainR = R + heightKm;
                
                // Re-intersect with terrain-adjusted sphere
                double tTerrain = RaySphereIntersect(rayOrigin, rayDir, terrainR);
                if (tTerrain > 0.0) {
                    outPoint = rayOrigin + tTerrain * rayDir;
                    
                    // Update lat/lon for next iteration
                    hitNorm = glm::normalize(outPoint);
                    lat = glm::degrees(std::asin(std::clamp(hitNorm.z, -1.0, 1.0)));
                    lon = glm::degrees(std::atan2(hitNorm.y, hitNorm.x));
                }
            }
        }
    }
    
    return true;
}

void GlobeEngine::FlyTo(double lat, double lon, double altMeters, 
                        double heading, double tilt, double duration) {
    flightController_->FlyToLocation(lat, lon, altMeters, heading, tilt, duration);
    frameRequested_ = true;
}

void GlobeEngine::LookAt(double lat, double lon, double altitude) {
    camera_->SetLatLonAlt(lat, lon, altitude);
    frameRequested_ = true;
}

void GlobeEngine::GetCameraLatLonAlt(double& lat, double& lon, double& alt) const {
    camera_->GetLatLonAlt(lat, lon, alt);
}

int GlobeEngine::GetCurrentZoom() const {
    double lat, lon, alt;
    camera_->GetLatLonAlt(lat, lon, alt);
    // Google Earth style zoom calculation
    int zoom = static_cast<int>(std::log2(40000000.0 / std::max(1.0, alt)));
    return std::clamp(zoom, config_.minZoom, config_.maxZoom);
}

// Input callbacks - delegate to FlightController (Google Earth parity)
void GlobeEngine::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
    engine->frameRequested_ = true;
    
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
        return;
    }
    
    // Screenshot (S key)
    if (key == GLFW_KEY_S && action == GLFW_PRESS) {
        static int screenshotNum = 0;
        std::string filename = "screenshot_" + std::to_string(screenshotNum++) + ".ppm";
        engine->SaveScreenshot(filename);
        return;
    }
    
    // Visual LOD Test (T key)
    if (key == GLFW_KEY_T && action == GLFW_PRESS) {
        engine->RunVisualLodTest();
        return;
    }
    
    // Pass to flight controller
    if (action == GLFW_PRESS) {
        engine->flightController_->OnKeyDown(key);
    } else if (action == GLFW_RELEASE) {
        engine->flightController_->OnKeyUp(key);
    }
    
    // Modifier keys
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    engine->flightController_->OnModifiers(shift, ctrl);
}

void GlobeEngine::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
    engine->frameRequested_ = true;
    
    // Update modifiers for Shift+Scroll = Tilt
    bool shift = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                 (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
    bool ctrl = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    engine->flightController_->OnModifiers(shift, ctrl);
    
    engine->flightController_->OnScroll(xoffset, yoffset);
}

void GlobeEngine::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
    engine->frameRequested_ = true;
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    double time = glfwGetTime();
    
    // Update modifiers BEFORE mouse action (critical for Shift+Click = Orbit)
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    engine->flightController_->OnModifiers(shift, ctrl);
    
    if (action == GLFW_PRESS) {
        // Double-click detection (Google Earth style - FlyTo on double-click)
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            static double lastClickTime = 0.0;
            static double lastClickX = 0.0;
            static double lastClickY = 0.0;
            constexpr double DOUBLE_CLICK_TIME = 0.3;  // 300ms
            constexpr double DOUBLE_CLICK_DIST = 5.0;  // 5 pixels
            
            double dx = x - lastClickX;
            double dy = y - lastClickY;
            double dist = std::sqrt(dx * dx + dy * dy);
            
            if ((time - lastClickTime) < DOUBLE_CLICK_TIME && dist < DOUBLE_CLICK_DIST) {
                engine->flightController_->OnDoubleClick(x, y);
                lastClickTime = 0.0;  // Reset to prevent triple-click
            } else {
                lastClickTime = time;
                lastClickX = x;
                lastClickY = y;
                engine->flightController_->OnMouseDown(button, x, y, time);
            }
        } else {
            engine->flightController_->OnMouseDown(button, x, y, time);
        }
    } else if (action == GLFW_RELEASE) {
        engine->flightController_->OnMouseUp(button, time);
    }
}

void GlobeEngine::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
    engine->frameRequested_ = true;
    engine->flightController_->OnMouseMove(xpos, ypos, glfwGetTime());
}

void GlobeEngine::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
    engine->frameRequested_ = true;
    if (width <= 0 || height <= 0) return;
    
    // Get window size (for cursor coordinate system - may differ from framebuffer on HiDPI)
    int windowW, windowH;
    glfwGetWindowSize(window, &windowW, &windowH);
    
    // Update config with WINDOW size (matches cursor coordinates)
    engine->config_.windowWidth = windowW;
    engine->config_.windowHeight = windowH;
    
    // Update camera aspect ratio (use window size for consistent NDC mapping)
    engine->camera_->SetAspectRatio(static_cast<double>(windowW) / windowH);
    
    // Update flight controller with WINDOW size (cursor coords are in window space)
    engine->flightController_->OnWindowResize(windowW, windowH);
    
    // Update OpenGL viewport with FRAMEBUFFER size
    glViewport(0, 0, width, height);
}

// ============================================================================
// IMGUI DEBUG PANEL
// ============================================================================
void GlobeEngine::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Dark style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.Alpha = 0.95f;
    
    // Initialize backends
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void GlobeEngine::ShutdownImGui() {
    // Guard: only shutdown if context exists
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void GlobeEngine::RenderDebugPanel() {
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Toggle with F3 key
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
        showDebugPanel_ = !showDebugPanel_;
    }
    
    if (showDebugPanel_) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 380), ImGuiCond_FirstUseEver);
        
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        
        if (ImGui::Begin("Debug Panel (F3 toggle)", &showDebugPanel_, flags)) {
            // Performance
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Performance");
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", debugStats_.fps);
            ImGui::Text("Frame Time: %.2f ms", 1000.0f / std::max(debugStats_.fps, 1.0f));
            ImGui::Text("Avg/P95/P99: %.2f / %.2f / %.2f ms",
                        debugStats_.frameAvgMs, debugStats_.frameP95Ms, debugStats_.frameP99Ms);
            ImGui::Text("Update/Render: %.2f / %.2f ms", debugStats_.updateMs, debugStats_.renderMs);
            ImGui::Text("LOD: %.2f | Req: %.2f | Sch: %.2f",
                        debugStats_.lodSelectMs, debugStats_.requestLoopMs, debugStats_.schedulerUpdateMs);
            ImGui::Text("Upload: %.2f | Mesh: %.2f ms",
                        debugStats_.textureUploadMs, debugStats_.meshBuildMs);
            
            ImGui::Spacing();
            
            // Tiles
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tiles");
            ImGui::Separator();
            ImGui::Text("Total: %d", debugStats_.tileCount);
            ImGui::Text("Leaves: %d", debugStats_.leafCount);
            ImGui::Text("Required: %d", debugStats_.requiredCount);
            ImGui::Text("Visible: %d", debugStats_.visibleTiles);
            ImGui::Text("Draw Calls: %d", debugStats_.drawCalls);
            ImGui::Text("Triangles: %d", debugStats_.trianglesRendered);
            ImGui::Text("Instanced: %d batches / %d tiles",
                        debugStats_.instancedBatches, debugStats_.instancedTiles);
            if (debugStats_.atlasEnabled) {
                ImGui::Text("Atlas Slots: %d / %d (%d pages)",
                            debugStats_.atlasUsedSlots,
                            debugStats_.atlasCapacitySlots,
                            debugStats_.atlasPages);
            } else {
                ImGui::Text("Atlas: disabled");
            }
            ImGui::Text("Pending Fetch: %d", debugStats_.pendingFetches);
            ImGui::Text("Pending Decode: %d", debugStats_.pendingDecodes);
            ImGui::Text("Active Fetch: %d", debugStats_.activeFetches);
            ImGui::Text("FetchQ/DecodeQ: %zu / %zu",
                        debugStats_.fetchQueueSize, debugStats_.decodeQueueSize);
            ImGui::Text("Dropped F/D: %zu / %zu",
                        debugStats_.droppedFetchResults, debugStats_.droppedDecodeResults);
            ImGui::Text("Avg Fetch/Decode: %.2f / %.2f ms",
                        debugStats_.avgFetchMs, debugStats_.avgDecodeMs);
            ImGui::Text("Decoded Cache Hit/Miss: %zu / %zu",
                        debugStats_.decodedCacheReadHits, debugStats_.decodedCacheReadMisses);
            ImGui::Text("Decoded Entries: %zu (%.1f MB)",
                        debugStats_.decodedCacheEntries,
                        static_cast<double>(debugStats_.decodedCacheBytesUsed) / (1024.0 * 1024.0));
            ImGui::Text("Decoded Writes/Reject/Evict: %zu / %zu / %zu",
                        debugStats_.decodedCacheWrites,
                        debugStats_.decodedCacheWriteRejects,
                        debugStats_.decodedCacheEvictions);
            ImGui::Text("Decode Bypass Hits: %zu", debugStats_.decodeBypassHits);
            ImGui::Text("Memory Cache Hit/Miss: %zu / %zu",
                        debugStats_.memoryCacheReadHits, debugStats_.memoryCacheReadMisses);
            ImGui::Text("Memory Cache Entries: %zu (%.1f MB)",
                        debugStats_.memoryCacheEntries,
                        static_cast<double>(debugStats_.memoryCacheBytesUsed) / (1024.0 * 1024.0));
            ImGui::Text("Memory Writes/Reject/Evict: %zu / %zu / %zu",
                        debugStats_.memoryCacheWrites,
                        debugStats_.memoryCacheWriteRejects,
                        debugStats_.memoryCacheEvictions);
            ImGui::Text("Disk Cache Hit/Miss: %zu / %zu",
                        debugStats_.diskCacheReadHits, debugStats_.diskCacheReadMisses);
            ImGui::Text("Disk Cache Writes/Fails: %zu / %zu",
                        debugStats_.diskCacheWrites, debugStats_.diskCacheWriteFails);
            ImGui::Text("Network Fetches: %zu / %zu req",
                        debugStats_.networkFetches, debugStats_.totalFetchRequests);
            
            // Gap-free telemetry
            ImGui::Text("Renderable: %d", debugStats_.renderableLeaves);
            ImGui::Text("Crossfading: %d", debugStats_.crossfadingLeaves);
            ImGui::Text("Fallback: %d", debugStats_.fallbackTiles);
            ImGui::Text("Leaf Underflow Frames: %llu",
                        static_cast<unsigned long long>(debugStats_.leafUnderflowFrames));
            ImGui::Text("Seam Gap P95/Max: %.2f / %.2f m",
                        debugStats_.seamGapP95M, debugStats_.seamGapMaxM);
            ImGui::Text("Cliff Edge Count: %d", debugStats_.cliffEdgeCount);
            ImGui::Text("DEM Flat/Pending: %d / %d",
                        debugStats_.demFlatLeaves, debugStats_.demPendingLeaves);
            ImGui::Text("Ancestor DEM Ratio: %.1f%%",
                        debugStats_.ancestorDemRatio * 100.0);
            ImGui::Text("Legacy Seam Edges: %d", debugStats_.seamEdgeCount);
            ImGui::Text("Legacy Avg Edge Delta: %.2f m", debugStats_.avgEdgeHeightDeltaM);
            ImGui::Text("Ancestor DEM Tiles: %d", debugStats_.tilesUsingAncestorDem);
            if (debugStats_.placeholderTiles > 0) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Placeholder: %d", debugStats_.placeholderTiles);
            }
            if (debugStats_.leafNoMesh > 0 || debugStats_.leafNoTexture > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "NoMesh: %d | NoTex: %d", 
                    debugStats_.leafNoMesh, debugStats_.leafNoTexture);
            }
            if (debugStats_.missingTiles > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "MISSING: %d", debugStats_.missingTiles);
            }
            
            ImGui::Spacing();
            
            // LOD & Camera
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Camera & LOD");
            ImGui::Separator();
            ImGui::Text("Zoom Level: %d", debugStats_.currentZoom);
            ImGui::Text("Altitude: %.1f km", debugStats_.altitude / 1000.0);
            ImGui::Text("Lat: %.4f", debugStats_.latitude);
            ImGui::Text("Lon: %.4f", debugStats_.longitude);
            ImGui::Text("Heading: %.1f", debugStats_.heading);
            ImGui::Text("Tilt: %.1f", debugStats_.tilt);
            ImGui::Text("Cam Speed: %.1f km/s", debugStats_.cameraSpeedKmPerSec);
            ImGui::Text("Max Leaf Lvl: %d", debugStats_.maxLeafLevel);
            ImGui::Text("SSE Eff.Thresh: %.3f", debugStats_.sseEffectiveThreshold);
            ImGui::Text("Tilt Factor: %.3f", debugStats_.tiltFactor);
            if (debugStats_.staleTileCount > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                    "Stale Loading: %d", debugStats_.staleTileCount);
            }
            if (debugStats_.stallFrames > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                    "STALL FRAMES: %llu", static_cast<unsigned long long>(debugStats_.stallFrames));
            }
            
            ImGui::Spacing();
            
            // Render Options
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Render Options");
            ImGui::Separator();
            ImGui::Checkbox("Wireframe Mode", &config_.wireframeMode);
            ImGui::Checkbox("Log Depth Precision", &config_.logDepthEnabled);
            if (ImGui::Checkbox("Reversed-Z Precision", &config_.reversedZEnabled)) {
                if (config_.reversedZEnabled) {
                    config_.logDepthEnabled = false;
                }
            }
            if (config_.reversedZEnabled && config_.logDepthEnabled) {
                config_.logDepthEnabled = false;
            }
            ImGui::Text("Depth Near/Far: %.3f / %.0f km", currentNearPlaneKm_, currentFarPlaneKm_);

            // Culling toggles (diagnostic): helps confirm whether residual black gaps are
            // caused by visibility false-negatives (frustum/horizon) or by streaming.
            if (ImGui::Checkbox("Disable Frustum Cull", &config_.disableFrustumCull)) {
                frameRequested_ = true;
            }
            if (ImGui::Checkbox("Disable Horizon Cull", &config_.disableHorizonCull)) {
                frameRequested_ = true;
            }
            
            // Displacement mode toggle
            const char* modeNames[] = { "CPU Mesh Bake", "GPU Heightmap" };
            int currentMode = static_cast<int>(config_.terrainDisplacementMode);
            if (ImGui::Combo("Terrain Mode", &currentMode, modeNames, 2)) {
                config_.terrainDisplacementMode = static_cast<DisplacementMode>(currentMode);
            }
            if (config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE &&
                demManager_ &&
                demManager_->GetHealthStatus() != DemHealthStatus::Healthy) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                    "GPU terrain gecici devre disi (DEM sagliksiz)");
            }
            
            // DEM Telemetry
            if (demManager_) {
                ImGui::Spacing();
                const auto& demStats = demManager_->GetStats();
                auto demHealth = demManager_->GetHealthStatus();
                ImVec4 healthColor = (demHealth == DemHealthStatus::Healthy) 
                    ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(healthColor, "DEM: %s", DemHealthStatusToString(demHealth));
                ImGui::Separator();
                ImGui::Text("Fetch OK/Fail: %d / %d (%.0f%%)", 
                    demStats.fetchSuccess.load(), demStats.fetchFail.load(), demStats.GetSuccessRate());
                ImGui::Text("Avg Fetch: %.0f ms", demStats.GetAvgFetchMs());
                if (demStats.fetchTimeout.load() > 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Timeouts: %d", demStats.fetchTimeout.load());
                if (demStats.fetchAuth.load() > 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Auth Fails: %d", demStats.fetchAuth.load());
                ImGui::Text("Parse OK/Fail: %d / %d", demStats.parseSuccess.load(), demStats.parseFail.load());
                ImGui::Text("DEM Cache: %d", demManager_->GetCacheSize());
                ImGui::Text("DEM Wait Avg: %.1f ms", debugStats_.demWaitMs);
                ImGui::Text("DEM Rebuilds: %zu", debugStats_.meshRebuildCount);
            }
            
            ImGui::Spacing();
            
            // Debug culling toggles
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Culling Debug");
            ImGui::Checkbox("Disable Frustum Cull", &config_.disableFrustumCull);
            ImGui::Checkbox("Disable Horizon Cull", &config_.disableHorizonCull);
            
            ImGui::Spacing();
            
            // Controls help
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Controls");
            ImGui::Separator();
            ImGui::TextWrapped("Left: Pan | Shift+Left: Orbit");
            ImGui::TextWrapped("Scroll: Zoom | Shift+Scroll: Tilt");
            ImGui::TextWrapped("Double-click: FlyTo + Zoom");
            ImGui::TextWrapped("F4: Network Panel");
        }
        ImGui::End();
    }
    
    // Toggle Network Panel with F4
    if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
        NetworkPanel::Instance().panelOpen = !NetworkPanel::Instance().panelOpen;
    }
    
    // Render Network Panel
    NetworkPanel::Instance().Render();
    
    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ============================================================================
// PIVOT GIZMO (Google Earth style target icon)
// ============================================================================
void GlobeEngine::InitPivotGizmo() {
    // Simple pivot shader
    const char* vertSrc = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uMVP;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
        }
    )";
    
    const char* fragSrc = R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )";
    
    // Compile shaders
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertSrc, nullptr);
    glCompileShader(vs);
    
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrc, nullptr);
    glCompileShader(fs);
    
    pivotProgram_ = glCreateProgram();
    glAttachShader(pivotProgram_, vs);
    glAttachShader(pivotProgram_, fs);
    glLinkProgram(pivotProgram_);
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    pivotMvpLoc_ = glGetUniformLocation(pivotProgram_, "uMVP");
    pivotColorLoc_ = glGetUniformLocation(pivotProgram_, "uColor");
    
    // Create pivot geometry (circle + crosshair)
    std::vector<float> vertices;
    const int segments = 32;
    
    // Circle (as lines)
    for (int i = 0; i < segments; ++i) {
        float angle1 = static_cast<float>(i) * 2.0f * 3.14159f / segments;
        float angle2 = static_cast<float>(i + 1) * 2.0f * 3.14159f / segments;
        vertices.push_back(std::cos(angle1)); vertices.push_back(std::sin(angle1)); vertices.push_back(0.0f);
        vertices.push_back(std::cos(angle2)); vertices.push_back(std::sin(angle2)); vertices.push_back(0.0f);
    }
    
    // Crosshair
    vertices.push_back(-1.3f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(1.3f);  vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(-1.3f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(1.3f);  vertices.push_back(0.0f);
    
    pivotVertexCount_ = static_cast<int>(vertices.size() / 3);
    
    glGenVertexArrays(1, &pivotVao_);
    glGenBuffers(1, &pivotVbo_);
    
    glBindVertexArray(pivotVao_);
    glBindBuffer(GL_ARRAY_BUFFER, pivotVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    
    glBindVertexArray(0);
}

void GlobeEngine::RenderPivot(const glm::mat4& viewProj) {
    glm::dvec3 pivot;
    if (!flightController_->GetPivot(pivot)) return;
    
    if (pivotProgram_ && pivotVao_) {
        glUseProgram(pivotProgram_);
        
        // Calculate orientation to lie on the surface
        glm::dvec3 up = glm::normalize(pivot);
        glm::dvec3 camPos = camera_->GetPositionECEF();
        glm::dvec3 toCam = glm::normalize(camPos - pivot);
        
        // Right vector
        glm::dvec3 right = glm::cross(up, toCam);
        if (glm::length(right) < 0.001) right = glm::dvec3(1, 0, 0);
        right = glm::normalize(right);
        
        // Forward vector (surface tangent)
        glm::dvec3 forward = glm::cross(right, up);
        
        // Scale based on distance and FOV to keep screen size constant (~40 pixels)
        double dist = glm::length(camPos - pivot);
        float fovRad = glm::radians(static_cast<float>(camera_->GetFov()));
        float targetPixelSize = 40.0f;
        float scale = static_cast<float>(targetPixelSize * (dist * std::tan(fovRad * 0.5f) / config_.windowHeight));
        
        // Construct Model Matrix
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(glm::vec3(right), 0.0f);
        model[1] = glm::vec4(glm::vec3(forward), 0.0f);
        model[2] = glm::vec4(glm::vec3(up), 0.0f);
        model[3] = glm::vec4(glm::vec3(pivot), 1.0f);
        
        // Scale
        model = glm::scale(model, glm::vec3(scale));
        
        // Raise slightly to avoid Z-fighting
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.1f));
        
        glm::mat4 mvp = viewProj * model;
        
        glUniformMatrix4fv(pivotMvpLoc_, 1, GL_FALSE, glm::value_ptr(mvp));
        
        // Google Earth style color (Yellow/Orange)
        glUniform4f(pivotColorLoc_, 1.0f, 0.7f, 0.0f, 0.9f);
        
        // Disable depth test for always-visible gizmo
        glDisable(GL_DEPTH_TEST);
        
        glBindVertexArray(pivotVao_);
        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, 0, pivotVertexCount_);
        glLineWidth(1.0f);
        glBindVertexArray(0);
        
        glEnable(GL_DEPTH_TEST);
    }
}

// =============================================================================
// MESH BUILD PIPELINE (Async CPU build + budgeted GPU upload)
// =============================================================================

void GlobeEngine::QueueMeshBuild(const TileKey& key, bool isVisible) {
    if (!meshScheduler_) return;
    if (rebuildPending_.count(key)) return;
    
    auto it = tiles_.find(key);
    if (it == tiles_.end()) return;
    
    Tile& tile = it->second;
    if (tile.meshPending) return;

    // P5.3: DEM-aware mesh build coordination.
    // If DEM fetch is already pending, wait up to 500ms before building a flat mesh.
    if (isVisible &&
        demManager_ &&
        config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE &&
        demManager_->GetHealthStatus() == DemHealthStatus::Healthy) {
        constexpr double kDemMeshWaitTimeoutSec = 0.5;
        // Wait for the DEM level that this tile is actually targeting (which can be an
        // ancestor level for coherence). Waiting only on the exact child key causes
        // meshes to be baked "flat" even when a coarser DEM tile is already in flight,
        // producing the "each tile lifts independently" cliff/wall artifact.
        TileKey demTargetKey = key;
        int targetLevel = std::clamp(static_cast<int>(tile.demTargetLevel), 0, key.level);
        while (demTargetKey.level > targetLevel) {
            demTargetKey = demTargetKey.Parent();
        }

        const bool hasDemData = demManager_->HasData(demTargetKey);
        const bool hasAncestorFallback = demManager_->HasDataOrAncestor(demTargetKey);
        const bool demPendingRequest = demManager_->HasPendingRequest(demTargetKey);
        const double nowSec = glfwGetTime();

        if (!hasDemData && !hasAncestorFallback && demPendingRequest) {
            auto [waitIt, inserted] = demMeshWaitStartSec_.emplace(key, nowSec);
            double waitedSec = std::max(0.0, nowSec - waitIt->second);
            if (waitedSec < kDemMeshWaitTimeoutSec) {
                return;  // Defer mesh build while DEM is likely to arrive soon.
            }
            demWaitAccumMs_ += waitedSec * 1000.0;
            ++demWaitSamples_;
            demMeshWaitStartSec_.erase(waitIt);
        } else {
            auto waitIt = demMeshWaitStartSec_.find(key);
            if (waitIt != demMeshWaitStartSec_.end()) {
                double waitedSec = std::max(0.0, nowSec - waitIt->second);
                demWaitAccumMs_ += waitedSec * 1000.0;
                ++demWaitSamples_;
                demMeshWaitStartSec_.erase(waitIt);
            }
        }
    }
    
    TileMeshScheduler::MeshRequest request;
    request.key = key;
    request.extent = tile.extent;
    // Feed measured seam gap edges back into the DEM edge-equalization mask.
    request.edgeMask = static_cast<uint8_t>(tile.edgeCoarserMask | tile.seamGapMask);
    request.stitchMask = tile.stitchMask;
    request.skirtMask = tile.skirtMask;
    request.demTargetLevel = static_cast<int>(tile.demTargetLevel);
    request.demEdgeLevelPack = tile.demEdgeLevelPack;
    request.meshRevision = tile.meshRevision;
    request.priority = isVisible ? Priority::Urgent : Priority::Normal;
    request.score = tile.importance;
    
    rebuildPending_.insert(key);
    tile.meshPending = true;
    meshScheduler_->Request(std::move(request));
}

void GlobeEngine::ProcessMeshResults() {
    if (!meshScheduler_) return;
    
    int processed = 0;
    double startMs = glfwGetTime() * 1000.0;
    while (processed < MAX_MESH_REBUILDS_PER_FRAME) {
        double elapsed = glfwGetTime() * 1000.0 - startMs;
        if (elapsed >= config_.meshUploadBudgetMs && processed > 0) {
            break;
        }

        TileMeshBuilder::BuildResult result;
        if (!meshScheduler_->TryGetResult(result)) {
            break;
        }
        
        auto it = tiles_.find(result.key);
        if (it == tiles_.end()) {
            rebuildPending_.erase(result.key);
            continue;
        }
        
        Tile& tile = it->second;
        tile.meshPending = false;
        rebuildPending_.erase(result.key);
        
        if (result.meshRevision != tile.meshRevision) {
            continue;  // Stale result
        }
        
        TileMeshBuilder::UploadToGPU(tile, result);
        tile.meshBuiltRevision = tile.meshRevision;
        tile.prevEdgeCoarserMask = tile.edgeCoarserMask;
        ++processed;
    }
}

// =============================================================================
// BASE TILE PRELOAD (LOD0-1 bootstrap coverage)
// =============================================================================

void GlobeEngine::PreloadBaseTiles() {
    // LOD 0: 1 tile, LOD 1: 4 tiles = 5 tiles total
    constexpr int MAX_PRELOAD_LEVEL = 1;
    uint32_t loadingTexture = textureManager_->GetLoadingTexture();
    const double nowSec = glfwGetTime();
    
    for (int level = 0; level <= MAX_PRELOAD_LEVEL; ++level) {
        int tilesPerSide = 1 << level;
        for (int y = 0; y < tilesPerSide; ++y) {
            for (int x = 0; x < tilesPerSide; ++x) {
                TileKey key(level, x, y);
                baseTileKeys_.insert(key);
                
                // Create tile if not exists
                auto it = tiles_.find(key);
                if (it == tiles_.end()) {
                    tiles_.emplace(key, Tile(key));
                    it = tiles_.find(key);
                }
                
                Tile& tile = it->second;
                tile.demTargetLevel = static_cast<uint8_t>(std::clamp(key.level, 0, 255));
                tile.demEffectiveLevel = tile.demTargetLevel;
                {
                    const uint32_t lvl = static_cast<uint32_t>(tile.demTargetLevel);
                    tile.demEdgeLevelPack = lvl | (lvl << 8) | (lvl << 16) | (lvl << 24);
                }
                tile.stitchMask = 0;
                tile.skirtMask = config_.selectiveSkirts
                    ? static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_EAST | Tile::EDGE_SOUTH | Tile::EDGE_WEST)
                    : static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_EAST | Tile::EDGE_SOUTH | Tile::EDGE_WEST);
                
                // Ensure mesh exists for immediate fallback coverage
                if (!tile.hasMesh) {
                    BuildTileMesh(tile);
                }
                
                // Use loading texture as temporary placeholder until real texture arrives
                if (tile.textureId == 0 && loadingTexture != 0) {
                    tile.textureId = loadingTexture;
                    tile.ownsTexture = false;  // Shared placeholder texture
                    tile.texScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
                }
                
                // Request real texture with high priority (state machine handles transitions)
                if (tile.state == TileState::Unloaded) {
                    tile.requestPriority = static_cast<uint8_t>(Priority::Urgent);
                    if (scheduler_->Request(key, Priority::Urgent, 1.0f)) {
                        TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, nowSec);
                        TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, nowSec);
                    }
                }

                // Ensure coarse global DEM exists early for seamless terrain fallback (GE-style).
                if (demManager_) {
                    demManager_->Request(key, /*priority=*/2, /*score=*/1.0);
                }
            }
        }
    }
    
    std::cout << "Preloaded " << baseTileKeys_.size() << " base tiles (LOD 0-1)" << std::endl;
}

// =============================================================================
// SCREENSHOT CAPTURE (Visual Testing)
// =============================================================================

bool GlobeEngine::SaveScreenshot(const std::string& filename) {
    // Use framebuffer size (not window size) for HiDPI support
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    
    // Set pack alignment to 1 to avoid row padding issues with RGB (3 bytes/pixel)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    
    std::vector<unsigned char> pixels(width * height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    
    // Restore default alignment
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    
    // Flip vertically (OpenGL has origin at bottom-left)
    std::vector<unsigned char> flipped(width * height * 3);
    for (int y = 0; y < height; ++y) {
        memcpy(&flipped[y * width * 3], 
               &pixels[(height - 1 - y) * width * 3], 
               width * 3);
    }
    
    // Write PPM file (simple format, no external dependencies)
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(reinterpret_cast<char*>(flipped.data()), flipped.size());
    file.close();
    
    std::cout << "Screenshot saved: " << filename << std::endl;
    return true;
}

// =============================================================================
// VISUAL LOD TEST - Automated screenshot capture at each LOD level
// =============================================================================

void GlobeEngine::RunVisualLodTest() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║            VISUAL LOD TEST - Automated Screenshots               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
    
    // Create screenshots directory (portable)
    std::filesystem::create_directories("screenshots");
    
    struct LodTest {
        int lod;
        double alt;
        double lat;
        double lon;
        const char* name;
    };
    
    std::vector<LodTest> tests = {
        {0, 25000000, 0.0, 0.0, "LOD0_World"},
        {1, 15000000, 0.0, 0.0, "LOD1_Hemisphere"},
        {2, 8000000, 39.0, 35.0, "LOD2_Turkey_Region"},
        {3, 4000000, 39.0, 35.0, "LOD3_Turkey"},
        {4, 2000000, 41.0, 29.0, "LOD4_Istanbul_Region"},
        {5, 1000000, 41.0, 29.0, "LOD5_Istanbul"},
        {6, 500000, 41.015, 28.98, "LOD6_Bosphorus"},
        {7, 250000, 41.015, 28.98, "LOD7_Detail"},
        {8, 100000, 41.015, 28.98, "LOD8_High_Detail"},
    };
    
    for (const auto& test : tests) {
        std::cout << "Testing LOD " << test.lod << ": " << test.name << "...\n";
        
        // Fly to location
        FlyTo(test.lat, test.lon, test.alt, 0.0, 0.0, 0.1);
        
        // Wait for animation and tiles to load
        for (int frame = 0; frame < 120; ++frame) {  // ~2 seconds at 60fps
            double currentTime = glfwGetTime();
            double dt = currentTime - lastFrameTime_;
            lastFrameTime_ = currentTime;
            
            Update(dt, currentTime);
            Render();
            glfwSwapBuffers(window_);
            glfwPollEvents();
            
            if (glfwWindowShouldClose(window_)) return;
        }
        
        // Capture screenshot
        std::string filename = "screenshots/" + std::string(test.name) + ".ppm";
        SaveScreenshot(filename);
        
        // Get stats
        int visibleTiles = 0;
        int pendingTiles = 0;
        for (const auto& [key, tile] : tiles_) {
            if (currentLeafSet_.count(key) > 0) {
                visibleTiles++;
                if (tile.IsLoading()) pendingTiles++;
            }
        }
        
        std::cout << "  ✅ LOD " << test.lod << ": " << visibleTiles << " tiles visible, "
                  << pendingTiles << " pending\n";
    }
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║ Visual LOD Test Complete! Screenshots saved in ./screenshots/   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
}

} // namespace globe
