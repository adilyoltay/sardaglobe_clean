#include <cstring>
#include <cctype>
#include <string>
#include <iostream>
#include <algorithm>

#include "globe_api.h"

int main(int argc, char** argv) {
  GlobeConfig config;
  config.tileUrl = "https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/{z}/{x}/{y}";
  bool runLodTest = false;
  bool runDemTest = false;
  bool run2DClampTest = false;
  bool runParityTest = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--tile-url") == 0 && i + 1 < argc) {
      config.tileUrl = argv[++i];
    } else if (std::strcmp(argv[i], "--vector-url") == 0 && i + 1 < argc) {
      config.vectorTileUrl = argv[++i];
    } else if (std::strcmp(argv[i], "--vector-layer") == 0 && i + 1 < argc) {
      config.vectorLayerName = argv[++i];
    } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
      config.cacheDir = argv[++i];
    } else if (std::strcmp(argv[i], "--no-cache") == 0) {
      config.useDiskCache = false;
    } else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
      config.fixedZoom = std::atoi(argv[++i]);
      config.useFixedZoom = true;
    } else if (std::strcmp(argv[i], "--min-zoom") == 0 && i + 1 < argc) {
      config.minZoom = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--max-zoom") == 0 && i + 1 < argc) {
      config.maxZoom = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--tile-radius") == 0 && i + 1 < argc) {
      config.tileRadius = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--segments") == 0 && i + 1 < argc) {
      config.segments = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--mesh-type") == 0 && i + 1 < argc) {
      std::string type = argv[++i];
      for (auto& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (type == "xyz" || type == "xyz_mercator") {
        config.meshType = MeshType::XYZ_MERCATOR;
      } else {
        config.meshType = MeshType::WGS84;
      }
    } else if (std::strcmp(argv[i], "--mesh-url") == 0 && i + 1 < argc) {
      std::string urls = argv[++i];
      size_t start = 0;
      while (start <= urls.size()) {
        size_t comma = urls.find(',', start);
        std::string part = (comma == std::string::npos)
                               ? urls.substr(start)
                               : urls.substr(start, comma - start);
        if (!part.empty()) {
          config.meshUrls.push_back(part);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      if (config.meshUrls.empty()) {
        config.meshUrl = urls;
      }
    } else if (std::strcmp(argv[i], "--mesh-cache-size") == 0 && i + 1 < argc) {
      config.meshCacheSize = static_cast<size_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--mesh-retry") == 0 && i + 1 < argc) {
      config.meshRetryAtTimeout = std::atoi(argv[++i]) != 0;
    } else if (std::strcmp(argv[i], "--mesh-continue-division") == 0 && i + 1 < argc) {
      config.meshContinueDivision = std::atoi(argv[++i]) != 0;
    } else if (std::strcmp(argv[i], "--dem-batch-grid") == 0 && i + 1 < argc) {
      config.demBatchGrid = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--dem-mesh-n") == 0 && i + 1 < argc) {
      config.demMeshN = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--wgs84-max-lod") == 0 && i + 1 < argc) {
      config.wgs84MaxLOD = std::max(0, std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--test-lod") == 0) {
      runLodTest = true;
    } else if (std::strcmp(argv[i], "--test-dem") == 0) {
      runDemTest = true;
    } else if (std::strcmp(argv[i], "--debug-dem") == 0) {
      config.demDebug = true;
    } else if (std::strcmp(argv[i], "--test-2d-clamp") == 0) {
      run2DClampTest = true;
    } else if (std::strcmp(argv[i], "--test-parity") == 0) {
      runParityTest = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::cout << "Usage: native_globe [options]\n"
                << "Options:\n"
                << "  --tile-url URL       Tile server URL template\n"
                << "  --vector-url URL     Vector tile URL template\n"
                << "  --vector-layer NAME  Vector layer name\n"
                << "  --cache-dir DIR      Tile cache directory\n"
                << "  --no-cache           Disable disk cache\n"
                << "  --zoom N             Fixed zoom level\n"
                << "  --min-zoom N         Minimum zoom level\n"
                << "  --max-zoom N         Maximum zoom level\n"
                << "  --tile-radius N      Tile radius\n"
                << "  --segments N         Sphere segments\n"
                << "  --mesh-type TYPE     Mesh type: wgs84 | xyz_mercator\n"
                << "  --mesh-url URL       Mesh/DEM service base URL\n"
                << "  --mesh-cache-size N  Mesh cache size (cells)\n"
                << "  --mesh-retry 0|1     Retry mesh request on timeout\n"
                << "  --mesh-continue-division 0|1  Divide mesh batch on timeout\n"
                << "  --dem-batch-grid N   DEM batch grid size (1=single, 2=2x2/CN=4)\n"
                << "  --dem-mesh-n N       DEM grid size (default: 5)\n"
                << "  --wgs84-max-lod N    Max LOD for WGS84 DEM tiles (default: 15)\n"
                << "  --test-lod           Run automated LOD tests\n"
                << "  --test-dem           Run automated DEM/mesh height tests\n"
                << "  --debug-dem          Enable verbose DEM network logging\n"
                << "  --test-2d-clamp      Run automated 2D clamp telemetry test\n"
                << "  --test-parity        Run automated Parity Snapshot test (Phase 0)\n"
                << "  --help, -h           Show this help\n"
                << "\nKeyboard:\n"
                << "  W                    Toggle wireframe mode\n";
      return 0;
    }
  }

  GlobeApi api;
  if (!api.Init(config)) {
    return 1;
  }

  if (runLodTest) {
    bool success = api.RunLodTest();
    api.Shutdown();
    return success ? 0 : 1;
  }

  if (runDemTest) {
    bool success = api.RunDemTest();
    api.Shutdown();
    return success ? 0 : 1;
  }

  if (run2DClampTest) {
    bool success = api.Run2DClampTest();
    api.Shutdown();
    return success ? 0 : 1;
  }

  if (runParityTest) {
    bool success = api.RunParityTest();
    api.Shutdown();
    return success ? 0 : 1;
  }

  api.Run();
  api.Shutdown();
  return 0;
}
