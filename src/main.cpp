#include "engine/globe_engine.h"
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    globe::Config config;
    
    // Default tile URL (Pirireis HGM Orthofoto)
    config.tileUrl = "https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/{z}/{x}/{y}";
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tile-url") == 0 && i + 1 < argc) {
            config.tileUrl = argv[++i];
        } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            config.cacheDir = argv[++i];
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            config.useDiskCache = false;
        } else if (std::strcmp(argv[i], "--min-zoom") == 0 && i + 1 < argc) {
            config.minZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--max-zoom") == 0 && i + 1 < argc) {
            config.maxZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            config.windowWidth = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            config.windowHeight = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: native_globe [options]\n"
                      << "Options:\n"
                      << "  --tile-url URL    Tile server URL template\n"
                      << "  --cache-dir DIR   Tile cache directory\n"
                      << "  --no-cache        Disable disk cache\n"
                      << "  --min-zoom N      Minimum zoom level\n"
                      << "  --max-zoom N      Maximum zoom level\n"
                      << "  --width N         Window width\n"
                      << "  --height N        Window height\n"
                      << "  --help            Show this help\n";
            return 0;
        }
    }
    
    std::cout << "Native Globe - Clean Architecture\n";
    std::cout << "Tile URL: " << config.tileUrl << "\n";
    
    globe::GlobeEngine engine(config);
    
    if (!engine.Init()) {
        std::cerr << "Failed to initialize engine\n";
        return 1;
    }
    
    engine.Run();
    engine.Shutdown();
    
    return 0;
}
