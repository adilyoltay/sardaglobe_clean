#include "engine/globe_engine.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

int main(int argc, char** argv) {
    globe::Config config;
    bool runVisualTest = false;
    bool runSmokeTest = false;
    bool runPanProfile = false;
    
    // Default tile URL: open satellite imagery (EOX Sentinel-2 cloudless mosaic).
    // OSM fallback:
    //   https://tile.openstreetmap.org/{z}/{x}/{y}.png
    config.tileUrl = "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2024_3857/default/g/{z}/{y}/{x}.jpg";

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
        } else if (std::strcmp(argv[i], "--dem-provider") == 0 && i + 1 < argc) {
            const char* provider = argv[++i];
            // Strict validation for provider values
            if (std::strcmp(provider, "terrain-rgb") != 0 && 
                std::strcmp(provider, "google-earth") != 0) {
                std::cerr << "Error: Invalid DEM provider '" << provider << "'\n"
                          << "Valid providers: terrain-rgb, google-earth\n";
                return 1;
            }
            config.demProvider = provider;
            config.demEnabled = true;
        } else if (std::strcmp(argv[i], "--dem-format") == 0 && i + 1 < argc) {
            std::cerr << "ERROR: --dem-format is deprecated. Use --dem-provider terrain-rgb|google-earth\n";
            return 1;
        } else if (std::strcmp(argv[i], "--dem-auth") == 0 && i + 1 < argc) {
            config.demAuth = argv[++i];
        } else if (std::strcmp(argv[i], "--dem-max-zoom") == 0 && i + 1 < argc) {
            config.demMaxZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--dem-mesh-n") == 0 && i + 1 < argc) {
            config.demMeshN = std::max(2, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--ge-elevation-endpoint") == 0 && i + 1 < argc) {
            config.geElevationEndpoint = argv[++i];
        } else if (std::strcmp(argv[i], "--ge-mesh-endpoint") == 0 && i + 1 < argc) {
            config.geMeshEndpoint = argv[++i];
        } else if (std::strcmp(argv[i], "--ge-header") == 0 && i + 1 < argc) {
            // Parse K:V format for GE headers
            std::string header = argv[++i];
            size_t colonPos = header.find(':');
            if (colonPos != std::string::npos) {
                std::string key = header.substr(0, colonPos);
                std::string value = header.substr(colonPos + 1);
                // Allowlist check: only specific headers allowed
                if (key == "Authorization" || key == "X-Custom-Auth") {
                    config.geHeaders.push_back({key, value});
                } else {
                    std::cerr << "Warning: Header '" << key << "' not in GE allowlist. Ignored.\n";
                }
            } else {
                std::cerr << "Error: --ge-header format must be 'Key:Value'\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--ge-elevation-type") == 0 && i + 1 < argc) {
            const char* type = argv[++i];
            if (std::strcmp(type, "ellipsoid") == 0) config.geElevationType = 0;
            else if (std::strcmp(type, "terrain") == 0) config.geElevationType = 1;
            else if (std::strcmp(type, "sea_level") == 0) config.geElevationType = 2;
            else {
                std::cerr << "Error: Invalid elevation type '" << type << "'\n"
                          << "Valid types: ellipsoid, terrain, sea_level\n";
                return 1;
            }
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
        } else if (std::strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
            const char* q = argv[++i];
            if (std::strcmp(q, "low") == 0) config.qualityMode = globe::QualityMode::LOW;
            else if (std::strcmp(q, "medium") == 0) config.qualityMode = globe::QualityMode::MEDIUM;
            else if (std::strcmp(q, "high") == 0) config.qualityMode = globe::QualityMode::HIGH;
            else if (std::strcmp(q, "ultra") == 0) config.qualityMode = globe::QualityMode::ULTRA;
            else {
                std::cerr << "Error: Invalid quality mode '" << q << "'\n"
                          << "Valid modes: low, medium, high, ultra\n"
                          << "Default: medium (GE standard quality)\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: native_globe [options]\n"
                      << "Options:\n"
                      << "  --tile-url URL    Tile server URL template\n"
                      << "  --tile-auth U:P   Tile HTTP basic auth (user:password)\n"
                      << "  --dem-url URL     DEM server URL (elevation)\n"
                      << "  --dem-provider P  DEM provider: terrain-rgb | google-earth (default: terrain-rgb)\n"
                      << "  --dem-auth U:P    DEM HTTP basic auth (user:password)\n"
                      << "  --dem-max-zoom N  Max DEM source zoom level (default 15)\n"
                      << "  --dem-mesh-n N    DEM mesh grid size per tile (>=2)\n"
                      << "  --ge-elevation-endpoint URL  Google Earth elevation endpoint\n"
                      << "  --ge-mesh-endpoint URL       Google Earth mesh endpoint (Phase 5)\n"
                      << "  --ge-header K:V              GE custom header (allowlist: Authorization, X-Custom-Auth)\n"
                      << "  --ge-elevation-type TYPE     Elevation type: ellipsoid | terrain | sea_level\n"
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
                      << "  --quality MODE    Render quality: low | medium | high | ultra (default: medium)\n"
                      << "  --help            Show this help\n"
                      << "\nEnvironment:\n"
                      << "  NATIVE_GLOBE_TILE_AUTH  Tile HTTP basic auth (user:password)\n"
                      << "  NATIVE_GLOBE_DEM_AUTH   DEM HTTP basic auth (user:password)\n"
                      << "  NATIVE_GLOBE_GE_TOKEN   Google Earth auth token (for --dem-provider google-earth)\n"
                      ;
            return 0;
        }
    }
    
    // Validate --dem-url is not used with google-earth provider
    if (config.demProvider == "google-earth" && !config.demUrl.empty()) {
        std::cerr << "Error: --dem-url cannot be used with --dem-provider google-earth\n"
                  << "google-earth provider uses its own elevation/mesh endpoints.\n"
                  << "Use --ge-elevation-endpoint and set " << config.geTokenEnv << " env var.\n";
        return 1;
    }
    
    std::cout << "Native Globe - Clean Architecture\n";
    std::cout << "Tile URL: " << config.tileUrl << "\n";
    if (config.demProvider == "google-earth") {
        std::cout << "GE Elevation Endpoint: " << config.geElevationEndpoint << "\n";
        std::cout << "GE Mesh Endpoint: " << (config.geMeshEndpoint.empty() ? "(not set)" : config.geMeshEndpoint) << "\n";
    } else {
        std::cout << "DEM URL: " << (config.demUrl.empty() ? config.demBaseUrl : config.demUrl) << "\n";
    }
    std::cout << "DEM Provider: " << config.demProvider << "\n";
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
