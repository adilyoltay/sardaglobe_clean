#pragma once

#include "../core/config.h"
#include "../core/tile.h"
#include "../scheduling/tile_scheduler.h"
#include "../scheduling/lod_selector.h"
#include "../rendering/texture_manager.h"
#include "../rendering/shader_manager.h"
#include "../camera/earth_camera.h"
#include "../camera/flight_controller.h"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

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
    
private:
    // Frame phases (Google Earth style game loop)
    void ProcessInput(double currentTime);
    void Update(double dt, double currentTime);
    void Render();
    
    // Tile management
    void BuildTileMesh(Tile& tile);
    void RenderTile(const Tile& tile, const glm::mat4& mvp);
    
    // Globe picking (for navigation)
    bool PickGlobe(double screenX, double screenY, glm::dvec3& outPoint);
    
    // Input callbacks
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    
    Config config_;
    GLFWwindow* window_ = nullptr;
    
    // Camera system (from backup - Google Earth parity)
    std::unique_ptr<earth::PerspectiveCamera> camera_;
    std::unique_ptr<earth::FlightController> flightController_;
    
    // Subsystems
    std::unique_ptr<TileScheduler> scheduler_;
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<ShaderManager> shaderManager_;
    LodSelector lodSelector_;
    
    // Tiles
    std::unordered_map<TileKey, Tile> tiles_;
    
    // Frame timing
    double lastFrameTime_ = 0.0;
    int frameCount_ = 0;
    float fps_ = 0.0f;
    
    // Debug stats (for ImGui panel)
    struct DebugStats {
        float fps = 0.0f;
        int tileCount = 0;
        int pendingFetches = 0;
        int pendingDecodes = 0;
        int readyTiles = 0;
        int visibleTiles = 0;
        int currentZoom = 0;
        double altitude = 0.0;
        double latitude = 0.0;
        double longitude = 0.0;
        double heading = 0.0;
        double tilt = 0.0;
        size_t textureMemoryMB = 0;
    };
    DebugStats debugStats_;
    bool showDebugPanel_ = true;
    
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
};

} // namespace globe
