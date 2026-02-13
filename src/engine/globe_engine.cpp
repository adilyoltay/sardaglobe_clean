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
#include <limits>
#include <cctype>
#include <optional>

// ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// Network debug panel
#include "../debug/network_panel.h"

namespace globe {

#ifndef NATIVE_GLOBE_GIT_SHA
#define NATIVE_GLOBE_GIT_SHA "unknown"
#endif

namespace {

// Parse DEM provider string to enum. Strict: returns nullopt on unknown value.
// Caller (main.cpp CLI) must validate before calling. Unknown values indicate
// programmatic API misuse or config drift.
std::optional<DemProviderType> ParseDemProvider(const std::string& provider) {
    std::string lower = provider;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "google-earth") {
        return DemProviderType::GoogleEarth;
    }
    if (lower == "terrain-rgb" || lower == "terrain_rgb" || lower == "mapbox") {
        return DemProviderType::TerrainRGB;
    }
    // Strict: unknown values are an error
    return std::nullopt;
}

} // namespace

GlobeEngine::GlobeEngine(const Config& config)
    : config_(config) {
}

GlobeEngine::~GlobeEngine() {
    Shutdown();
}

bool GlobeEngine::Init() {
    shutdown_ = false;
    glReady_ = false;

    // Init GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return false;
    }
    glfwInitialized_ = true;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, config_.headless ? GLFW_FALSE : GLFW_TRUE);
    
    window_ = glfwCreateWindow(config_.windowWidth, config_.windowHeight, 
                               "Native Globe", nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create window" << std::endl;
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
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
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
        return false;
    }
    glReady_ = true;
    
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
        demConfig.basicAuthUserPwd = config_.demAuth;
        auto providerOpt = ParseDemProvider(config_.demProvider);
        if (!providerOpt.has_value()) {
            std::cerr << "[DEM] ERROR: Unknown provider '" << config_.demProvider << "'. "
                      << "Expected: terrain-rgb, google-earth" << std::endl;
            return false;
        }
        demConfig.providerType = providerOpt.value();
        demConfig.meshN = config_.demMeshN;
        demConfig.maxZoom = config_.demMaxZoom;
        demConfig.cacheSize = config_.demCacheSize;
        demConfig.debug = config_.demDebug;
        demConfig.timeoutSec = 30;
        demConfig.connectTimeoutSec = 10;
        // Wire GE config fields
        demConfig.geElevationEndpoint = config_.geElevationEndpoint;
        demConfig.geMeshEndpoint = config_.geMeshEndpoint;  // Phase 5 wiring
        demConfig.geHeaders = config_.geHeaders;
        demConfig.geTokenEnv = config_.geTokenEnv;
        demConfig.geElevationType = config_.geElevationType;
        demConfig.geEpoch = config_.geEpoch;
        demConfig.geChannel = config_.geChannel;
        demManager_ = std::make_unique<DemManager>(demConfig);
        
        // Startup health check
        auto health = demManager_->CheckHealth();
        if (health != DemHealthStatus::Healthy) {
            // google-earth provider requires hard-fail on init
            if (demConfig.providerType == DemProviderType::GoogleEarth) {
                std::cerr << "[DEM] ERROR: google-earth provider initialization failed (" 
                          << DemHealthStatusToString(health) << "). "
                          << "Use --dem-provider terrain-rgb or check configuration." << std::endl;
                return false;
            }
            // terrain-rgb can continue with flat terrain
            std::cerr << "[DEM] WARNING: DEM endpoint not healthy (" 
                      << DemHealthStatusToString(health) 
                      << "). Terrain will be flat until DEM becomes available." << std::endl;
        }
        
        // Init heightmap manager for GPU terrain displacement
        heightmapManager_ = std::make_unique<HeightmapManager>();
    }

    meshScheduler_ = std::make_unique<TileMeshScheduler>(config_, demManager_.get());
    
    // Init RockMesh manager for NodeData meshes (Phase 5 Sprint 1)
    if (config_.geMeshEnabled()) {
        rockMeshManager_ = std::make_unique<RockMeshManager>(config_);
        if (!rockMeshManager_->Init()) {
            std::cerr << "[RockMesh] Failed to initialize manager\n";
            rockMeshManager_.reset();
        } else {
            // Queue all configured quadkeys for loading
            for (const auto& qk : config_.geMeshQuadKeys) {
                std::cout << "[RockMesh] Requesting mesh: " << qk << "\n";
                rockMeshManager_->Request(qk);
            }
        }
    }
    
    // Set scheduler upload callback
    scheduler_->SetUploadCallback([this](Tile& tile) {
        textureManager_->QueueUpload(tile);
    });

    adaptiveBaseMaxInFlightFetches_ = std::max(1, config_.maxInFlightFetches);
    adaptiveBaseUploadBudgetMs_ = std::max(0.1, config_.uploadBudgetMs);
    adaptiveBaseMeshUploadBudgetMs_ = std::max(0.1, config_.meshUploadBudgetMs);

    // Set eviction callback for co-eviction cleanup.
    textureManager_->SetEvictionCallback([this](const TileKey& key) {
        if (heightmapManager_) {
            heightmapManager_->Release(key);
        }
        if (demManager_ && config_.demRasterCoEviction) {
            demManager_->UnpinAndEvict(key);
            ++demCoEvictions_;
        }
    });
    
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

    // Track current terrain mode so runtime toggles can invalidate cleanly.
    lastTerrainMode_ = config_.terrainDisplacementMode;
    
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
    if (shutdown_) {
        return;
    }
    shutdown_ = true;

    ShutdownImGui();
    
    if (demManager_) demManager_->Shutdown();
    if (meshScheduler_) meshScheduler_->Shutdown();
    if (heightmapManager_) heightmapManager_->Clear();
    
    // Shutdown RockMeshManager before GL context teardown (Phase 5)
    if (rockMeshManager_) {
        rockMeshManager_->Shutdown();
        rockMeshManager_.reset();
    }

    // Ensure a current GL context while destructing GL-owning subsystems/resources.
    if (window_) {
        glfwMakeContextCurrent(window_);
    }
    scheduler_.reset();
    textureManager_.reset();
    shaderManager_.reset();
    meshScheduler_.reset();
    demManager_.reset();
    flightController_.reset();
    camera_.reset();
    
    if (glReady_) {
        // Clear tiles
        for (auto& [key, tile] : tiles_) {
            if (tile.ownsTexture && tile.textureId != 0) {
                glDeleteTextures(1, &tile.textureId);
            }
            if (tile.vao != 0) glDeleteVertexArrays(1, &tile.vao);
            if (tile.vbo != 0) glDeleteBuffers(1, &tile.vbo);
            if (tile.ebo != 0 && tile.ownsEBO) glDeleteBuffers(1, &tile.ebo);
        }

        // Clear shared mesh template EBOs
        MeshTemplate::Clear();

        // Pivot gizmo GL resources
        if (pivotProgram_ != 0) glDeleteProgram(pivotProgram_);
        if (pivotVbo_ != 0) glDeleteBuffers(1, &pivotVbo_);
        if (pivotVao_ != 0) glDeleteVertexArrays(1, &pivotVao_);
        pivotProgram_ = 0;
        pivotVbo_ = 0;
        pivotVao_ = 0;
        pivotVertexCount_ = 0;
        pivotMvpLoc_ = -1;
        pivotColorLoc_ = -1;
    }

    tiles_.clear();
    
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glReady_ = false;
    if (glfwInitialized_) {
        glfwTerminate();
        glfwInitialized_ = false;
    }
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
    ++frameSerial_;

    // Update flight controller (handles momentum, animations)
    flightController_->Update(dt, currentTime);

    if (scheduler_) {
        if (config_.adaptiveResourceLimits) {
            auto schedulerStats = scheduler_->GetStats();
            const double fpsTarget = 60.0;
            const double fpsDeficit = std::clamp((fpsTarget - static_cast<double>(fps_)) / fpsTarget, 0.0, 1.0);
            const double queueLoad = static_cast<double>(
                schedulerStats.pendingFetches + schedulerStats.pendingDecodes + schedulerStats.activeFetches);
            const double queuePressure = std::clamp(
                queueLoad / static_cast<double>(std::max(1, adaptiveBaseMaxInFlightFetches_)),
                0.0, 2.0);
            const double queueDeficit = std::clamp(queuePressure - 1.0, 0.0, 1.0);
            const double targetPressure = std::clamp(fpsDeficit * 0.65 + queueDeficit * 0.35, 0.0, 1.0);

            // Damped update to avoid oscillation.
            adaptivePressure_ = adaptivePressure_ * 0.9 + targetPressure * 0.1;

            const double scale = std::clamp(1.0 - adaptivePressure_ * 0.5, 0.5, 1.0);
            config_.maxInFlightFetches = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(adaptiveBaseMaxInFlightFetches_) * scale)),
                8,
                adaptiveBaseMaxInFlightFetches_);
            config_.uploadBudgetMs = std::clamp(
                adaptiveBaseUploadBudgetMs_ * scale,
                0.5,
                adaptiveBaseUploadBudgetMs_);
            config_.meshUploadBudgetMs = std::clamp(
                adaptiveBaseMeshUploadBudgetMs_ * scale,
                0.5,
                adaptiveBaseMeshUploadBudgetMs_);
        } else {
            adaptivePressure_ = 0.0;
            config_.maxInFlightFetches = adaptiveBaseMaxInFlightFetches_;
            config_.uploadBudgetMs = adaptiveBaseUploadBudgetMs_;
            config_.meshUploadBudgetMs = adaptiveBaseMeshUploadBudgetMs_;
        }
    }

    // Terrain displacement mode is user-toggleable from the debug UI.
    // Invalidate all meshes/heightmaps on mode switches to avoid mixing CPU-baked geometry
    // with GPU heightmap displacement (double-displacement = kilometer-scale cliffs/walls).
    if (config_.terrainDisplacementMode != lastTerrainMode_) {
        std::cout << "[Terrain] Displacement mode changed: "
                  << (lastTerrainMode_ == DisplacementMode::CPU_MESH_BAKE ? "CPU" : "GPU")
                  << " -> "
                  << (config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE ? "CPU" : "GPU")
                  << std::endl;

        // Clear GPU heightmaps so min/max normalization stays consistent.
        if (heightmapManager_) {
            heightmapManager_->Clear();
        }

        rebuildPending_.clear();
        demMeshWaitStartSec_.clear();

        for (auto& [_, tile] : tiles_) {
            if (tile.hasMesh) {
                TileMeshBuilder::DeleteMesh(tile);
            }
            tile.meshPending = false;
            ++tile.meshRevision;
            tile.meshBuiltRevision = 0;
            tile.prevEdgeCoarserMask = 0;
            tile.edgeCoarserMask = 0;
            tile.stitchMask = 0;
            tile.skirtMask = 0;
            tile.cornerLods = glm::vec4(0.0f);
            tile.edgeGapMaxM = 0.0f;
            tile.edgeGapM = glm::vec4(0.0f);
            tile.seamGapMask = 0;
            tile.demUsed = false;
            tile.demPending = false;
            tile.demEffectiveLevel = tile.demTargetLevel;
        }

        // Rebuild base tiles immediately so we always have fallback coverage.
        for (const TileKey& base : baseTileKeys_) {
            auto it = tiles_.find(base);
            if (it == tiles_.end()) continue;
            Tile& tile = it->second;
            if (!tile.hasMesh || tile.meshBuiltRevision != tile.meshRevision) {
                BuildTileMesh(tile);
            }
        }

        lastTerrainMode_ = config_.terrainDisplacementMode;
        frameRequested_ = true;
    }
    
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
    lodSettings.maxRefinementsPerFrame = config_.maxRefinementsPerFrame;
    
    // GE parity: quality mode multiplier (1.0/2.0/4.0)
    lodSettings.qualityMultiplier = QualityModeToMultiplier(config_.qualityMode);
    
    // GE parity: minLodPixels culling (prevent sub-pixel tiles)
    // Note: Set to 256.0f for production, 0.0f for tests to avoid breaking forced-refine scenarios
    lodSettings.minLodPixels = 256.0f;  // GE default (tests use 0.0f)
    
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
    
    // Temporal leaf hold: keep recent leaves only when they fill a true coverage gap.
    // Keeping "covered" leaves (e.g., parent+children simultaneously) creates overlapping
    // geometry which shows up as dark grids / join tearing (z-fighting) and inflates CPU work.
    double nowTime = glfwGetTime();
    for (const TileKey& key : currentLeafSet_) {
        lastLeafSeenTime_[key] = nowTime;
    }

    // Fast coverage query: current leaves and all their ancestors.
    std::unordered_set<TileKey> currentLeafCoverage;
    currentLeafCoverage.reserve(currentLeafSet_.size() * 8);
    for (const TileKey& leaf : currentLeafSet_) {
        TileKey probe = leaf;
        while (true) {
            currentLeafCoverage.insert(probe);
            if (probe.level == 0) break;
            probe = probe.Parent();
        }
    }

    renderLeafSet_.clear();
    for (auto it = lastLeafSeenTime_.begin(); it != lastLeafSeenTime_.end(); ) {
        if (nowTime - it->second > leafHoldSeconds_) {
            it = lastLeafSeenTime_.erase(it);
            continue;
        }

        const TileKey& candidate = it->first;
        bool keep = false;
        if (currentLeafSet_.count(candidate) > 0) {
            keep = true;
        } else {
            bool covered = false;

            // Covered by a coarser current leaf (candidate is a descendant).
            TileKey probe = candidate;
            while (probe.level > 0) {
                probe = probe.Parent();
                if (currentLeafSet_.count(probe) > 0) {
                    covered = true;
                    break;
                }
            }

            // Covered by finer current leaves (candidate is an ancestor).
            if (!covered && currentLeafCoverage.count(candidate) > 0) {
                covered = true;
            }

            // Keep only if it contributes unique coverage.
            keep = !covered;
        }

        if (keep) {
            renderLeafSet_.insert(candidate);
        }
        ++it;
    }
    // If no leaves are available (startup edge case), render base tiles as a fallback
    if (renderLeafSet_.empty() && !baseTileKeys_.empty()) {
        renderLeafSet_ = baseTileKeys_;
    }

    // -------------------------------------------------------------------------
    // Render-Time Child Quorum (P2): collapse non-renderable leaves to an ancestor
    //
    // The LOD selector's quorum is texture-based (by design, to avoid mesh deadlocks),
    // but render-time can still end up mixed LODs due to missing mesh/terrain. In that
    // case RenderFrame falls back to ancestors, which can create delta-LOD joins that
    // were not accounted for by edge masks/stitching. This is the primary source of
    // "tile join tearing" + cliff/wall artifacts.
    //
    // Fix: build an effective leaf set for this frame that replaces any non-renderable
    // leaf with its parent, iterating upward. We still *request/mesh-build* the desired
    // fine leaves so refinement continues.
    // -------------------------------------------------------------------------
    const uint32_t loadingTextureId = textureManager_ ? textureManager_->GetLoadingTexture() : 0;
    // Block render-time quorum on terrain readiness only when DEM coverage exists.
    // The terrainExpected guard in classifyRenderBlockReason prevents over-collapse:
    // tiles with no DEM coverage render flat without blocking; tiles where DEM IS expected
    // but hasn't arrived collapse to parent (prevents flat-vs-displaced cliff artifacts).
    const bool requireTerrainForQuorum =
        config_.demEnabled &&
        demManager_ &&
        (demManager_->GetHealthStatus() == DemHealthStatus::Healthy);
    const bool useHeightmapForTerrain =
        (config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE) &&
        demManager_ &&
        (demManager_->GetHealthStatus() == DemHealthStatus::Healthy) &&
        heightmapManager_;

    auto hasAnyHeightmap = [&](const TileKey& key) -> bool {
        if (!useHeightmapForTerrain || !heightmapManager_) {
            return false;
        }
        TileKey probe = key;
        while (true) {
            if (heightmapManager_->HasTexture(probe)) {
                return true;
            }
            if (probe.level == 0) {
                break;
            }
            probe = probe.Parent();
        }
        return false;
    };

    auto hasAnyDemCoverage = [&](const TileKey& key) -> bool {
        if (!demManager_) {
            return false;
        }
        return demManager_->HasDataOrAncestor(key);
    };

    enum class RenderBlockReason : uint8_t { None, NoTile, NoMesh, NoTexture, NoTerrain };
    auto classifyRenderBlockReason = [&](const TileKey& key) -> RenderBlockReason {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            return RenderBlockReason::NoTile;
        }
        const Tile& tile = it->second;
        if (!tile.hasMesh) {
            return RenderBlockReason::NoMesh;
        }
        const bool hasRealTexture = tile.textureId != 0 && tile.textureId != loadingTextureId;
        // Black nodata tiles should not be considered renderable (consistency with RenderFrame)
        if (!hasRealTexture || tile.mostlyBlackOpaqueRaster) {
            return RenderBlockReason::NoTexture;
        }
        if (requireTerrainForQuorum) {
            const bool hasTerrain = useHeightmapForTerrain ? hasAnyHeightmap(tile.key) : tile.demUsed;
            // Only block rendering when terrain is expected to be available for this tile.
            // Some coarse/world-covering tiles have no DEM coverage; forcing a terrain quorum
            // on those tiles collapses the entire frame to a single blurry ancestor tile.
            const bool terrainExpected = tile.demPending || hasAnyDemCoverage(tile.key);
            if (!hasTerrain && terrainExpected) {
                return RenderBlockReason::NoTerrain;
            }
        }
        return RenderBlockReason::None;
    };

    // Keep the desired leaf set for priority and mesh build scheduling.
    std::unordered_set<TileKey> desiredLeafSet = renderLeafSet_;

    // Reset per-frame quorum telemetry.
    renderQuorumDowngrades_ = 0;
    renderQuorumNoMesh_ = 0;
    renderQuorumNoTexture_ = 0;
    renderQuorumNoTerrain_ = 0;

    // NOTE: Render-time quorum is intentionally ALWAYS-ON and independent of the selector's
    // child-quorum. Even when LOD selection is allowed to partially refine, RenderFrame will
    // still fall back for missing mesh/texture/terrain. If edge masks/stitching are computed
    // from the pre-fallback leaf set, we get unaccounted delta-LOD joins (tearing/walls).
    std::unordered_set<TileKey> effectiveLeafSet = renderLeafSet_;
    constexpr int kMaxQuorumPasses = 32;  // Must converge across deep LOD chains.

    bool anyBlocked = false;
    for (const TileKey& leaf : effectiveLeafSet) {
        if (leaf.level == 0) continue;
        if (classifyRenderBlockReason(leaf) != RenderBlockReason::None) {
            anyBlocked = true;
            break;
        }
    }

    if (anyBlocked) {
        for (int pass = 0; pass < kMaxQuorumPasses; ++pass) {
            // Parents to collapse this pass (standard quadtree quorum).
            // If ANY child is blocked, collapse the full sibling set to parent.
            // The terrainExpected guard in classifyRenderBlockReason prevents
            // over-collapse for regions with no DEM coverage.
            std::unordered_set<TileKey> candidateParents;
            candidateParents.reserve(effectiveLeafSet.size());
            for (const TileKey& leaf : effectiveLeafSet) {
                if (leaf.level == 0) continue;
                if (classifyRenderBlockReason(leaf) != RenderBlockReason::None) {
                    candidateParents.insert(leaf.Parent());
                }
            }

            if (candidateParents.empty()) break;

            std::vector<TileKey> collapseList;
            collapseList.reserve(candidateParents.size());
            for (const TileKey& parent : candidateParents) {
                auto children = parent.Children();
                bool fullSiblingSet = true;
                bool anyBlocked = false;
                for (const TileKey& child : children) {
                    if (effectiveLeafSet.count(child) == 0) {
                        fullSiblingSet = false;
                        break;
                    }
                    if (classifyRenderBlockReason(child) != RenderBlockReason::None) {
                        anyBlocked = true;
                    }
                }
                if (fullSiblingSet && anyBlocked) {
                    collapseList.push_back(parent);
                }
            }

            if (collapseList.empty()) break;

            bool changed = false;
            for (const TileKey& parent : collapseList) {
                auto children = parent.Children();
                int removed = 0;
                for (const TileKey& child : children) {
                    RenderBlockReason reason = classifyRenderBlockReason(child);
                    if (reason == RenderBlockReason::NoTerrain) {
                        ++renderQuorumNoTerrain_;
                    } else if (reason == RenderBlockReason::NoTexture) {
                        ++renderQuorumNoTexture_;
                    } else if (reason == RenderBlockReason::NoMesh || reason == RenderBlockReason::NoTile) {
                        ++renderQuorumNoMesh_;
                    }
                    removed += (effectiveLeafSet.erase(child) > 0) ? 1 : 0;
                }
                effectiveLeafSet.insert(parent);
                if (removed > 0) {
                    changed = true;
                    renderQuorumDowngrades_ += removed;
                }
            }

            if (!changed) {
                break;
            }
        }
    }

    renderLeafSet_ = std::move(effectiveLeafSet);

    if (renderLeafSet_.empty() && !baseTileKeys_.empty()) {
        renderLeafSet_ = baseTileKeys_;
    }
    
    double requestStartMs = glfwGetTime() * 1000.0;
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
        tile.lastFrameUsed = frameSerial_;
        tile.accessCount++;
        tile.importance = ranked.score;  // Store score for eviction decisions
        // Leaf-ness for streaming must match what we *intend to draw* (temporal hold + render-time quorum),
        // not only the selector's leaf set. Otherwise we can render an ancestor fallback while only
        // requesting children, which shows up as join tearing / cliffs due to unaccounted delta-LOD joins.
        bool isLeaf = (desiredLeafSet.count(key) > 0) || (renderLeafSet_.count(key) > 0);
        Priority priority = isLeaf ? Priority::Urgent : Priority::Normal;
        if (!isLeaf && config_.lodChildQuorum && quorumUrgentChildren.count(key) > 0) {
            priority = Priority::Urgent;
        }
        tile.requestPriority = static_cast<uint8_t>(priority);
        
        if (tile.state == TileState::Unloaded || tile.state == TileState::Canceled) {
            if (scheduler_->Request(key, priority, ranked.score)) {
                TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
            }
        }
        else if (tile.IsLoading()) {
            // Priority bump (GE-style): if this tile was previously queued as low/normal (prefetch)
            // and it becomes required/urgent, upgrade its rank in the fetcher so it doesn't wait behind
            // irrelevant background work.
            if (tile.state == TileState::Scheduled || tile.state == TileState::Fetching) {
                scheduler_->Request(key, priority, ranked.score);
            }
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
        // Child-quorum children should also get elevated DEM priority; otherwise imagery refines
        // first and we bake mixed flat/terrain meshes, which shows up as cliffs/walls at joins.
        if (demManager_) {
            int demPriority = isLeaf ? 2 : 1;
            if (!isLeaf && config_.lodChildQuorum && quorumUrgentChildren.count(key) > 0) {
                demPriority = 2;
            }
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
        tile.lastFrameUsed = frameSerial_;
        
        if (tile.state == TileState::Unloaded || tile.state == TileState::Canceled) {
            tile.lastAccessTime = currentTime;
            tile.accessCount++;
            if (scheduler_->Request(key, Priority::Low, ranked.score)) {  // Low priority prefetch with score
                TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, currentTime);
                TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, currentTime);
                ++prefetchCount;
            }
        }
    }
    for (const TileKey& key : baseTileKeys_) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            it->second.lastAccessTime = currentTime;
            it->second.lastFrameUsed = frameSerial_;
        }
    }
    for (const TileKey& key : renderLeafSet_) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            it->second.lastAccessTime = currentTime;
            it->second.lastFrameUsed = frameSerial_;
        }
    }
    for (const TileKey& key : desiredLeafSet) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            it->second.lastFrameUsed = frameSerial_;
        }
    }
    frameTimings_.requestLoopMs = (glfwGetTime() * 1000.0) - requestStartMs;

    if (config_.cancelAfterFramesUntouched > 0) {
        std::vector<TileKey> cancelKeys;
        cancelKeys.reserve(64);
        const uint64_t untouchedLimit = static_cast<uint64_t>(config_.cancelAfterFramesUntouched);

        for (const auto& [key, tile] : tiles_) {
            if (!tile.IsLoading()) {
                continue;
            }
            if (baseTileKeys_.count(key) > 0) {
                continue;
            }
            if (selection.required.count(key) > 0) {
                continue;
            }
            if (renderLeafSet_.count(key) > 0 || desiredLeafSet.count(key) > 0) {
                continue;
            }
            uint64_t ageFrames = frameSerial_ > tile.lastFrameUsed
                ? (frameSerial_ - tile.lastFrameUsed)
                : 0;
            if (ageFrames >= untouchedLimit) {
                cancelKeys.push_back(key);
            }
        }

        for (const TileKey& key : cancelKeys) {
            scheduler_->Cancel(key);
        }
    }
    
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
    int textureUploadsThisFrame = textureManager_->ProcessUploads(tiles_, config_.uploadBudgetMs);
    frameTimings_.textureUploadMs = (glfwGetTime() * 1000.0) - uploadStartMs;

    int heightmapUploadsThisFrame = 0;
    
    // DEM update + optional heightmap upload.
    // Requests are already coordinated in the required imagery traversal above (P5.3).
    double demUpdateStartMs = glfwGetTime() * 1000.0;
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
        // Pin DEM keys for the effective render leaf set (post-quorum). These are the tiles
        // that actually get drawn; pinning only selector leaves can evict the rendered
        // fallbacks and reintroduce join tearing/cliffs.
        for (const TileKey& leaf : renderLeafSet_) {
            auto it = tiles_.find(leaf);
            if (it == tiles_.end()) continue;
            const Tile& tile = it->second;
            int lvl = std::clamp(static_cast<int>(tile.demTargetLevel), 0, leaf.level);
            pushPinned(keyAtLevel(leaf, lvl));
        }

        // Pin edge-coherent DEM keys (often coarser, shared across neighbors) if budget allows.
        for (const TileKey& leaf : renderLeafSet_) {
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
            // Use ranked required keys (view-centered) so visible tiles get heightmaps first.
            int queued = 0;
            constexpr int kMaxQueuesPerFrame = 256;
            for (const RankedTile& ranked : tilePyramid_.GetRankedRequired()) {
                if (queued >= kMaxQueuesPerFrame) break;
                const TileKey& key = ranked.key;
                if (!demManager_->HasData(key)) continue;
                if (heightmapManager_->HasTexture(key)) continue;
                DemGridData demData;
                if (demManager_->GetGridData(key, demData)) {
                    heightmapManager_->QueueUpload(key, demData, static_cast<float>(config_.demHeightScaleBase * config_.demExaggerationFactor));
                    ++queued;
                }
            }

            // Also queue heightmaps for leaf tiles (render + desired sets).
            // Render set keeps currently drawn fallbacks complete; desired set prevents
            // deadlock where quorum collapse shrinks renderLeafSet_ and starves fine-tile
            // heightmap uploads forever.
            auto queueLeafHeightmapWithAncestorFallback = [&](const TileKey& key) {
                if (queued >= kMaxQueuesPerFrame) {
                    return;
                }
                // Skip if any ancestor already has a heightmap (matches the missing-leaf check).
                {
                    bool anyHm = false;
                    TileKey probe = key;
                    while (true) {
                        if (heightmapManager_->HasTexture(probe)) { anyHm = true; break; }
                        if (probe.level == 0) break;
                        probe = probe.Parent();
                    }
                    if (anyHm) {
                        return;
                    }
                }
                // Try exact DEM, then walk ancestors.
                TileKey demKey = key;
                bool foundDem = false;
                while (true) {
                    if (demManager_->HasData(demKey)) { foundDem = true; break; }
                    if (demKey.level == 0) break;
                    demKey = demKey.Parent();
                }
                if (!foundDem) {
                    // DEM data missing — request it so the heightmap can be uploaded next frame.
                    demManager_->Request(key, /*priority=*/2, /*score=*/100.0);
                    return;
                }
                DemGridData demData;
                if (demManager_->GetGridData(demKey, demData)) {
                    heightmapManager_->QueueUpload(demKey, demData, static_cast<float>(config_.demHeightScaleBase * config_.demExaggerationFactor));
                    ++queued;
                }
            };
            for (const TileKey& key : renderLeafSet_) {
                if (queued >= kMaxQueuesPerFrame) break;
                queueLeafHeightmapWithAncestorFallback(key);
            }
            for (const TileKey& key : desiredLeafSet) {
                if (queued >= kMaxQueuesPerFrame) break;
                queueLeafHeightmapWithAncestorFallback(key);
            }

            // Process pending heightmap uploads (budgeted).
            double budgetMs = 2.0;
            if (heightmapManager_->GetPendingCount() > 0) {
                budgetMs = 3.0;
            }
            heightmapUploadsThisFrame = heightmapManager_->ProcessUploads(budgetMs);
        }
    }
    
    // Compute edge masks + DEM effective level for seam/cliff fixes.
    // An edge is "coarser" if the neighbor at same level is NOT a leaf but its parent IS.
    frameTimings_.demUpdateMs = (glfwGetTime() * 1000.0) - demUpdateStartMs;

    double edgeMaskStartMs = glfwGetTime() * 1000.0;
    const bool hasDem = (demManager_ != nullptr);

    // We need two leaf contexts:
    // - renderLeafSet_: what we actually draw this frame (post render-time quorum).
    // - desiredLeafSet: what we are refining toward (pre-quorum).
    //
    // IMPORTANT: DEM/mesh readiness must keep advancing for desired tiles even when we
    // temporarily render an ancestor fallback; otherwise the engine can get stuck drawing
    // only coarse tiles (no progress toward refinement).
    const std::unordered_set<TileKey>& renderLeafSetForMasks = renderLeafSet_;
    const std::unordered_set<TileKey>& desiredLeafSetForMasks = desiredLeafSet;

    // Reset corner LOD uniforms only for tiles in the active leaf sets (not all tiles).
    // Previous approach iterated all tiles_ which is O(1400+) at zoom 8; this is O(leaves).
    auto resetCornerLods = [&](const TileKey& key) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            it->second.cornerLods = glm::vec4(0.0f);
        }
    };
    for (const TileKey& key : renderLeafSetForMasks) { resetCornerLods(key); }
    for (const TileKey& key : desiredLeafSetForMasks) { resetCornerLods(key); }
    // Also reset previous frame's leaves so stale values don't persist.
    for (const TileKey& key : prevResetCornerLodKeys_) { resetCornerLods(key); }
    prevResetCornerLodKeys_.clear();
    prevResetCornerLodKeys_.insert(renderLeafSetForMasks.begin(), renderLeafSetForMasks.end());
    prevResetCornerLodKeys_.insert(desiredLeafSetForMasks.begin(), desiredLeafSetForMasks.end());

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

    auto buildLeafCoverage = [&](const std::unordered_set<TileKey>& leafSet) {
        // Fast "does this region have any leaf coverage?" query for finer-neighbor detection.
        // Build the ancestor set once; avoids O(N^2) scans at high LOD.
        std::unordered_set<TileKey> coverage;
        coverage.reserve(leafSet.size() * 8);
        for (const TileKey& leaf : leafSet) {
            TileKey probe = leaf;
            while (true) {
                coverage.insert(probe);
                if (probe.level == 0) break;
                probe = probe.Parent();
            }
        }
        return coverage;
    };

    const std::unordered_set<TileKey> renderLeafCoverage = buildLeafCoverage(renderLeafSetForMasks);
    const std::unordered_set<TileKey> desiredLeafCoverage = buildLeafCoverage(desiredLeafSetForMasks);

    auto keyAtLevel = [](TileKey k, int targetLevel) -> TileKey {
        int lvl = std::clamp(targetLevel, 0, k.level);
        while (k.level > lvl) {
            k = k.Parent();
        }
        return k;
    };

    auto updateTileMasksAndDemTargets =
        [&](const TileKey& key,
            const std::unordered_set<TileKey>& contextLeafSet,
            const std::unordered_set<TileKey>& contextCoverage) {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) return;
        
        uint8_t newEdgeCoarserMask = 0;
        uint8_t newEdgeFinerMask = 0;
        uint8_t newSkirtMask = 0;
        // Per-edge relative LOD delta (tile level - neighbor cover level) used for uCornerLods.
        // Unlike binary edge masks, this preserves >1 level differences during aggressive
        // render-time fallback, enabling smoother heightmap textureLod interpolation.
        float edgeCoarserDelta[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // N,E,S,W
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
                
                bool neighborSameIsLeaf = contextLeafSet.count(neighborSame) > 0;
                if (!neighborSameIsLeaf) {
                    // Find the coarsest rendered leaf that covers this neighboring region.
                    int coarserDelta = 0;
                    TileKey probe = neighborSame;
                    while (true) {
                        if (contextLeafSet.count(probe) > 0) {
                            coarserDelta = std::max(0, key.level - probe.level);
                            break;
                        }
                        if (probe.level == 0) break;
                        probe = probe.Parent();
                    }

                    if (coarserDelta > 0) {
                        newEdgeCoarserMask |= edgeBits[dir];
                        edgeCoarserDelta[dir] = static_cast<float>(coarserDelta);
                        if (!config_.edgeStitching) {
                            // No stitching → always use skirts on LOD boundaries.
                            newSkirtMask |= edgeBits[dir];
                        } else if (coarserDelta > 1) {
                            // Stitching only handles delta=1. For larger gaps, add a safety
                            // skirt to cover the geometry mismatch that stitching cannot fix.
                            newSkirtMask |= edgeBits[dir];
                        }
                        continue;
                    }

                    // Neighbor region is finer if any leaf covers the regionKey.
                    // For finer-neighbor coverage, avoid redundant skirts (visible black seams).
                    // Keep skirts only for true coverage holes.
                    if (contextCoverage.count(neighborSame) > 0) {
                        newEdgeFinerMask |= edgeBits[dir];
                    } else {
                        newSkirtMask |= edgeBits[dir];
                    }
                } else if (hasDem) {
                    // FIX: Same-LOD neighbor DEM mismatch skirt.
                    // When both tiles are at the same LOD level but compute shared border
                    // vertices from different DEM source tiles, floating-point differences
                    // in bilinear interpolation produce residual height cracks.
                    // Add a skirt on edges where DEM state differs.
                    auto nit = tiles_.find(neighborSame);
                    if (nit != tiles_.end()) {
                        const Tile& neighborTile = nit->second;
                        const Tile& selfTile = it->second;
                        bool selfHasDem = selfTile.demUsed;
                        bool neighborHasDem = neighborTile.demUsed;
                        // One side flat, other side displaced → guaranteed height cliff.
                        if (selfHasDem != neighborHasDem) {
                            newSkirtMask |= edgeBits[dir];
                        }
                        // Both have DEM but at different effective levels → residual crack.
                        else if (selfHasDem && neighborHasDem &&
                                 selfTile.demEffectiveLevel != neighborTile.demEffectiveLevel) {
                            newSkirtMask |= edgeBits[dir];
                        }
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
                if (contextLeafSet.count(neighborSame) > 0) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 && contextLeafSet.count(neighborSame.Parent()) > 0) {
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
                        if (contextLeafSet.count(children[static_cast<std::size_t>(a)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (contextLeafSet.count(children[static_cast<std::size_t>(b)]) > 0) {
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

        // FIX A: Seam→skirt feedback with hysteresis (latch).
        //
        // Problem: without latch, seam feedback oscillates every frame:
        //   Frame N: gap detected → skirt added → mesh rebuild
        //   Frame N+1: skirt hides gap → seamGapMask=0 → skirt removed → mesh rebuild
        //   Frame N+2: gap reappears → ... (visible as tile "vibration")
        //
        // Solution: once seamGapMask triggers a skirt on an edge, latch it ON.
        // The latch only resets when a *structural* change occurs (DEM level change,
        // edge coarser mask change), not when the seam measurement itself clears.
        if (config_.selectiveSkirts) {
            // Accumulate new seam gaps into the latch (never shrink from seam feedback alone).
            if (tile.seamGapMask != 0) {
                // For delta-LOD stitched edges, only latch if gap is significant (>= 12m).
                uint8_t seamForceMask = tile.seamGapMask;
                if (config_.edgeStitching) {
                    constexpr float kStitchedEdgeForceSkirtGapM = 12.0f;
                    const uint8_t deltaLodMask = static_cast<uint8_t>(newEdgeCoarserMask | newEdgeFinerMask);

                    auto edgeGapForBit = [&](uint8_t edgeBit) -> float {
                        if (edgeBit == Tile::EDGE_NORTH) return tile.edgeGapM.x;
                        if (edgeBit == Tile::EDGE_EAST) return tile.edgeGapM.y;
                        if (edgeBit == Tile::EDGE_SOUTH) return tile.edgeGapM.z;
                        return tile.edgeGapM.w;
                    };

                    const uint8_t edgeBits[4] = {
                        Tile::EDGE_NORTH, Tile::EDGE_EAST, Tile::EDGE_SOUTH, Tile::EDGE_WEST
                    };
                    for (uint8_t edgeBit : edgeBits) {
                        if ((seamForceMask & edgeBit) == 0) continue;
                        if ((deltaLodMask & edgeBit) == 0) continue;
                        if (edgeGapForBit(edgeBit) < kStitchedEdgeForceSkirtGapM) {
                            seamForceMask = static_cast<uint8_t>(seamForceMask & ~edgeBit);
                        }
                    }
                }
                tile.latchedSeamSkirtMask |= seamForceMask;
            }
            newSkirtMask |= tile.latchedSeamSkirtMask;
        }

        // Stitch only for delta=1; delta>1 uses skirt (safety fallback)
        static const uint8_t kEdgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST,
                                            Tile::EDGE_SOUTH, Tile::EDGE_WEST};
        uint8_t stitchedMask = 0;
        if (config_.edgeStitching) {
            for (int dir = 0; dir < 4; ++dir) {
                if ((newEdgeCoarserMask & kEdgeBits[dir]) != 0 && edgeCoarserDelta[dir] == 1.0f) {
                    stitchedMask |= kEdgeBits[dir];
                }
            }
        }
        uint8_t resolvedSkirtMask = config_.selectiveSkirts
            ? newSkirtMask
            : static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_EAST | Tile::EDGE_SOUTH | Tile::EDGE_WEST);

        tile.cornerLods = CornerLodsFromEdgeDeltas(edgeCoarserDelta[0], edgeCoarserDelta[1],
                                                   edgeCoarserDelta[2], edgeCoarserDelta[3]);
        bool revisionChanged = false;
        // FIX A: Track structural changes that should reset the seam-skirt latch.
        bool structuralChange = false;
        if (newEdgeCoarserMask != tile.edgeCoarserMask) {
            tile.edgeCoarserMask = newEdgeCoarserMask;
            if (tile.edgeCoarserMask != tile.prevEdgeCoarserMask) {
                revisionChanged = true;
                structuralChange = true;
            }
        }
        if (tile.stitchMask != stitchedMask) {
            tile.stitchMask = stitchedMask;
            revisionChanged = true;
            structuralChange = true;
        }
        if (tile.skirtMask != resolvedSkirtMask) {
            tile.skirtMask = resolvedSkirtMask;
            revisionChanged = true;
        }
        uint8_t newEffectiveLevel = static_cast<uint8_t>(std::clamp(effectiveDemLevel, 0, 255));
        if (tile.demTargetLevel != newEffectiveLevel) {
            tile.demTargetLevel = newEffectiveLevel;
            revisionChanged = true;
            // NOTE: DEM level changes rebuild the mesh but must NOT reset the
            // seam-skirt latch.  When neighbor DEM instability causes the level
            // to flip-flop between frames, resetting the latch each time
            // re-introduces the gap→skirt→gap oscillation (visible shimmer).
            // Topology changes (edgeCoarserMask, stitchMask) still reset it.
        }

        // FIX A: Reset seam-skirt latch on structural changes so stale skirts
        // are re-evaluated with fresh geometry. Without this, skirts latched from
        // an old DEM level persist forever even when no longer needed.
        if (structuralChange) {
            tile.latchedSeamSkirtMask = 0;
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
                if (hasAnyDem && !tile.meshPending &&
                    tile.meshBuiltRevision == tile.meshRevision) {
                    revisionChanged = true;
                    ++demTriggeredMeshRebuilds_;
                }
            } else if (hasOwnDem && hasAllCoarserDem) {
                revisionChanged = true;
                tile.demPending = false;
                ++demTriggeredMeshRebuilds_;
            }
        }

        // Hard safety: in CPU mesh-bake mode, a render-leaf mesh should never remain "flat"
        // once any DEM data (exact or ancestor) is available. This catches mode-mix / stale
        // results and prevents persistent cliffs/walls from flat+terrain adjacency.
        //
        // Guard: only trigger ONE revision bump per build cycle. Without this guard,
        // tiles whose worker thread can't find cached DEM get meshRevision incremented
        // every frame (13-19 tiles at zoom 6), causing perpetual rebuild churn.
        if (demManager_ && config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
            if (!tile.demUsed &&
                demManager_->HasDataOrAncestor(key) &&
                !tile.meshPending &&
                tile.meshBuiltRevision == tile.meshRevision) {
                revisionChanged = true;
                ++demTriggeredMeshRebuilds_;
            }
        }

        // If DEM coherence policy locked this tile to an ancestor level (demTargetLevel),
        // make sure that ancestor DEM tile is actually requested and rebuild once it arrives.
        //
        // Without this, the mesh builder may fall back to "whatever is cached" (including an
        // exact child tile) and adjacent leaves can lift independently, creating large cracks.
        if (demManager_ && config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
            TileKey targetDemKey = keyAtLevel(key, static_cast<int>(tile.demTargetLevel));
            const bool targetIsAncestor = !(targetDemKey == key);
            if (targetIsAncestor) {
                demManager_->Request(targetDemKey, /*priority=*/2, /*score=*/tile.importance);

                // Converge: if the coherent target DEM is now cached but the mesh was built using
                // a different level, schedule a rebuild.
                if (tile.demUsed &&
                    tile.demEffectiveLevel != tile.demTargetLevel &&
                    demManager_->HasData(targetDemKey) &&
                    !tile.meshPending) {
                    revisionChanged = true;
                    ++demTriggeredMeshRebuilds_;
                }
            }
        }
        
        {
            const int tileExpectedSegs = AdaptiveMeshSegments(key.level, config_.meshSegments, config_.demMeshN, hasDem);
            if (tile.builtSegments != 0 && tile.builtSegments != tileExpectedSegs) {
                revisionChanged = true;
            }
        }
        
        if (revisionChanged) {
            ++tile.meshRevision;
        }
    };

    // Update desired-only tiles first so DEM/mask revisions can progress even when
    // renderLeafSet_ collapses to a coarse fallback.
    std::vector<TileKey> desiredOnlyKeys;
    desiredOnlyKeys.reserve(desiredLeafSetForMasks.size());
    for (const TileKey& key : desiredLeafSetForMasks) {
        if (renderLeafSetForMasks.count(key) == 0) {
            desiredOnlyKeys.push_back(key);
        }
    }
    for (const TileKey& key : desiredOnlyKeys) {
        updateTileMasksAndDemTargets(key, desiredLeafSetForMasks, desiredLeafCoverage);
    }
    for (const TileKey& key : renderLeafSetForMasks) {
        updateTileMasksAndDemTargets(key, renderLeafSetForMasks, renderLeafCoverage);
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

        auto updateDemEdgePackForTile =
            [&](const TileKey& key,
                const std::unordered_set<TileKey>& contextLeafSet) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) return;
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
                if (contextLeafSet.count(neighborSame) > 0) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 && contextLeafSet.count(neighborSame.Parent()) > 0) {
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
                        if (contextLeafSet.count(children[static_cast<std::size_t>(a)]) > 0) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (contextLeafSet.count(children[static_cast<std::size_t>(b)]) > 0) {
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

                // Seam feedback: if we still measure a significant gap on this edge,
                // bias ONE level coarser so both sides converge on a shared ancestor DEM.
                //
                // IMPORTANT: Do NOT use gap-proportional bias (e.g. log2(gapM/threshold)).
                // That creates a positive feedback catastrophe: large gap → aggressive bias →
                // edge samples at root DEM level → even larger gap → locked at max bias.
                // The commonAncestorLevel() above already finds the correct shared level;
                // at most one extra coarsening step is needed for residual rounding.
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

            // Request edge-coherent DEM keys when they are coarser than our current coherence target.
            // Pinning alone does not fetch; without explicit requests these shared ancestors can
            // remain missing and cracks/walls persist indefinitely.
            for (int dir = 0; dir < 4; ++dir) {
                const int lvl = std::clamp(edgeLevels[dir], 0, key.level);
                if (lvl < selfDemLevel) {
                    const TileKey edgeKey = keyAtLevel(key, lvl);
                    demManager_->Request(edgeKey, /*priority=*/2, /*score=*/tile.importance);
                }
            }

            // Mesh invalidation for coherent DEM edge keys:
            // When edge-coherent DEM keys (common ancestors) arrive after a mesh was baked using
            // only finer per-tile DEM keys, rebuild so border sampling converges and seams fade.
            {
                int levels[4] = {edgeLevels[0], edgeLevels[1], edgeLevels[2], edgeLevels[3]};
                for (int dir = 0; dir < 4; ++dir) {
                    const int lvl = std::clamp(levels[dir], 0, key.level);
                    const TileKey edgeKey = keyAtLevel(key, lvl);

                    // If the coherent edge DEM just became available, rebuild so border sampling
                    // uses the requested common-ancestor key (reduces cracks/cliffs on joins).
                    if (tile.demUsed &&
                        tile.demSourceLevelMin > static_cast<uint8_t>(lvl) &&
                        demManager_->HasData(edgeKey) &&
                        !tile.meshPending) {
                        ++tile.meshRevision;
                    }
                }
            }
        };

        for (const TileKey& key : desiredOnlyKeys) {
            updateDemEdgePackForTile(key, desiredLeafSetForMasks);
        }
        for (const TileKey& key : renderLeafSetForMasks) {
            updateDemEdgePackForTile(key, renderLeafSetForMasks);
        }
    }
    
    frameTimings_.edgeMaskMs = (glfwGetTime() * 1000.0) - edgeMaskStartMs;

    double meshStartMs = glfwGetTime() * 1000.0;
    frameTimings_.meshRebuildsQueued = 0;
    // Queue mesh builds for:
    // - the effective render leaf set (what we will actually draw this frame), and
    // - the desired leaf set (what we are refining toward).
    //
    // This prevents render-time quorum from deadlocking on "NoMesh" by ensuring fine
    // leaves keep getting their mesh builds scheduled even when we temporarily render
    // an ancestor fallback for continuity.
    {
        std::vector<TileKey> buildKeys;
        buildKeys.reserve(renderLeafSetForMasks.size() + desiredLeafSet.size());
        for (const TileKey& key : renderLeafSetForMasks) {
            buildKeys.push_back(key);
        }
        for (const TileKey& key : desiredLeafSet) {
            if (renderLeafSetForMasks.count(key) == 0) {
                buildKeys.push_back(key);
            }
        }

        for (const TileKey& key : buildKeys) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) continue;
            Tile& tile = it->second;
            if (!tile.hasMesh || tile.meshBuiltRevision != tile.meshRevision) {
                const bool isRendered = renderLeafSetForMasks.count(key) > 0;
                if (QueueMeshBuild(key, isRendered)) {
                    ++frameTimings_.meshRebuildsQueued;
                }
            }
        }
    }
    
    // Process completed mesh builds with frame budget
    int meshUploadsThisFrame = ProcessMeshResults();
    
    // Sprint 2: Update RockMeshManager with visible quadkeys
    if (rockMeshManager_) {
        // Update generation counter
        static uint64_t viewportVersion = 0;
        viewportVersion++;
        rockMeshManager_->SetViewportVersion(viewportVersion);
        
        // Convert visible leaves to TileKeys and update
        std::vector<TileKey> visibleLeaves;
        visibleLeaves.reserve(renderLeafSet_.size());
        for (const auto& key : renderLeafSet_) {
            visibleLeaves.push_back(key);
        }
        rockMeshManager_->UpdateVisibleQuadKeys(visibleLeaves);
        
        // Process priority queue (dispatch requests within budget)
        int dispatched = rockMeshManager_->ProcessPriorityQueue(config_.geMeshRequestBudgetMs);
        if (dispatched > 0) {
            frameRequested_ = true;
        }
    }
    
    // Process RockMesh uploads (Phase 5)
    if (rockMeshManager_) {
        bool rockUploaded = rockMeshManager_->ProcessUploads(config_.meshUploadBudgetMs);
        if (rockUploaded) {
            frameRequested_ = true;  // Request render frame for new mesh
        }
    }
    
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
            tilePyramid_.IsPrefetch(key) ||
            renderLeafSet_.count(key) > 0 ||
            desiredLeafSet.count(key) > 0) {
            ++it;
            continue;
        }
        if (tile.state == TileState::Unloaded || tile.state == TileState::Canceled) {
            double last = tile.lastAccessTime;
            if (last <= 0.0 || (cleanupNow - last) > UNLOADED_STALE_SEC) {
                // Release heightmap texture before erasing
                if (heightmapManager_) {
                    heightmapManager_->Release(it->first);
                }
                if (textureManager_) {
                    textureManager_->ReleaseTileResources(tile);
                }
                if (tile.vao != 0) {
                    glDeleteVertexArrays(1, &tile.vao);
                }
                if (tile.vbo != 0) {
                    glDeleteBuffers(1, &tile.vbo);
                }
                if (tile.ebo != 0 && tile.ownsEBO) {
                    glDeleteBuffers(1, &tile.ebo);
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
                if (tile.vao != 0) {
                    glDeleteVertexArrays(1, &tile.vao);
                }
                if (tile.vbo != 0) {
                    glDeleteBuffers(1, &tile.vbo);
                }
                if (tile.ebo != 0 && tile.ownsEBO) {
                    glDeleteBuffers(1, &tile.ebo);
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
    if (textureUploadsThisFrame > 0) {
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
    if (heightmapUploadsThisFrame > 0) {
        hasBackgroundWork = true;
    }

    // Request-driven frame invalidation: even if queues drained to 0, applying uploads this
    // frame changes visible output and should trigger at least one render.
    if (meshUploadsThisFrame > 0) {
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
    const bool requireTerrainForSeamStats =
        config_.demEnabled &&
        demManager_ &&
        (demManager_->GetHealthStatus() == DemHealthStatus::Healthy);
    const bool requireTerrainForDraw = requireTerrainForSeamStats;
    auto drawStats = renderFrame_->DrawTiles(
        sceneSnapshot_.leafSet, tiles_, mvp, sceneSnapshot_.cameraPos,
        sceneSnapshot_.currentTime, cameraSpeedKmPerSec_,
        requireTerrainForDraw,
        sceneSnapshot_.useLogDepth, sceneSnapshot_.logDepthFarKm,
        sceneSnapshot_.wireframe, sceneSnapshot_.loadingTexture,
        hmForRender,
        demManager_.get()
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
    if (demManager_ && config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
        const uint32_t loadingTex = sceneSnapshot_.loadingTexture;
        auto isRenderableLeafForDemStats = [&](const Tile& tile) -> bool {
            return tile.hasMesh && tile.textureId != 0 && tile.textureId != loadingTex;
        };

        int visibleDemTiles = 0;
        for (const TileKey& key : sceneSnapshot_.leafSet) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) {
                continue;
            }
            const Tile& tile = it->second;
            if (!isRenderableLeafForDemStats(tile)) {
                continue;
            }
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

        auto isSeamTile = [&](const TileKey& key) -> bool {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) {
                return false;
            }
            const Tile& tile = it->second;
            if (!isRenderableLeafForDemStats(tile)) {
                return false;
            }
            if (requireTerrainForSeamStats && !tile.demUsed) {
                return false;
            }
            return hasBorderHeights(tile);
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
            // Stitch-aware seam metric:
            // When delta-LOD stitching is enabled, fine-tile boundary triangles deliberately
            // skip every other boundary vertex to match the coarser edge tessellation.
            // Measuring seam gaps at those skipped vertices over-reports cliffs and
            // triggers bad feedback (extra skirts / over-coarsened DEM edges).
            const bool fineEdgeStitched = (delta == 1) && ((fineTile->stitchMask & fineEdgeBit) != 0);
            const int fineStep = fineEdgeStitched ? 2 : 1;

            auto accumulateSample = [&](int i) {
                double tFine = (fineSeg == 0) ? 0.0 : static_cast<double>(i) / static_cast<double>(fineSeg);
                double tCoarse = std::clamp(tFine * scale + offset, 0.0, 1.0);
                double hFineKm = sampleEdgeKm(*fineTile, fineEdgeIndex, tFine);
                double hCoarseKm = sampleEdgeKm(*coarseTile, coarseEdgeIndex, tCoarse);
                double deltaM = std::abs(hFineKm - hCoarseKm) * 1000.0;
                outMaxDeltaM = std::max(outMaxDeltaM, deltaM);
            };

            for (int i = 0; i <= fineSeg; i += fineStep) {
                accumulateSample(i);
            }
            if (fineStep > 1 && (fineSeg % fineStep) != 0) {
                accumulateSample(fineSeg);  // Ensure the endpoint is always included.
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
            if (!isRenderableLeafForDemStats(tileA)) continue;
            if (requireTerrainForSeamStats && !tileA.demUsed) continue;
            if (!hasBorderHeights(tileA)) continue;

            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) continue;

                std::vector<TileKey> neighborLeaves;
                neighborLeaves.reserve(2);

                if (sceneSnapshot_.leafSet.count(neighborSame) > 0 && isSeamTile(neighborSame)) {
                    neighborLeaves.push_back(neighborSame);
                } else if (neighborSame.level > 0 &&
                           sceneSnapshot_.leafSet.count(neighborSame.Parent()) > 0 &&
                           isSeamTile(neighborSame.Parent())) {
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
                        const TileKey childA = children[static_cast<std::size_t>(a)];
                        const TileKey childB = children[static_cast<std::size_t>(b)];
                        if (sceneSnapshot_.leafSet.count(childA) > 0 && isSeamTile(childA)) {
                            neighborLeaves.push_back(children[static_cast<std::size_t>(a)]);
                        }
                        if (sceneSnapshot_.leafSet.count(childB) > 0 && isSeamTile(childB)) {
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
            seamGapMaxM = *std::max_element(seamDeltas.begin(), seamDeltas.end());
            std::size_t p95Index = static_cast<std::size_t>(
                std::floor(0.95 * static_cast<double>(seamDeltas.size() - 1)));
            std::nth_element(seamDeltas.begin(), seamDeltas.begin() + static_cast<std::ptrdiff_t>(p95Index),
                             seamDeltas.end());
            seamGapP95M = seamDeltas[p95Index];
        }
    }
    
    // Render pivot gizmo (Google Earth style target icon)
    RenderPivot(mvp);
    
    // Render RockTree meshes (Phase 5 Sprint 1)
    if (rockMeshManager_ && rockMeshManager_->GetUploadedCount() > 0) {
        // Enable polygon offset to prevent z-fighting with base terrain
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        
        // Disable culling for first bring-up (helps debug visibility)
        bool cullWasEnabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        
        // Bind tile shader with neutral uniforms
        GLuint tileProgram = shaderManager_->GetTileProgram();
        if (tileProgram != 0) {
            glUseProgram(tileProgram);
            
            // Set uniforms for rockmesh rendering
            // uHasHeightmap = 0 (no heightmap displacement)
            GLint hasHeightmapLoc = glGetUniformLocation(tileProgram, "uHasHeightmap");
            if (hasHeightmapLoc >= 0) glUniform1i(hasHeightmapLoc, 0);
            
            // uTerrainMorph = 1.0 (fully morphed)
            GLint morphLoc = glGetUniformLocation(tileProgram, "uTerrainMorph");
            if (morphLoc >= 0) glUniform1f(morphLoc, 1.0f);
            
            // uFade = 1.0 (fully visible)
            GLint fadeLoc = glGetUniformLocation(tileProgram, "uFade");
            if (fadeLoc >= 0) glUniform1f(fadeLoc, 1.0f);
            
            // Texture scale/offset = identity
            GLint texScaleLoc = glGetUniformLocation(tileProgram, "uTexScale");
            GLint texOffsetLoc = glGetUniformLocation(tileProgram, "uTexOffset");
            if (texScaleLoc >= 0) glUniform2f(texScaleLoc, 1.0f, 1.0f);
            if (texOffsetLoc >= 0) glUniform2f(texOffsetLoc, 0.0f, 0.0f);
            
            // MVP matrix
            GLint mvpLoc = glGetUniformLocation(tileProgram, "uMVP");
            if (mvpLoc >= 0) glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        }
        
        // Draw all rockmeshes
        rockMeshManager_->Render();
        
        // Restore state
        if (cullWasEnabled) {
            glEnable(GL_CULL_FACE);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    
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
    debugStats_.fetchAuthFails = schedulerStats.fetchAuthFails;
    debugStats_.fetchRateLimited = schedulerStats.fetchRateLimited;
    debugStats_.fetchHttpErrors = schedulerStats.fetchHttpErrors;
    debugStats_.renderableLeaves = drawStats.renderableLeaves;
    debugStats_.crossfadingLeaves = drawStats.crossfadingLeaves;
    debugStats_.fallbackTiles = drawStats.fallbackTiles;
    debugStats_.placeholderTiles = drawStats.placeholderTiles;
    debugStats_.leafNoMesh = drawStats.leafNoMesh;
    debugStats_.leafNoTexture = drawStats.leafNoTexture;
    debugStats_.leafNoTerrain = drawStats.leafNoTerrain;
    debugStats_.renderQuorumDowngrades = renderQuorumDowngrades_;
    debugStats_.renderQuorumNoMesh = renderQuorumNoMesh_;
    debugStats_.renderQuorumNoTexture = renderQuorumNoTexture_;
    debugStats_.renderQuorumNoTerrain = renderQuorumNoTerrain_;
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
    debugStats_.demCoEvictions = demCoEvictions_;
    debugStats_.heightmapCacheSize = heightmapManager_ ? heightmapManager_->GetCacheSize() : 0;
    debugStats_.heightmapPendingUploads = heightmapManager_ ? heightmapManager_->GetPendingCount() : 0;
    debugStats_.heightmapMissingLeaves = 0;
    if (sceneSnapshot_.useHeightmap && heightmapManager_) {
        for (const TileKey& key : sceneSnapshot_.leafSet) {
            TileKey probe = key;
            bool found = false;
            while (true) {
                if (heightmapManager_->HasTexture(probe)) {
                    found = true;
                    break;
                }
                if (probe.level == 0) {
                    break;
                }
                probe = probe.Parent();
            }
            if (!found) {
                // Only count as "missing" if DEM data IS available (exact or ancestor)
                // but the heightmap texture wasn't uploaded. If DEM data is genuinely
                // unavailable (e.g. root tile 0/0/0 with world-spanning bbox rejected
                // by the DEM service), GPU heightmap displacement simply can't be applied.
                bool hasDemAnywhere = demManager_ && demManager_->HasDataOrAncestor(key);
                if (hasDemAnywhere) {
                    ++debugStats_.heightmapMissingLeaves;
                }
            }
        }
    }
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
        
        // NOTE: SampleHeightDetailed() already implements parent fallback (exact tile -> ancestors).
        // Avoid an extra outer loop here because PickGlobe() can be called per-mouse-move during pan.
        auto sampleWithParentFallback = [&](double sLon, double sLat, int startLevel, double& outHeightMeters) {
            DemSampleResult detailed;
            if (demManager_->SampleHeightDetailed(sLon, sLat, startLevel, detailed) && detailed.ok) {
                outHeightMeters = detailed.heightMeters;
                return true;
            }
            return false;
        };

        // Iterative refinement (2 passes for convergence)
        for (int iter = 0; iter < 2; ++iter) {
            double heightMeters = 0.0;
            if (sampleWithParentFallback(lon, lat, sampleLevel, heightMeters)) {
                double heightKm = heightMeters * 0.001 * config_.demHeightScaleBase * config_.demExaggerationFactor;
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
            ImGui::Text("Build: %s %s %s", NATIVE_GLOBE_GIT_SHA, __DATE__, __TIME__);
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
            if (debugStats_.fetchAuthFails > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Raster Auth Fails: %zu", debugStats_.fetchAuthFails);
            } else {
                ImGui::Text("Raster Auth Fails: %zu", debugStats_.fetchAuthFails);
            }
            if (debugStats_.fetchRateLimited > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Raster 429 (RateLimit): %zu", debugStats_.fetchRateLimited);
            } else {
                ImGui::Text("Raster 429 (RateLimit): %zu", debugStats_.fetchRateLimited);
            }
            if (debugStats_.fetchHttpErrors > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Raster HTTP Errors: %zu", debugStats_.fetchHttpErrors);
            } else {
                ImGui::Text("Raster HTTP Errors: %zu", debugStats_.fetchHttpErrors);
            }
            
            // Gap-free telemetry
            ImGui::Text("Renderable: %d", debugStats_.renderableLeaves);
            ImGui::Text("Crossfading: %d", debugStats_.crossfadingLeaves);
            ImGui::Text("Fallback: %d", debugStats_.fallbackTiles);
            ImGui::Text("Quorum Down: %d (M/T/Ter: %d/%d/%d)",
                        debugStats_.renderQuorumDowngrades,
                        debugStats_.renderQuorumNoMesh,
                        debugStats_.renderQuorumNoTexture,
                        debugStats_.renderQuorumNoTerrain);
            ImGui::Text("Leaf Underflow Frames: %llu",
                        static_cast<unsigned long long>(debugStats_.leafUnderflowFrames));
            ImGui::Text("Seam Gap P95/Max: %.2f / %.2f m",
                        debugStats_.seamGapP95M, debugStats_.seamGapMaxM);
            ImGui::Text("Cliff Edge Count: %d", debugStats_.cliffEdgeCount);
            if (demManager_) {
                ImGui::Text("DEM Health: %s", DemHealthStatusToString(demManager_->GetHealthStatus()));
                bool terrainRequired = config_.demEnabled &&
                                       (demManager_->GetHealthStatus() == DemHealthStatus::Healthy);
                ImGui::Text("Terrain Required: %s", terrainRequired ? "yes" : "no");
                ImGui::Text("DEM Co-Evicts: %zu", debugStats_.demCoEvictions);
            }
            if (config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE) {
                ImGui::Text("Heightmap Cache/Pending: %d / %d",
                            debugStats_.heightmapCacheSize,
                            debugStats_.heightmapPendingUploads);
                if (debugStats_.heightmapMissingLeaves > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                       "Heightmap Missing Leaves: %d",
                                       debugStats_.heightmapMissingLeaves);
                } else {
                    ImGui::Text("Heightmap Missing Leaves: %d",
                                debugStats_.heightmapMissingLeaves);
                }
            } else {
                ImGui::Text("DEM Flat/Pending: %d / %d",
                            debugStats_.demFlatLeaves, debugStats_.demPendingLeaves);
                ImGui::Text("Ancestor DEM Ratio: %.1f%%",
                            debugStats_.ancestorDemRatio * 100.0);
            }
            ImGui::Text("Legacy Seam Edges: %d", debugStats_.seamEdgeCount);
            ImGui::Text("Legacy Avg Edge Delta: %.2f m", debugStats_.avgEdgeHeightDeltaM);
            if (config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
                ImGui::Text("Ancestor DEM Tiles: %d", debugStats_.tilesUsingAncestorDem);
            }
            if (debugStats_.placeholderTiles > 0) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Placeholder: %d", debugStats_.placeholderTiles);
            }
            if (debugStats_.leafNoMesh > 0 || debugStats_.leafNoTexture > 0 || debugStats_.leafNoTerrain > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "NoMesh: %d | NoTex: %d", 
                    debugStats_.leafNoMesh, debugStats_.leafNoTexture);
                if (debugStats_.leafNoTerrain > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "NoTerrain: %d",
                                       debugStats_.leafNoTerrain);
                }
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

bool GlobeEngine::QueueMeshBuild(const TileKey& key, bool isVisible) {
    if (!meshScheduler_) return false;
    if (rebuildPending_.count(key)) return false;
    
    auto it = tiles_.find(key);
    if (it == tiles_.end()) return false;
    
    Tile& tile = it->second;
    if (tile.meshPending) return false;

    // P5.3: DEM-aware mesh build coordination.
    // If DEM fetch is already pending, wait up to 500ms before building a flat mesh.
    // Only wait for the INITIAL mesh build (!tile.hasMesh). Once a mesh exists, rebuild
    // immediately so the existing mesh serves as visual fallback. Without this guard,
    // DEM cache arrive/evict cycles reset the wait timer perpetually, deferring 50+ tiles
    // every frame and preventing mesh convergence.
    if (isVisible && !tile.hasMesh &&
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
                return false;  // Defer mesh build while DEM is likely to arrive soon.
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
    return true;
}

int GlobeEngine::ProcessMeshResults() {
    if (!meshScheduler_) return 0;
    
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
            // Accept DEM upgrades even if revision is stale: a mesh with terrain
            // data is always better than a flat placeholder. Without this, the
            // flat→DEM rebuild check increments meshRevision every frame while
            // demUsed stays false, and the completed DEM build gets perpetually
            // discarded as "stale" — causing 50-75 wasted rebuilds/frame.
            const bool isDemUpgrade = !tile.demUsed && result.demUsed;
            if (!isDemUpgrade) {
                continue;  // Truly stale result
            }
        }
        
        TileMeshBuilder::UploadToGPU(tile, result);
        tile.meshBuiltRevision = tile.meshRevision;
        tile.prevEdgeCoarserMask = tile.edgeCoarserMask;
        ++processed;
    }

    return processed;
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
                tile.lastFrameUsed = frameSerial_;
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
                if (tile.state == TileState::Unloaded || tile.state == TileState::Canceled) {
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
// SMOKE TEST - Zoom in/out + terrain pipeline exercise (deterministic, automated)
// =============================================================================

bool GlobeEngine::RunSmokeTest() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                 SMOKE TEST - Zoom + Tiles + Terrain              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";

    std::filesystem::create_directories("smoke");
    std::ofstream report("smoke/smoke_report.txt", std::ios::out | std::ios::trunc);

    auto logLine = [&](const std::string& s) {
        std::cout << s << "\n";
        if (report) {
            report << s << "\n";
        }
    };

    if (!window_) {
        logLine("ERROR: engine not initialized (no window).");
        return false;
    }

    // Keep the test focused on rendering/pipeline rather than UI.
    showDebugPanel_ = false;

    auto stepFrame = [&]() -> bool {
        double currentTime = glfwGetTime();
        double dt = currentTime - lastFrameTime_;
        lastFrameTime_ = currentTime;

        Update(dt, currentTime);
        Render();
        glfwSwapBuffers(window_);
        glfwPollEvents();

        return !glfwWindowShouldClose(window_);
    };

    auto backgroundWorkActive = [&]() -> bool {
        if (flightController_ && flightController_->IsMoving()) {
            return true;
        }
        if (scheduler_) {
            auto s = scheduler_->GetStats();
            if (s.pendingFetches > 0 || s.pendingDecodes > 0 || s.activeFetches > 0 ||
                s.fetchResultQueue > 0 || s.decodeResultQueue > 0) {
                return true;
            }
        }
        if (textureManager_ && textureManager_->GetPendingUploads() > 0) {
            return true;
        }
        if (meshScheduler_ && meshScheduler_->GetPendingCount() > 0) {
            return true;
        }
        if (!rebuildPending_.empty()) {
            return true;
        }
        if (demManager_ && demManager_->GetPendingCount() > 0) {
            return true;
        }
        if (heightmapManager_ && heightmapManager_->GetPendingCount() > 0) {
            return true;
        }
        // Crossfade/morph can remain active even when background queues drain.
        if (debugStats_.crossfadingLeaves > 0) {
            return true;
        }
        return false;
    };

    struct StepMetrics {
        int maxMissingTiles = 0;
        int maxPlaceholderTiles = 0;
        int maxPendingFetches = 0;
        int maxPendingDecodes = 0;
        int maxActiveFetches = 0;
        int maxRenderQuorumDowngrades = 0;
        double maxSeamGapMaxM = 0.0;
        int maxCliffEdgeCount = 0;
        int maxHeightmapMissingLeaves = 0;
        int endPlaceholderTiles = 0;
        int endHeightmapMissingLeaves = 0;
        double endSeamGapMaxM = 0.0;
        int endCliffEdgeCount = 0;
        int endDemPendingLeaves = 0;
        int endDemFlatLeaves = 0;
        uint64_t leafUnderflowDelta = 0;
    };

    auto collectMetricSample = [&](StepMetrics& m, uint64_t leafUnderflowStart) {
        m.maxMissingTiles = std::max(m.maxMissingTiles, debugStats_.missingTiles);
        m.maxPlaceholderTiles = std::max(m.maxPlaceholderTiles, debugStats_.placeholderTiles);
        m.maxPendingFetches = std::max(m.maxPendingFetches, debugStats_.pendingFetches);
        m.maxPendingDecodes = std::max(m.maxPendingDecodes, debugStats_.pendingDecodes);
        m.maxActiveFetches = std::max(m.maxActiveFetches, debugStats_.activeFetches);
        m.maxRenderQuorumDowngrades = std::max(m.maxRenderQuorumDowngrades, debugStats_.renderQuorumDowngrades);
        m.maxSeamGapMaxM = std::max(m.maxSeamGapMaxM, debugStats_.seamGapMaxM);
        m.maxCliffEdgeCount = std::max(m.maxCliffEdgeCount, debugStats_.cliffEdgeCount);
        m.maxHeightmapMissingLeaves = std::max(m.maxHeightmapMissingLeaves, debugStats_.heightmapMissingLeaves);
        m.leafUnderflowDelta = debugStats_.leafUnderflowFrames - leafUnderflowStart;
    };

    auto settleFrames = [&](int maxFrames, StepMetrics& m, uint64_t leafUnderflowStart) -> bool {
        for (int i = 0; i < maxFrames; ++i) {
            if (!stepFrame()) {
                return false;
            }
            collectMetricSample(m, leafUnderflowStart);
            if (i > 30 && !backgroundWorkActive()) {
                break;  // stable enough
            }
        }
        m.endPlaceholderTiles = debugStats_.placeholderTiles;
        m.endHeightmapMissingLeaves = debugStats_.heightmapMissingLeaves;
        m.endSeamGapMaxM = debugStats_.seamGapMaxM;
        m.endCliffEdgeCount = debugStats_.cliffEdgeCount;
        m.endDemPendingLeaves = debugStats_.demPendingLeaves;
        m.endDemFlatLeaves = debugStats_.demFlatLeaves;
        return true;
    };

    auto zoomTicks = [&](int ticks, StepMetrics& m, uint64_t leafUnderflowStart) -> bool {
        if (!flightController_) return false;
        const double centerX = config_.windowWidth * 0.5;
        const double centerY = config_.windowHeight * 0.5;
        // Ensure scroll zoom has a stable cursor target.
        flightController_->OnMouseMove(centerX, centerY, glfwGetTime());

        int sign = (ticks >= 0) ? 1 : -1;
        int count = std::abs(ticks);
        for (int i = 0; i < count; ++i) {
            flightController_->OnScroll(0.0, static_cast<double>(sign));
            if (!stepFrame()) {
                return false;
            }
            collectMetricSample(m, leafUnderflowStart);
        }
        return true;
    };

    auto saveStepScreenshot = [&](const std::string& stem) {
        SaveScreenshot("smoke/" + stem + ".ppm");
    };

    auto runScenario = [&](const char* scenarioName) -> bool {
        logLine(std::string("\n== Scenario: ") + scenarioName + " ==");

        // Start from a known location.
        LookAt(41.015, 28.98, 4000000.0);
        uint64_t leafUnderflowStart = leafUnderflowFrames_;

        StepMetrics metrics;
        if (!settleFrames(120, metrics, leafUnderflowStart)) return false;
        saveStepScreenshot(std::string(scenarioName) + "_start");

        // Zoom in gradually (exercise child quorum + streaming refinement).
        if (!zoomTicks(+6, metrics, leafUnderflowStart)) return false;
        if (!settleFrames(240, metrics, leafUnderflowStart)) return false;
        saveStepScreenshot(std::string(scenarioName) + "_zoom_in_1");

        // Pan to a nearby location to force new tile requests.
        FlyTo(39.93, 32.86, 1600000.0, 0.0, 0.0, 0.6);
        if (!settleFrames(240, metrics, leafUnderflowStart)) return false;
        saveStepScreenshot(std::string(scenarioName) + "_pan");

        // Zoom in further, then zoom out faster (stress leaf hold + fallback).
        if (!zoomTicks(+6, metrics, leafUnderflowStart)) return false;
        if (!settleFrames(240, metrics, leafUnderflowStart)) return false;
        saveStepScreenshot(std::string(scenarioName) + "_zoom_in_2");

        if (!zoomTicks(-12, metrics, leafUnderflowStart)) return false;
        if (!settleFrames(240, metrics, leafUnderflowStart)) return false;
        saveStepScreenshot(std::string(scenarioName) + "_zoom_out_fast");

        // Summarize
        double lat = 0.0, lon = 0.0, alt = 0.0;
        GetCameraLatLonAlt(lat, lon, alt);
        logLine("End camera: lat=" + std::to_string(lat) +
                " lon=" + std::to_string(lon) +
                " alt=" + std::to_string(alt) + "m" +
                " zoom=" + std::to_string(GetCurrentZoom()));

        logLine("Metrics: missing(max)=" + std::to_string(metrics.maxMissingTiles) +
                " placeholder(max)=" + std::to_string(metrics.maxPlaceholderTiles) +
                " fetch(p/d/a max)=" + std::to_string(metrics.maxPendingFetches) + "/" +
                std::to_string(metrics.maxPendingDecodes) + "/" +
                std::to_string(metrics.maxActiveFetches) +
                " quorumDown(max)=" + std::to_string(metrics.maxRenderQuorumDowngrades) +
                " seamGapMax(max/end)=" + std::to_string(metrics.maxSeamGapMaxM) + "/" +
                std::to_string(metrics.endSeamGapMaxM) + "m" +
                " cliffs(max/end)=" + std::to_string(metrics.maxCliffEdgeCount) + "/" +
                std::to_string(metrics.endCliffEdgeCount) +
                " placeholder(end)=" + std::to_string(metrics.endPlaceholderTiles) +
                " hmMissing(max/end)=" + std::to_string(metrics.maxHeightmapMissingLeaves) + "/" +
                std::to_string(metrics.endHeightmapMissingLeaves) +
                " demFlat(end)=" + std::to_string(metrics.endDemFlatLeaves) +
                " demPending(end)=" + std::to_string(metrics.endDemPendingLeaves) +
                " leafUnderflow(delta)=" + std::to_string(metrics.leafUnderflowDelta));

        // Pass/fail gates: keep them strict on true gaps, softer elsewhere (printed for iteration).
        bool ok = true;
        if (metrics.maxMissingTiles > 0) {
            ok = false;
            logLine("FAIL: missingTiles > 0 (true gaps detected).");
        }
        if (metrics.endPlaceholderTiles > 0) {
            ok = false;
            logLine("FAIL: placeholderTiles > 0 at end (imagery incomplete/unavailable).");
        }
        if (config_.demEnabled && demManager_ && demManager_->GetHealthStatus() == DemHealthStatus::Healthy &&
            config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE) {
            if (metrics.endSeamGapMaxM > 50.0) {
                ok = false;
                logLine("FAIL: seamGapMax exceeded 50m (terrain continuity).");
            }
        }
        if (config_.demEnabled && demManager_ && demManager_->GetHealthStatus() == DemHealthStatus::Healthy &&
            config_.terrainDisplacementMode == DisplacementMode::GPU_HEIGHTMAP_DISPLACE) {
            if (metrics.endHeightmapMissingLeaves > 0) {
                ok = false;
                logLine("FAIL: heightmapMissingLeaves > 0 at end (GPU terrain incomplete).");
            }
        }

        return ok;
    };

    bool overallOk = true;

    // Run for current mode; if DEM is healthy, also toggle and run the other terrain authority.
    std::vector<DisplacementMode> modes;
    modes.push_back(config_.terrainDisplacementMode);
    if (config_.demEnabled && demManager_ && demManager_->GetHealthStatus() == DemHealthStatus::Healthy) {
        DisplacementMode other =
            (config_.terrainDisplacementMode == DisplacementMode::CPU_MESH_BAKE)
                ? DisplacementMode::GPU_HEIGHTMAP_DISPLACE
                : DisplacementMode::CPU_MESH_BAKE;
        modes.push_back(other);
    }

    for (DisplacementMode mode : modes) {
        config_.terrainDisplacementMode = mode;
        const char* name = (mode == DisplacementMode::CPU_MESH_BAKE) ? "CPU_MESH_BAKE" : "GPU_HEIGHTMAP";
        logLine(std::string("\n-- Terrain mode: ") + name + " --");

        // Give Update() a few frames to process mode switch invalidation.
        for (int i = 0; i < 10; ++i) {
            if (!stepFrame()) return false;
        }

        bool ok = runScenario(name);
        overallOk = overallOk && ok;
    }

    logLine(std::string("\nSMOKE TEST RESULT: ") + (overallOk ? "PASS" : "FAIL"));
    logLine("Report: smoke/smoke_report.txt");
    logLine("Screenshots: smoke/*.ppm");
    return overallOk;
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
        std::cout << "     SeamGap P95/Max: " << debugStats_.seamGapP95M << " / "
                  << debugStats_.seamGapMaxM << " m"
                  << " | CliffEdges: " << debugStats_.cliffEdgeCount
                  << " | QuorumDown: " << debugStats_.renderQuorumDowngrades
                  << " (NoMesh/NoTex/NoTer: " << debugStats_.renderQuorumNoMesh
                  << "/" << debugStats_.renderQuorumNoTexture
                  << "/" << debugStats_.renderQuorumNoTerrain << ")\n";

        if (debugStats_.seamGapMaxM > 50.0) {
            float worstGap = 0.0f;
            TileKey worstKey;
            bool found = false;
            for (const TileKey& key : sceneSnapshot_.leafSet) {
                auto it = tiles_.find(key);
                if (it == tiles_.end()) continue;
                const Tile& tile = it->second;
                if (tile.edgeGapMaxM > worstGap) {
                    worstGap = tile.edgeGapMaxM;
                    worstKey = key;
                    found = true;
                }
            }
            if (found) {
                const Tile& t = tiles_.at(worstKey);
                const uint32_t pack = t.demEdgeLevelPack;
                int n = static_cast<int>(pack & 0xFFu);
                int e = static_cast<int>((pack >> 8) & 0xFFu);
                int s = static_cast<int>((pack >> 16) & 0xFFu);
                int w = static_cast<int>((pack >> 24) & 0xFFu);
                std::cout << "     Worst seam tile: z=" << worstKey.level
                          << " x=" << worstKey.x << " y=" << worstKey.y
                          << " gapMax=" << worstGap << "m"
                          << " edgeGap(N/E/S/W)="
                          << t.edgeGapM.x << "/" << t.edgeGapM.y << "/"
                          << t.edgeGapM.z << "/" << t.edgeGapM.w << "m"
                          << " demUsed=" << (t.demUsed ? "yes" : "no")
                          << " demPending=" << (t.demPending ? "yes" : "no")
                          << " demTarget=" << static_cast<int>(t.demTargetLevel)
                          << " demEff=" << static_cast<int>(t.demEffectiveLevel)
                          << " edgeDemLvls(N/E/S/W)=" << n << "/" << e << "/" << s << "/" << w
                          << " seamMask=0x" << std::hex << static_cast<int>(t.seamGapMask) << std::dec
                          << " stitchMask=0x" << std::hex << static_cast<int>(t.stitchMask) << std::dec
                          << " edgeCoarserMask=0x" << std::hex << static_cast<int>(t.edgeCoarserMask) << std::dec
                          << " skirtMask=0x" << std::hex << static_cast<int>(t.skirtMask) << std::dec
                          << "\n";
            }
        }
    }
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║ Visual LOD Test Complete! Screenshots saved in ./screenshots/   ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
}

// =============================================================================
// PAN PROFILE - Measure per-frame timing breakdown at zoom 6-7 during pan
// =============================================================================
void GlobeEngine::RunPanProfile() {
    if (!window_) return;
    showDebugPanel_ = false;

    auto stepFrame = [&]() -> bool {
        double currentTime = glfwGetTime();
        double dt = currentTime - lastFrameTime_;
        lastFrameTime_ = currentTime;
        Update(dt, currentTime);
        Render();
        glfwSwapBuffers(window_);
        glfwPollEvents();
        return !glfwWindowShouldClose(window_);
    };

    // Header
    std::cerr << "frame,zoom,leaves,tiles,lod_ms,req_ms,sched_ms,texUp_ms,"
              << "demUp_ms,edgeMask_ms,mesh_ms,render_ms,total_ms,"
              << "meshRebuilds,pendFetch,pendDecode,rebuildPending,"
              << "triangles,maxLeafLevel,avgLeafSeg,minLeafSeg,maxLeafSeg\n";

    auto logFrame = [&](int frame) {
        int minSeg = std::numeric_limits<int>::max();
        int maxSeg = 0;
        int segSamples = 0;
        double segSum = 0.0;
        for (const TileKey& key : renderLeafSet_) {
            auto it = tiles_.find(key);
            if (it == tiles_.end()) continue;
            const int seg = it->second.builtSegments;
            if (seg <= 0) continue;
            minSeg = std::min(minSeg, seg);
            maxSeg = std::max(maxSeg, seg);
            segSum += static_cast<double>(seg);
            ++segSamples;
        }
        if (segSamples == 0) {
            minSeg = 0;
        }
        const double avgSeg = segSamples > 0 ? (segSum / static_cast<double>(segSamples)) : 0.0;

        std::cerr << frame
            << "," << GetCurrentZoom()
            << "," << renderLeafSet_.size()
            << "," << tiles_.size()
            << "," << frameTimings_.lodSelectMs
            << "," << frameTimings_.requestLoopMs
            << "," << frameTimings_.schedulerUpdateMs
            << "," << frameTimings_.textureUploadMs
            << "," << frameTimings_.demUpdateMs
            << "," << frameTimings_.edgeMaskMs
            << "," << frameTimings_.meshBuildMs
            << "," << frameTimings_.renderMs
            << "," << frameTimings_.totalMs
            << "," << frameTimings_.meshRebuildsQueued
            << "," << debugStats_.pendingFetches
            << "," << debugStats_.pendingDecodes
            << "," << rebuildPending_.size()
            << "," << debugStats_.trianglesRendered
            << "," << debugStats_.maxLeafLevel
            << "," << avgSeg
            << "," << minSeg
            << "," << maxSeg
            << "\n";
    };

    // Phase 1: Zoom 3 baseline (settle)
    std::cerr << "# Phase 1: Zoom 3 baseline\n";
    LookAt(41.015, 28.98, 4000000.0);
    for (int i = 0; i < 60; ++i) { if (!stepFrame()) return; }
    for (int i = 0; i < 30; ++i) { if (!stepFrame()) return; logFrame(i); }

    // Phase 2: Zoom 6 settle + pan
    std::cerr << "# Phase 2: Zoom 6 settle + pan\n";
    LookAt(41.015, 28.98, 500000.0);  // ~zoom 6
    for (int i = 0; i < 120; ++i) { if (!stepFrame()) return; }
    for (int i = 0; i < 30; ++i) { if (!stepFrame()) return; logFrame(100 + i); }

    // Pan at zoom 6
    std::cerr << "# Phase 3: Pan at zoom 6\n";
    for (int i = 0; i < 60; ++i) {
        double lon = 28.98 + i * 0.05;  // ~3 degrees pan
        FlyTo(41.015, lon, 500000.0, 0.0, 0.0, 0.02);
        if (!stepFrame()) return;
        logFrame(200 + i);
    }

    // Phase 4: Zoom 7 settle + pan
    std::cerr << "# Phase 4: Zoom 7 settle + pan\n";
    LookAt(41.015, 28.98, 250000.0);  // ~zoom 7
    for (int i = 0; i < 120; ++i) { if (!stepFrame()) return; }
    for (int i = 0; i < 30; ++i) { if (!stepFrame()) return; logFrame(300 + i); }

    // Pan at zoom 7
    std::cerr << "# Phase 5: Pan at zoom 7\n";
    for (int i = 0; i < 60; ++i) {
        double lon = 28.98 + i * 0.03;  // ~1.8 degrees pan
        FlyTo(41.015, lon, 250000.0, 0.0, 0.0, 0.02);
        if (!stepFrame()) return;
        logFrame(400 + i);
    }

    // Phase 6: Zoom 8 settle + pan
    std::cerr << "# Phase 6: Zoom 8 settle + pan\n";
    LookAt(41.015, 28.98, 100000.0);  // ~zoom 8
    for (int i = 0; i < 120; ++i) { if (!stepFrame()) return; }
    for (int i = 0; i < 30; ++i) { if (!stepFrame()) return; logFrame(500 + i); }

    // Pan at zoom 8
    std::cerr << "# Phase 7: Pan at zoom 8\n";
    for (int i = 0; i < 60; ++i) {
        double lon = 28.98 + i * 0.015;
        FlyTo(41.015, lon, 100000.0, 0.0, 0.0, 0.02);
        if (!stepFrame()) return;
        logFrame(600 + i);
    }

    std::cerr << "# Profile complete\n";
}

} // namespace globe
