#include "engine/globe_engine.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
    globe::Config config;
    bool runVisualTest = false;
    bool runSmokeTest = false;
    bool runPanProfile = false;
    
    // Default tile URL (Pirireis HGM Orthofoto)
    config.tileUrl = "https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/{z}/{x}/{y}";

    // Optional basic-auth via environment (avoids leaking credentials into shell history).
    // Format: "user:password"
    if (const char* env = std::getenv("NATIVE_GLOBE_TILE_AUTH")) {
        config.tileAuth = env;
    }
    if (const char* env = std::getenv("NATIVE_GLOBE_DEM_AUTH")) {
        config.demAuth = env;
    }
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tile-url") == 0 && i + 1 < argc) {
            config.tileUrl = argv[++i];
        } else if (std::strcmp(argv[i], "--tile-auth") == 0 && i + 1 < argc) {
            config.tileAuth = argv[++i];
        } else if (std::strcmp(argv[i], "--dem-url") == 0 && i + 1 < argc) {
            config.demUrl = argv[++i];
            config.demEnabled = true;
        } else if (std::strcmp(argv[i], "--dem-auth") == 0 && i + 1 < argc) {
            config.demAuth = argv[++i];
        } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            config.cacheDir = argv[++i];
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            config.useDiskCache = false;
        } else if (std::strcmp(argv[i], "--no-dem") == 0) {
            config.demEnabled = false;
        } else if (std::strcmp(argv[i], "--min-zoom") == 0 && i + 1 < argc) {
            config.minZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--max-zoom") == 0 && i + 1 < argc) {
            config.maxZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--lod-refine-budget") == 0 && i + 1 < argc) {
            config.maxRefinementsPerFrame = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            config.windowWidth = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            config.windowHeight = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--headless") == 0) {
            config.headless = true;
        } else if (std::strcmp(argv[i], "--test") == 0) {
            runVisualTest = true;
        } else if (std::strcmp(argv[i], "--smoke") == 0) {
            runSmokeTest = true;
        } else if (std::strcmp(argv[i], "--demDebug") == 0) {
            config.demDebug = true;
        } else if (std::strcmp(argv[i], "--profile-pan") == 0) {
            runPanProfile = true;
        } else if (std::strcmp(argv[i], "--gpu-terrain") == 0) {
            config.terrainDisplacementMode = globe::DisplacementMode::GPU_HEIGHTMAP_DISPLACE;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: native_globe [options]\n"
                      << "Options:\n"
                      << "  --tile-url URL    Tile server URL template\n"
                      << "  --tile-auth U:P   Tile HTTP basic auth (user:password)\n"
                      << "  --dem-url URL     DEM server URL (elevation)\n"
                      << "  --dem-auth U:P    DEM HTTP basic auth (user:password)\n"
                      << "  --cache-dir DIR   Tile cache directory\n"
                      << "  --no-cache        Disable disk cache\n"
                      << "  --no-dem          Disable DEM\n"
                      << "  --min-zoom N      Minimum zoom level\n"
                      << "  --max-zoom N      Maximum zoom level\n"
                      << "  --lod-refine-budget N  Max parent->child LOD refinements per frame (<=0 unlimited)\n"
                      << "  --width N         Window width\n"
                      << "  --height N        Window height\n"
                      << "  --headless        Create hidden window (useful for automated tests)\n"
                      << "  --test            Run visual LOD test and exit\n"
                      << "  --smoke           Run smoke test (zoom in/out + terrain) and exit\n"
                      << "  --profile-pan     Run zoom/pan profiler and print per-frame CSV\n"
                      << "  --gpu-terrain     Use GPU heightmap displacement (default: CPU mesh bake)\n"
                      << "  --help            Show this help\n"
                      << "\nEnvironment:\n"
                      << "  NATIVE_GLOBE_TILE_AUTH  Tile HTTP basic auth (user:password)\n"
                      << "  NATIVE_GLOBE_DEM_AUTH   DEM HTTP basic auth (user:password)\n"
                      ;
            return 0;
        }
    }
    
    std::cout << "Native Globe - Clean Architecture\n";
    std::cout << "Tile URL: " << config.tileUrl << "\n";
    std::cout << "Tile Auth: " << (config.tileAuth.empty() ? "none" : "basic") << "\n";
    std::cout << "DEM Auth: " << (config.demAuth.empty() ? "none" : "basic") << "\n";
    
    int runModeCount = (runVisualTest ? 1 : 0) + (runSmokeTest ? 1 : 0) + (runPanProfile ? 1 : 0);
    if (runModeCount > 1) {
        std::cerr << "Error: --test, --smoke, and --profile-pan are mutually exclusive\n";
        return 1;
    }

    globe::GlobeEngine engine(config);
    
    if (!engine.Init()) {
        std::cerr << "Failed to initialize engine\n";
        return 1;
    }
    
    if (runVisualTest) {
        engine.RunVisualLodTest();
        engine.Shutdown();
        return 0;
    }

    if (runSmokeTest) {
        bool ok = engine.RunSmokeTest();
        engine.Shutdown();
        return ok ? 0 : 2;
    }

    if (runPanProfile) {
        engine.RunPanProfile();
        engine.Shutdown();
        return 0;
    }
    
    engine.Run();
    engine.Shutdown();
    
    return 0;
}
