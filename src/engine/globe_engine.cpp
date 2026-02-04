#include "globe_engine.h"
#include "../core/ellipsoid.h"
#include "../math/tile_math.h"
#include "../math/frustum.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <cstring>
#include <filesystem>

// ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

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
    
    // Init DEM manager for terrain elevation
    if (config_.demEnabled) {
        DemManager::Config demConfig;
        demConfig.baseUrl = config_.demBaseUrl;
        demConfig.meshN = config_.demMeshN;
        demConfig.cacheSize = config_.demCacheSize;
        demConfig.debug = config_.demDebug;
        demManager_ = std::make_unique<DemManager>(demConfig);
    }
    
    // Set scheduler upload callback
    scheduler_->SetUploadCallback([this](Tile& tile) {
        textureManager_->QueueUpload(tile);
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
        Render();
        glfwSwapBuffers(window_);
    }
}

void GlobeEngine::Shutdown() {
    ShutdownImGui();
    
    if (demManager_) demManager_->Shutdown();
    scheduler_.reset();
    textureManager_.reset();
    shaderManager_.reset();
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
        if (tile.ebo != 0) glDeleteBuffers(1, &tile.ebo);
    }
    tiles_.clear();
    
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
    // Update flight controller (handles momentum, animations)
    flightController_->Update(dt, currentTime);
    
    // Get camera position (in km from EarthCamera)
    glm::dvec3 cameraPosD = camera_->GetPositionECEF();
    glm::vec3 cameraPos = glm::vec3(cameraPosD);
    
    // Dynamic near/far planes based on altitude (Google Earth style)
    // This prevents z-fighting at both close and far distances
    double cameraDistKm = glm::length(cameraPosD);
    double altitudeKm = cameraDistKm - EARTH_RADIUS_KM;
    
    // Near plane: 1% of altitude, minimum 0.001 km (1 meter)
    // Far plane: enough to see the Earth + some margin
    double nearPlane = std::max(0.001, altitudeKm * 0.01);
    double farPlane = cameraDistKm + EARTH_RADIUS_KM * 2.0;
    camera_->SetNearFar(nearPlane, farPlane);
    
    // Get camera matrices
    glm::dmat4 view = camera_->GetViewMatrix();
    glm::dmat4 proj = camera_->GetProjectionMatrix();
    glm::dmat4 mvpD = proj * view;
    glm::mat4 mvp = glm::mat4(mvpD);
    
    // LOD selection
    LodSelector::Settings lodSettings;
    lodSettings.minZoom = config_.minZoom;
    lodSettings.maxZoom = config_.maxZoom;
    lodSettings.sseThreshold = config_.sseThreshold;
    
    // TiltFactor: reduce detail when camera is tilted toward horizon
    // tilt=0 (looking down) → tiltFactor=1.0 (full detail)
    // tilt=90 (looking at horizon) → tiltFactor=0.0 (reduced detail)
    double tilt = camera_->GetTilt();  // degrees, 0=looking down, 90=looking at horizon
    lodSettings.tiltFactor = static_cast<float>(1.0 - std::clamp(tilt / 90.0, 0.0, 1.0));
    
    auto isReady = [this](const TileKey& key) -> bool {
        auto it = tiles_.find(key);
        return it != tiles_.end() && it->second.IsReady();
    };
    
    // CRITICAL: Pass FOV directly from camera, not extracted from MVP
    float fovDegrees = static_cast<float>(camera_->GetFov());
    
    LodSelection selection = lodSelector_.Select(
        cameraPos, mvp, fovDegrees,
        config_.windowWidth, config_.windowHeight,
        isReady, lodSettings
    );
    
    // Store leafSet for render filtering
    currentLeafSet_ = selection.leafSet;
    
    // Request required tiles
    for (const TileKey& key : selection.required) {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            // Create tile entry
            Tile tile(key);
            tile.center = TileCenterWorld(key);
            tile.boundingRadius = TileBoundingRadius(key);
            tile.angularRadius = TileAngularRadius(key);
            tiles_.emplace(key, std::move(tile));
            it = tiles_.find(key);
        }
        
        Tile& tile = it->second;
        tile.lastAccessTime = glfwGetTime();
        tile.accessCount++;
        
        if (tile.state == TileState::Unloaded) {
            bool isLeaf = selection.leafSet.count(key) > 0;
            Priority priority = isLeaf ? Priority::Urgent : Priority::Normal;
            scheduler_->Request(key, priority);
            tile.state = TileState::Scheduled;
        }
        else if (tile.state == TileState::Failed) {
            // Exponential backoff for failed tiles (prevents hammering failed servers)
            // Backoff: 1s, 2s, 4s, 8s, 16s, max 32s
            double backoffSeconds = std::min(32.0, std::pow(2.0, tile.retryCount));
            double timeSinceLastRetry = glfwGetTime() - tile.lastRetryTime;
            
            if (timeSinceLastRetry >= backoffSeconds && tile.retryCount < 5) {
                bool isLeaf = selection.leafSet.count(key) > 0;
                Priority priority = isLeaf ? Priority::Urgent : Priority::Normal;
                scheduler_->Request(key, priority);
                tile.state = TileState::Scheduled;
            }
        }
    }
    
    // Prefetch tiles for smooth navigation (low priority)
    // Limit prefetch to avoid overwhelming the network
    int prefetchCount = 0;
    const int maxPrefetch = 8;
    for (const TileKey& key : selection.prefetch) {
        if (prefetchCount >= maxPrefetch) break;
        
        auto it = tiles_.find(key);
        if (it == tiles_.end()) {
            // Create tile entry for prefetch
            Tile tile(key);
            tile.center = TileCenterWorld(key);
            tile.boundingRadius = TileBoundingRadius(key);
            tile.angularRadius = TileAngularRadius(key);
            tiles_.emplace(key, std::move(tile));
            it = tiles_.find(key);
        }
        
        Tile& tile = it->second;
        if (tile.state == TileState::Unloaded) {
            scheduler_->Request(key, Priority::Low);  // Low priority prefetch
            tile.state = TileState::Scheduled;
            ++prefetchCount;
        }
    }
    
    // Process scheduler (fetch/decode results)
    scheduler_->Update(tiles_, glfwGetTime());
    
    // Process texture uploads (time-budgeted)
    textureManager_->ProcessUploads(tiles_, config_.uploadBudgetMs);
    
    // Request DEM data for visible tiles
    if (demManager_) {
        for (const TileKey& key : selection.leaves) {
            demManager_->Request(key);
        }
        demManager_->Update();
    }
    
    // Compute edge coarser mask for seam fix (FAZ 6.1)
    // An edge is "coarser" if the neighbor at same level is NOT a leaf but its parent IS
    for (const TileKey& key : selection.leaves) {
        auto it = tiles_.find(key);
        if (it == tiles_.end()) continue;
        
        uint8_t newMask = 0;
        if (key.level > 0) {  // Level 0 has no coarser neighbors
            // Check 4 cardinal directions: N(0,-1), E(1,0), S(0,1), W(-1,0)
            static const int dx[] = {0, 1, 0, -1};
            static const int dy[] = {-1, 0, 1, 0};
            static const uint8_t edgeBits[] = {Tile::EDGE_NORTH, Tile::EDGE_EAST, 
                                               Tile::EDGE_SOUTH, Tile::EDGE_WEST};
            
            for (int dir = 0; dir < 4; ++dir) {
                TileKey neighborSame = key.Neighbor(dx[dir], dy[dir]);
                if (!neighborSame.IsValid()) continue;
                
                // Neighbor is coarser if: neighborSame NOT in leafSet AND neighborSame.Parent() IS in leafSet
                bool neighborSameIsLeaf = selection.leafSet.count(neighborSame) > 0;
                if (!neighborSameIsLeaf) {
                    TileKey neighborParent = neighborSame.Parent();
                    bool neighborParentIsLeaf = selection.leafSet.count(neighborParent) > 0;
                    if (neighborParentIsLeaf) {
                        newMask |= edgeBits[dir];
                    }
                }
            }
        }
        
        it->second.edgeCoarserMask = newMask;
    }
    
    // Build meshes for ready tiles
    for (const TileKey& key : selection.leaves) {
        auto it = tiles_.find(key);
        if (it != tiles_.end() && it->second.IsReady()) {
            bool needsRebuild = !it->second.hasMesh;
            
            // Rebuild mesh if DEM data became available
            if (demManager_ && it->second.demPending && demManager_->HasData(key)) {
                needsRebuild = true;
            }
            
            // Rebuild mesh if edge coarser mask changed (seam fix FAZ 6.1)
            if (it->second.edgeCoarserMask != it->second.prevEdgeCoarserMask) {
                needsRebuild = true;
            }
            
            if (needsRebuild) {
                BuildTileMesh(it->second);
                // Update prevEdgeCoarserMask AFTER successful rebuild
                // Always update if hasMesh; DEM arrival triggers rebuild via demPending path
                if (it->second.hasMesh) {
                    it->second.prevEdgeCoarserMask = it->second.edgeCoarserMask;
                }
            }
        }
    }
    
    // Evict old tiles
    textureManager_->EvictIfNeeded(tiles_, config_.maxTiles);
}

void GlobeEngine::Render() {
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Get MVP from camera (Google Earth parity - double precision internally)
    glm::dmat4 viewD = camera_->GetViewMatrix();
    glm::dmat4 projD = camera_->GetProjectionMatrix();
    glm::mat4 mvp = glm::mat4(projD * viewD);
    
    // Use tile shader
    shaderManager_->UseTileShader();
    glUniformMatrix4fv(shaderManager_->GetMvpLocation(), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1i(shaderManager_->GetTextureLocation(), 0);
    glUniform1f(shaderManager_->GetFadeLocation(), 1.0f);
    
    // Enable polygon offset to reduce z-fighting between tiles
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    
    // Wireframe mode toggle
    if (config_.wireframeMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    
    // Collect tiles to render with fade-in animation (Google Earth style)
    // Only render current leaves (not stale non-leaf tiles)
    double currentTime = glfwGetTime();
    std::vector<std::pair<Tile*, float>> tilesToRender;  // tile + fade alpha
    tilesToRender.reserve(currentLeafSet_.size());
    
    for (const TileKey& key : currentLeafSet_) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            Tile& tile = it->second;
            if (tile.IsReady() && tile.hasMesh && tile.textureId != 0) {
                float alpha = tile.UpdateFade(currentTime);
                tilesToRender.push_back({&tile, alpha});
            }
        }
    }
    
    // Sort by alpha for proper blending (opaque first, then transparent)
    std::sort(tilesToRender.begin(), tilesToRender.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Enable blending for fade-in effect
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Render collected tiles with fade
    for (const auto& [tile, alpha] : tilesToRender) {
        glUniform1f(shaderManager_->GetFadeLocation(), alpha);
        RenderTile(*tile, mvp);
    }
    
    glDisable(GL_BLEND);
    
    // Reset polygon mode
    if (config_.wireframeMode) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    glDisable(GL_POLYGON_OFFSET_FILL);
    
    // Render pivot gizmo (Google Earth style target icon)
    RenderPivot(mvp);
    
    // Update debug stats
    debugStats_.fps = fps_;
    debugStats_.tileCount = static_cast<int>(tiles_.size());
    debugStats_.pendingFetches = scheduler_->GetPendingFetches();
    debugStats_.pendingDecodes = scheduler_->GetPendingDecodes();
    debugStats_.readyTiles = static_cast<int>(tilesToRender.size());
    debugStats_.visibleTiles = static_cast<int>(tilesToRender.size());
    debugStats_.currentZoom = GetCurrentZoom();
    camera_->GetLatLonAlt(debugStats_.latitude, debugStats_.longitude, debugStats_.altitude);
    debugStats_.heading = camera_->GetHeading();
    debugStats_.tilt = camera_->GetTilt();
    
    // Render ImGui debug panel
    RenderDebugPanel();
}

void GlobeEngine::BuildTileMesh(Tile& tile) {
    // Delete old mesh if rebuilding
    if (tile.hasMesh) {
        if (tile.vao != 0) glDeleteVertexArrays(1, &tile.vao);
        if (tile.vbo != 0) glDeleteBuffers(1, &tile.vbo);
        if (tile.ebo != 0) glDeleteBuffers(1, &tile.ebo);
        tile.vao = tile.vbo = tile.ebo = 0;
        tile.hasMesh = false;
    }
    
    // Use more segments for terrain mesh when DEM is enabled
    const int segments = demManager_ ? std::max(config_.meshSegments, 8) : config_.meshSegments;
    const int vertexCount = (segments + 1) * (segments + 1);
    const int indexCount = segments * segments * 6;
    
    std::vector<float> vertices;
    vertices.reserve(vertexCount * 8);  // pos(3) + normal(3) + uv(2)
    
    std::vector<unsigned int> indices;
    indices.reserve(indexCount);
    
    // Use tile's Extent (OpenGlobus integration)
    if (tile.extent.Width() == 0.0) {
        tile.ComputeExtent();
    }
    double lonLeft = tile.extent.West();
    double lonRight = tile.extent.East();
    double latTop = tile.extent.North();
    double latBottom = tile.extent.South();
    
    // WGS84 ellipsoid (km units for camera compatibility)
    const Ellipsoid& ellipsoid = Ellipsoid::WGS84_KM();
    
    // Pre-calculate Mercator Y for top and bottom (for correct UV mapping)
    // Web Mercator tiles are linear in Mercator Y space, NOT in latitude space
    double latTopRad = latTop * M_PI / 180.0;
    double latBottomRad = latBottom * M_PI / 180.0;
    double mercatorYTop = std::log(std::tan(M_PI / 4.0 + latTopRad / 2.0));
    double mercatorYBottom = std::log(std::tan(M_PI / 4.0 + latBottomRad / 2.0));
    
    // Get height sampler if DEM is available
    HeightSampler heightSampler = nullptr;
    if (demManager_) {
        heightSampler = demManager_->GetHeightSampler();
    }
    
    tile.demUsed = false;
    tile.demPending = false;
    
    // Edge coarser mask for seam fix (FAZ 6.1)
    const uint8_t edgeMask = tile.edgeCoarserMask;
    
    for (int iy = 0; iy <= segments; ++iy) {
        float v = static_cast<float>(iy) / segments;
        
        // CRITICAL: Interpolate in Mercator Y space, then convert to latitude
        // This ensures mesh vertices match the Web Mercator tile texture sampling
        double mercatorY = mercatorYTop + (mercatorYBottom - mercatorYTop) * v;
        double latRad = 2.0 * std::atan(std::exp(mercatorY)) - M_PI / 2.0;
        double lat = latRad * 180.0 / M_PI;
        
        // Check if this row is on North or South border
        bool isNorthBorder = (iy == 0);
        bool isSouthBorder = (iy == segments);
        
        for (int ix = 0; ix <= segments; ++ix) {
            float u = static_cast<float>(ix) / segments;
            double lon = lonLeft + (lonRight - lonLeft) * u;
            double lonRad = lon * M_PI / 180.0;
            
            // Check if this column is on West or East border
            bool isWestBorder = (ix == 0);
            bool isEastBorder = (ix == segments);
            
            // Determine sample level for DEM (edge equalization)
            int sampleLevel = tile.key.level;
            if (heightSampler && sampleLevel > 0) {
                // Use coarser level for border vertices with coarser neighbors
                bool useCoarser = false;
                if (isNorthBorder && (edgeMask & Tile::EDGE_NORTH)) useCoarser = true;
                if (isEastBorder && (edgeMask & Tile::EDGE_EAST)) useCoarser = true;
                if (isSouthBorder && (edgeMask & Tile::EDGE_SOUTH)) useCoarser = true;
                if (isWestBorder && (edgeMask & Tile::EDGE_WEST)) useCoarser = true;
                
                if (useCoarser) {
                    sampleLevel = std::max(0, sampleLevel - 1);
                }
            }
            
            // Get elevation from DEM
            double heightKm = 0.0;
            if (heightSampler) {
                double heightMeters = 0.0;
                if (heightSampler(lon, lat, sampleLevel, heightMeters)) {
                    heightKm = heightMeters * 0.001 * config_.demHeightScale;
                    tile.demUsed = true;
                } else {
                    tile.demPending = true;
                }
            }
            
            // Use Ellipsoid for geodetic to cartesian (OpenGlobus style)
            // Note: Ellipsoid uses meters internally, we use km for camera compatibility
            glm::dvec3 pos = ellipsoid.GeodeticToCartesian(lon, lat, heightKm);
            float x = static_cast<float>(pos.x);
            float y = static_cast<float>(pos.y);
            float z = static_cast<float>(pos.z);
            
            // Normal from ellipsoid surface
            glm::dvec3 surfaceNormal = ellipsoid.GetSurfaceNormal(pos);
            glm::vec3 normal = glm::vec3(surfaceNormal);
            
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(normal.x);
            vertices.push_back(normal.y);
            vertices.push_back(normal.z);
            vertices.push_back(u);
            vertices.push_back(1.0f - v);  // Flip V
        }
    }
    
    // Indices for main grid
    for (int iy = 0; iy < segments; ++iy) {
        for (int ix = 0; ix < segments; ++ix) {
            unsigned int tl = iy * (segments + 1) + ix;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + (segments + 1);
            unsigned int br = bl + 1;
            
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }
    
    // ========================================================================
    // SKIRT GENERATION (Google Earth style - hides LOD seams)
    // ========================================================================
    const bool generateSkirts = true;
    if (generateSkirts) {
        const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));
        
        // Calculate skirt depth based on tile size at this zoom level
        double tileArcKm = 40075.0 / (1 << tile.key.level);
        double skirtDepth = std::max(tileArcKm * 0.01, 0.001);  // 1% of tile or min 1m
        skirtDepth = std::min(skirtDepth, tileArcKm * 0.5);     // Max 50% of tile
        
        // Lambda to add a skirt vertex
        auto addSkirtVertex = [&](int mainIdx) {
            float px = vertices[mainIdx * 8 + 0];
            float py = vertices[mainIdx * 8 + 1];
            float pz = vertices[mainIdx * 8 + 2];
            float u = vertices[mainIdx * 8 + 6];
            float v = vertices[mainIdx * 8 + 7];
            
            // Push vertex inward (toward Earth center)
            glm::vec3 pos(px, py, pz);
            glm::vec3 radialDir = glm::normalize(pos);
            glm::vec3 skirtPos = pos - radialDir * static_cast<float>(skirtDepth);
            
            vertices.push_back(skirtPos.x);
            vertices.push_back(skirtPos.y);
            vertices.push_back(skirtPos.z);
            vertices.push_back(radialDir.x);  // Normal
            vertices.push_back(radialDir.y);
            vertices.push_back(radialDir.z);
            vertices.push_back(u);
            vertices.push_back(v);
        };
        
        // Add skirt vertices for all 4 edges
        // North edge (top, iy=0)
        for (int ix = 0; ix <= segments; ++ix) {
            addSkirtVertex(ix);
        }
        unsigned int northSkirtStart = mainVertexCount;
        
        // South edge (bottom, iy=segments)
        for (int ix = 0; ix <= segments; ++ix) {
            addSkirtVertex(segments * (segments + 1) + ix);
        }
        unsigned int southSkirtStart = northSkirtStart + segments + 1;
        
        // West edge (left, ix=0)
        for (int iy = 0; iy <= segments; ++iy) {
            addSkirtVertex(iy * (segments + 1));
        }
        unsigned int westSkirtStart = southSkirtStart + segments + 1;
        
        // East edge (right, ix=segments)
        for (int iy = 0; iy <= segments; ++iy) {
            addSkirtVertex(iy * (segments + 1) + segments);
        }
        unsigned int eastSkirtStart = westSkirtStart + segments + 1;
        
        // Generate skirt triangles
        // North edge skirt
        for (int i = 0; i < segments; ++i) {
            unsigned int v0 = i;
            unsigned int v1 = i + 1;
            unsigned int v2 = northSkirtStart + i;
            unsigned int v3 = northSkirtStart + i + 1;
            indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
            indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
        }
        
        // South edge skirt (reversed winding)
        for (int i = 0; i < segments; ++i) {
            unsigned int v0 = segments * (segments + 1) + i;
            unsigned int v1 = segments * (segments + 1) + i + 1;
            unsigned int v2 = southSkirtStart + i;
            unsigned int v3 = southSkirtStart + i + 1;
            indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
            indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
        }
        
        // West edge skirt (reversed winding)
        for (int j = 0; j < segments; ++j) {
            unsigned int v0 = j * (segments + 1);
            unsigned int v1 = (j + 1) * (segments + 1);
            unsigned int v2 = westSkirtStart + j;
            unsigned int v3 = westSkirtStart + j + 1;
            indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
            indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
        }
        
        // East edge skirt
        for (int j = 0; j < segments; ++j) {
            unsigned int v0 = j * (segments + 1) + segments;
            unsigned int v1 = (j + 1) * (segments + 1) + segments;
            unsigned int v2 = eastSkirtStart + j;
            unsigned int v3 = eastSkirtStart + j + 1;
            indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
            indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
        }
    }
    
    // Create VAO/VBO/EBO
    glGenVertexArrays(1, &tile.vao);
    glGenBuffers(1, &tile.vbo);
    glGenBuffers(1, &tile.ebo);
    
    glBindVertexArray(tile.vao);
    
    glBindBuffer(GL_ARRAY_BUFFER, tile.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), 
                 vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tile.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);
    
    // Position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // TexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
    
    tile.indexCount = static_cast<uint32_t>(indices.size());
    tile.hasMesh = true;
}

void GlobeEngine::RenderTile(const Tile& tile, const glm::mat4& mvp) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tile.textureId);
    
    glBindVertexArray(tile.vao);
    glDrawElements(GL_TRIANGLES, tile.indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Globe picking for navigation (ray-sphere intersection)
bool GlobeEngine::PickGlobe(double screenX, double screenY, glm::dvec3& outPoint) {
    glm::dvec3 rayOrigin, rayDir;
    camera_->GetRay(screenX, screenY, config_.windowWidth, config_.windowHeight, rayOrigin, rayDir);
    
    // Ray-sphere intersection with Earth
    // Camera ECEF is in KM units, use shared constant
    const double R = earth::EARTH_RADIUS_KM;
    glm::dvec3 oc = rayOrigin;  // Origin is camera position, sphere center is at (0,0,0)
    
    double a = glm::dot(rayDir, rayDir);
    double b = 2.0 * glm::dot(oc, rayDir);
    double c = glm::dot(oc, oc) - R * R;
    double discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0) {
        return false;  // No intersection
    }
    
    double t = (-b - std::sqrt(discriminant)) / (2.0 * a);
    if (t < 0.0) {
        t = (-b + std::sqrt(discriminant)) / (2.0 * a);
    }
    if (t < 0.0) {
        return false;  // Behind camera
    }
    
    outPoint = rayOrigin + t * rayDir;  // Result is in KM units
    return true;
}

void GlobeEngine::FlyTo(double lat, double lon, double altMeters, 
                        double heading, double tilt, double duration) {
    flightController_->FlyToLocation(lat, lon, altMeters, heading, tilt, duration);
}

void GlobeEngine::LookAt(double lat, double lon, double altitude) {
    camera_->SetLatLonAlt(lat, lon, altitude);
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
    engine->flightController_->OnMouseMove(xpos, ypos, glfwGetTime());
}

void GlobeEngine::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* engine = static_cast<GlobeEngine*>(glfwGetWindowUserPointer(window));
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
            
            ImGui::Spacing();
            
            // Tiles
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tiles");
            ImGui::Separator();
            ImGui::Text("Total: %d", debugStats_.tileCount);
            ImGui::Text("Visible: %d", debugStats_.visibleTiles);
            ImGui::Text("Ready: %d", debugStats_.readyTiles);
            ImGui::Text("Pending Fetch: %d", debugStats_.pendingFetches);
            ImGui::Text("Pending Decode: %d", debugStats_.pendingDecodes);
            
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
            
            ImGui::Spacing();
            
            // Render Options
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Render Options");
            ImGui::Separator();
            ImGui::Checkbox("Wireframe Mode", &config_.wireframeMode);
            
            ImGui::Spacing();
            
            // Controls help
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Controls");
            ImGui::Separator();
            ImGui::TextWrapped("Left: Pan | Shift+Left: Orbit");
            ImGui::TextWrapped("Scroll: Zoom | Shift+Scroll: Tilt");
            ImGui::TextWrapped("Double-click: FlyTo + Zoom");
        }
        ImGui::End();
    }
    
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
