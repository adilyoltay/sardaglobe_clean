#include "engine/globe_engine.h"
#include "camera/earth_camera.h"
#include "core/ellipsoid.h"
#include "io/ge_mesh_url_template.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <string>
#include <limits>
#include <cerrno>

// Phase 6.4: Strict numeric parsing helper for CLI flags with overflow protection
// Returns true on success, false on parse failure
template<typename T>
bool ParseNumeric(const char* str, T& out, const char* name) {
    if (!str || str[0] == '\0' || str[0] == '-') {
        // Reject empty strings and flag-like values starting with '-'
        if (str && str[0] == '-') {
            std::cerr << "Error: " << name << " expects a numeric value, got flag '" << str << "'\n";
        } else {
            std::cerr << "Error: " << name << " expects a numeric value\n";
        }
        return false;
    }
    
    // Check for non-numeric characters (except leading + or - for signed types)
    const char* p = str;
    if (*p == '+' || *p == '-') p++;  // Allow sign for signed types
    bool hasDigit = false;
    bool hasDecimal = false;
    
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            hasDigit = true;
        } else if (*p == '.' && !hasDecimal) {
            hasDecimal = true;  // Allow one decimal point for floats
        } else {
            std::cerr << "Error: " << name << " contains invalid character '" << *p << "' in '" << str << "'\n";
            return false;
        }
    }
    
    if (!hasDigit) {
        std::cerr << "Error: " << name << " must contain at least one digit\n";
        return false;
    }
    
    // Parse based on type
    if constexpr (std::is_integral_v<T>) {
        // Phase 6.4: Use strtoll for overflow-safe parsing
        char* endptr = nullptr;
        errno = 0;  // Clear errno before parse
        long long val = std::strtoll(str, &endptr, 10);
        
        // Check for parse errors
        if (endptr && *endptr != '\0') {
            std::cerr << "Error: " << name << " has trailing characters\n";
            return false;
        }
        if (errno == ERANGE) {
            std::cerr << "Error: " << name << " value out of range\n";
            return false;
        }
        
        // Phase 6.4: Check against type limits
        if (val < std::numeric_limits<T>::min() || val > std::numeric_limits<T>::max()) {
            std::cerr << "Error: " << name << " value " << val << " out of range for type\n";
            return false;
        }
        out = static_cast<T>(val);
    } else {
        char* endptr = nullptr;
        errno = 0;
        double val = std::strtod(str, &endptr);
        if (endptr && *endptr != '\0') {
            std::cerr << "Error: " << name << " has trailing characters\n";
            return false;
        }
        if (errno == ERANGE) {
            std::cerr << "Error: " << name << " value out of range\n";
            return false;
        }
        out = static_cast<T>(val);
    }
    return true;
}

std::string RedactSensitiveUrlParams(const std::string& input) {
    std::string masked = input;
    auto redact = [](std::string& value, const std::string& key) {
        size_t pos = value.find(key + "=");
        while (pos != std::string::npos) {
            if (pos == 0 || (value[pos - 1] != '?' && value[pos - 1] != '&')) {
                break;
            }
            size_t valueStart = pos + key.size() + 1;
            size_t valueEnd = value.find('&', valueStart);
            if (valueEnd == std::string::npos) {
                valueEnd = value.size();
            }
            value.replace(valueStart, valueEnd - valueStart, "***");
            pos = value.find(key + "=", valueEnd);
        }
    };
    redact(masked, "access_token");
    redact(masked, "key");
    return masked;
}

// Track A: #8 Precision Baseline Report
// Compares double vs float projection accuracy without GPU/OpenGL
int RunPrecisionReport() {
    std::cout << "=== Precision Baseline Report (Track A: #8) ===\n\n";
    
    // Camera setup
    earth::PerspectiveCamera camera;
    camera.SetFov(60.0);
    camera.SetAspectRatio(1280.0 / 720.0);
    camera.SetRoll(0.0);
    
    // Fixed test point ( away from poles to avoid singularities)
    const double testLat = 40.0;  // Istanbul-ish latitude
    const double testLon = 29.0;  // Istanbul-ish longitude
    
    // Test scenarios: altitude 5000m, 500m, 50m, 5m
    struct Scenario {
        const char* name;
        double altitude;
    };
    Scenario scenarios[] = {
        {"S1: Alt=5000m", 5000.0},
        {"S2: Alt=500m", 500.0},
        {"S3: Alt=50m", 50.0},
        {"S4: Alt=5m", 5.0}
    };
    
    const int windowWidth = 1280;
    const int windowHeight = 720;
    const double tilt = 80.0;      // Near-horizontal view
    const double heading = 45.0;   // Diagonal
    
    // Sample points around center (N/E/S/W at ~100m radius)
    struct SamplePoint {
        const char* name;
        double dLat, dLon;  // Degrees offset (~100m)
    };
    SamplePoint samples[] = {
        {"P0: Center", 0.0, 0.0},
        {"P1: North", 0.0009, 0.0},   // ~100m north
        {"P2: East", 0.0, 0.0011},    // ~100m east
        {"P3: South", -0.0009, 0.0},  // ~100m south
        {"P4: West", 0.0, -0.0011}    // ~100m west
    };
    
    // Use km-scale ellipsoid to match camera's internal units
    const globe::Ellipsoid& ellipsoid = globe::Ellipsoid::WGS84_KM();
    double maxDeltaAllScenarios = 0.0;
    double maxDelta50mOrLess = 0.0;
    
    for (const auto& scenario : scenarios) {
        // Set camera position
        camera.SetLatLonAlt(testLat, testLon, scenario.altitude);
        camera.SetTilt(tilt);
        camera.SetHeading(heading);
        
        // Get matrices (double precision)
        glm::dmat4 projD = camera.GetProjectionMatrix();
        glm::dmat4 viewD = camera.GetViewMatrix();
        glm::dmat4 mvpD = projD * viewD;
        
        // Float simulation (cast to float)
        glm::mat4 mvpF = glm::mat4(mvpD);
        
        double scenarioMaxDelta = 0.0;
        double scenarioSumDelta = 0.0;
        int sampleCount = 0;
        
        for (const auto& sample : samples) {
            // World position (ECEF double)
            double lat = testLat + sample.dLat;
            double lon = testLon + sample.dLon;
            glm::dvec3 posD = ellipsoid.GeodeticToCartesian(lon, lat, 0.0);
            
            // Double precision projection
            glm::dvec4 clipD = mvpD * glm::dvec4(posD, 1.0);
            glm::dvec3 ndcD = glm::dvec3(clipD) / clipD.w;
            
            // Float simulation
            glm::vec4 clipF = mvpF * glm::vec4(glm::vec3(posD), 1.0f);
            glm::vec3 ndcF = glm::vec3(clipF) / clipF.w;
            
            // Pixel delta
            double deltaPxX = std::abs(ndcD.x - static_cast<double>(ndcF.x)) * 0.5 * windowWidth;
            double deltaPxY = std::abs(ndcD.y - static_cast<double>(ndcF.y)) * 0.5 * windowHeight;
            double deltaPx = std::max(deltaPxX, deltaPxY);
            
            scenarioMaxDelta = std::max(scenarioMaxDelta, deltaPx);
            scenarioSumDelta += deltaPx;
            sampleCount++;
        }
        
        double scenarioMeanDelta = scenarioSumDelta / sampleCount;
        maxDeltaAllScenarios = std::max(maxDeltaAllScenarios, scenarioMaxDelta);
        
        if (scenario.altitude <= 50.0) {
            maxDelta50mOrLess = std::max(maxDelta50mOrLess, scenarioMaxDelta);
        }
        
        std::cout << scenario.name << ":\n";
        std::cout << "  maxDeltaPx = " << std::fixed << std::setprecision(4) << scenarioMaxDelta << "\n";
        std::cout << "  meanDeltaPx = " << scenarioMeanDelta << "\n\n";
    }
    
    // Final recommendation
    std::cout << "=== Summary ===\n";
    std::cout << "Overall maxDeltaPx = " << maxDeltaAllScenarios << "\n";
    std::cout << "Max delta at alt<=50m = " << maxDelta50mOrLess << "\n\n";
    
    if (maxDelta50mOrLess > 0.5) {
        std::cout << "RECOMMEND_RTE=YES (float precision insufficient at street level)\n";
        return 1;  // RTE recommended
    } else {
        std::cout << "RECOMMEND_RTE=NO (float precision adequate)\n";
        return 0;  // Float OK
    }
}

int main(int argc, char** argv) {
    globe::Config config;
    bool runVisualTest = false;
    bool runSmokeTest = false;
    bool runPanProfile = false;
    bool runPrecisionReport = false;
    
    // Default tile URL: open satellite imagery (EOX Sentinel-2 cloudless mosaic).
    // OSM fallback:
    //   https://tile.openstreetmap.org/{z}/{x}/{y}.png
    config.tileUrl = "https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/{z}/{x}/{y}";

    // Optional basic-auth via environment (avoids leaking credentials into shell history).
    // Format: "user:password"
    if (const char* env = std::getenv("NATIVE_GLOBE_TILE_AUTH")) {
        config.tileAuth = env;
    }
    if (const char* env = std::getenv("NATIVE_GLOBE_DEM_AUTH")) {
        config.demAuth = env;
    }
    if (const char* env = std::getenv(config.demApiKeyEnv.c_str())) {
        config.demApiKey = env;
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
        } else if (std::strcmp(argv[i], "--dem-api-key") == 0 && i + 1 < argc) {
            config.demApiKey = argv[++i];
        } else if (std::strcmp(argv[i], "--dem-api-key-env") == 0 && i + 1 < argc) {
            config.demApiKeyEnv = argv[++i];
        } else if (std::strcmp(argv[i], "--dem-max-zoom") == 0 && i + 1 < argc) {
            config.demMaxZoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--dem-mesh-n") == 0 && i + 1 < argc) {
            config.demMeshN = std::max(2, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--ge-elevation-endpoint") == 0 && i + 1 < argc) {
            config.geElevationEndpoint = argv[++i];
        } else if (std::strcmp(argv[i], "--ge-elevation-path") == 0 && i + 1 < argc) {
            config.geElevationPath = argv[++i];  // Override {path} placeholder in elevation URL
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
                if (key == "Authorization" || key == "X-Custom-Auth" || key == "X-Client-Data") {
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
        } else if (std::strcmp(argv[i], "--ge-mesh-quadkey") == 0 && i + 1 < argc) {
            const char* qk = argv[++i];
            // Sprint 1 validation: non-empty, only digits 0-7 (NodeData key space)
            if (qk[0] == '\0') {
                std::cerr << "Error: Empty quadkey not allowed\n";
                return 1;
            }
            bool valid = true;
            for (const char* p = qk; *p; ++p) {
                if (*p < '0' || *p > '7') {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                std::cerr << "Error: Invalid quadkey '" << qk << "'\n"
                          << "Sprint 1 NodeData keys use digits 0-7 only\n";
                return 1;
            }
            config.geMeshQuadKeys.push_back(qk);
        } else if (std::strcmp(argv[i], "--ge-mesh-no-flip-v") == 0) {
            config.geMeshFlipV = false;
        } else if (std::strcmp(argv[i], "--ge-mesh-enable-http2") == 0) {
            config.geMeshEnableHttp2 = true;
        } else if (std::strcmp(argv[i], "--ge-mesh-no-http2") == 0) {
            config.geMeshEnableHttp2 = false;
        } else if (std::strcmp(argv[i], "--ge-mesh-http1-fallback") == 0) {
            config.geMeshAllowHttp1Fallback = true;
        } else if (std::strcmp(argv[i], "--ge-mesh-no-http1-fallback") == 0) {
            config.geMeshAllowHttp1Fallback = false;
        } else if (std::strcmp(argv[i], "--ge-mesh-tcp-keepalive") == 0 && i + 1 < argc) {
            long keepalive;
            if (!ParseNumeric(argv[++i], keepalive, "--ge-mesh-tcp-keepalive")) return 1;
            if (keepalive <= 0 || keepalive > 3600) {
                std::cerr << "Error: --ge-mesh-tcp-keepalive must be between 1 and 3600 seconds\n";
                return 1;
            }
            config.geMeshTcpKeepAliveSec = keepalive;
        } else if (std::strcmp(argv[i], "--ge-mesh-child-lod-dist") == 0 && i + 1 < argc) {
            float dist;
            if (!ParseNumeric(argv[++i], dist, "--ge-mesh-child-lod-dist")) return 1;
            if (dist < 0 || dist > 1000000) {
                std::cerr << "Error: --ge-mesh-child-lod-dist must be between 0 and 1000000 meters\n";
                return 1;
            }
            config.geMeshChildLodDistance = dist;
        } else if (std::strcmp(argv[i], "--ge-mesh-max-child-req") == 0 && i + 1 < argc) {
            int maxReq;
            if (!ParseNumeric(argv[++i], maxReq, "--ge-mesh-max-child-req")) return 1;
            if (maxReq < 0 || maxReq > 100) {
                std::cerr << "Error: --ge-mesh-max-child-req must be between 0 and 100\n";
                return 1;
            }
            config.geMeshMaxChildRequestsPerFrame = maxReq;
        } else if (std::strcmp(argv[i], "--ge-epoch") == 0 && i + 1 < argc) {
            config.geEpoch = argv[++i];
            config.geEpochAutoDetect = false;
        } else if (std::strcmp(argv[i], "--ge-rate-limit") == 0 && i + 1 < argc) {
            int rateMs;
            if (!ParseNumeric(argv[++i], rateMs, "--ge-rate-limit")) return 1;
            if (rateMs < 0 || rateMs > 10000) {
                std::cerr << "Error: --ge-rate-limit must be between 0 and 10000 ms\n";
                return 1;
            }
            config.geRateLimitMs = rateMs;
        } else if (std::strcmp(argv[i], "--ge-epoch-auto-detect") == 0) {
            config.geEpochAutoDetect = true;
        } else if (std::strcmp(argv[i], "--no-ge-epoch-auto-detect") == 0) {
            config.geEpochAutoDetect = false;
        } else if (std::strcmp(argv[i], "--ge-no-octree") == 0) {
            config.geOctreeEnabled = false;
        } else if (std::strcmp(argv[i], "--fallback-parent-until-children-ready") == 0) {
            config.fallbackRequireParentUntilChildrenReady = true;
        } else if (std::strcmp(argv[i], "--no-fallback-parent-until-children-ready") == 0) {
            config.fallbackRequireParentUntilChildrenReady = false;
        } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            config.cacheDir = argv[++i];
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            config.useDiskCache = false;
        } else if (std::strcmp(argv[i], "--no-dem") == 0) {
            config.demEnabled = false;
        } else if (std::strcmp(argv[i], "--no-distance-morph") == 0) {
            config.useDistanceBasedTerrainMorph = false;  // P2: Disable distance-based morph
        } else if (std::strcmp(argv[i], "--morph-range") == 0 && i + 1 < argc) {
            // P2: Parse and validate morph range
            char* end = nullptr;
            const char* val = argv[++i];
            double range = std::strtod(val, &end);
            if (end == val || *end != '\0' || range <= 0.0 || !std::isfinite(range)) {
                std::cerr << "ERROR: Invalid morph range '" << val << "'. Must be positive number (km).\n";
                return 1;
            }
            config.terrainMorphDistanceRangeKm = static_cast<float>(range);
        } else if (std::strcmp(argv[i], "--no-morph-fallback") == 0) {
            config.enableTerrainMorphTimeFallback = false;  // P2: Disable time fallback
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
        } else if (std::strcmp(argv[i], "--precision-report") == 0) {
            runPrecisionReport = true;
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
                      << "  --dem-provider P  DEM provider: google-earth | terrain-rgb (default: google-earth)\n"
                      << "  --dem-auth U:P    DEM HTTP basic auth (user:password)\n"
                      << "  --dem-api-key KEY DEM Terrain-RGB API key (optional)\n"
                      << "  --dem-api-key-env ENV DEM API key env var (default: NATIVE_GLOBE_DEM_TOKEN)\n"
                      << "  --dem-max-zoom N  Max DEM source zoom level (default 15)\n"
                      << "  --dem-mesh-n N    DEM mesh grid size per tile (>=2)\n"
                      << "  --ge-elevation-endpoint URL  Google Earth elevation endpoint\n"
                      << "  --ge-elevation-path PATH     Override {path} in elevation URL (default: Elevation)\n"
                      << "  --ge-epoch EPOCH             Manual GE dataset epoch override\n"
                      << "  --ge-epoch-auto-detect       Auto-detect epoch from PlanetoidMetadata (default: enabled)\n"
                      << "  --no-ge-epoch-auto-detect    Disable GE epoch auto-detection\n"
                      << "  --ge-mesh-endpoint URL       Google Earth mesh endpoint (Phase 5)\n"
                      << "  --ge-header K:V              GE custom header (allowlist: Authorization, X-Custom-Auth, X-Client-Data)\n"
                      << "  --ge-elevation-type TYPE     Elevation type: ellipsoid | terrain | sea_level\n"
                      << "  --ge-mesh-quadkey QK         RockTree NodeData quadkey (Sprint 1, repeatable, digits 0-7)\n"
                      << "  --ge-mesh-no-flip-v          Disable V coordinate flip for texture (default: flip enabled)\n"
                      << "  --ge-mesh-enable-http2       Enable HTTP/2 for mesh requests (default: enabled)\n"
                      << "  --ge-mesh-no-http2           Disable HTTP/2 for mesh requests\n"
                      << "  --ge-mesh-http1-fallback     Allow HTTP/1.1 fallback (default: enabled)\n"
                      << "  --ge-mesh-no-http1-fallback  Disable HTTP/1.1 fallback\n"
                      << "  --ge-mesh-tcp-keepalive SEC  TCP keep-alive interval in seconds (default: 30)\n"
                      << "  --ge-mesh-child-lod-dist M   Child LOD distance threshold in meters (default: 5000)\n"
                      << "  --ge-mesh-max-child-req N    Max child requests per frame (default: 2)\n"
                      << "  --fallback-parent-until-children-ready    Keep parent tile until child tiles are fully ready (default: enabled)\n"
                      << "  --no-fallback-parent-until-children-ready Disable parent hold during child loading\n"
                      << "  --cache-dir DIR   Tile cache directory\n"
                      << "  --no-cache        Disable disk cache\n"
                      << "  --no-dem          Disable DEM\n"
                      << "  --no-distance-morph   Disable distance-based terrain morph (use time-based)\n"
                      << "  --morph-range KM      Terrain morph distance range in km (default: 0.2)\n"
                      << "  --no-morph-fallback   Disable time-based fallback for invalid distance\n"
                      << "  --min-zoom N      Minimum zoom level\n"
                      << "  --max-zoom N      Maximum zoom level\n"
                      << "  --lod-refine-budget N  Max parent->child LOD refinements per frame (<=0 unlimited)\n"
                      << "  --width N         Window width\n"
                      << "  --height N        Window height\n"
                      << "  --headless        Create hidden window (useful for automated tests)\n"
                      << "  --test            Run visual LOD test and exit\n"
                      << "  --smoke           Run smoke test (zoom in/out + terrain) and exit\n"
                      << "  --profile-pan     Run zoom/pan profiler and print per-frame CSV\n"
                      << "  --precision-report  CPU-only precision baseline (Track A: #8)\n"
                      << "  --gpu-terrain     Use GPU heightmap displacement (default: CPU mesh bake)\n"
                      << "  --quality MODE    Render quality: low | medium | high | ultra (default: medium)\n"
                      << "  --help            Show this help\n"
                      << "\nEnvironment:\n"
                      << "  NATIVE_GLOBE_TILE_AUTH  Tile HTTP basic auth (user:password)\n"
                      << "  NATIVE_GLOBE_DEM_AUTH   DEM HTTP basic auth (user:password)\n"
                      << "  NATIVE_GLOBE_DEM_TOKEN  Terrain-RGB API key\n"
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
    
    // Sprint 1: mesh quadkey requires valid endpoint template
    if (!config.geMeshQuadKeys.empty()) {
        std::string err;
        if (!globe::ValidateGeMeshEndpointTemplate(config.geMeshEndpoint, err)) {
            std::cerr << "Error: Invalid --ge-mesh-endpoint: " << err << "\n"
                      << "Usage: --ge-mesh-endpoint URL with {quadkey} placeholder\n"
                      << "Example: https://example.com/mesh/{quadkey}\n";
            return 1;
        }
    }
    
    std::cout << "Native Globe - Clean Architecture\n";
    std::cout << "Tile URL: " << config.tileUrl << "\n";
    if (config.demProvider == "google-earth") {
        std::cout << "GE Elevation Endpoint: " << config.geElevationEndpoint << "\n";
        std::cout << "GE Mesh Endpoint: " << (config.geMeshEndpoint.empty() ? "(not set)" : config.geMeshEndpoint) << "\n";
    } else {
        const std::string demDisplayUrl = RedactSensitiveUrlParams(
            config.demUrl.empty() ? config.demBaseUrl : config.demUrl);
        std::cout << "DEM URL: " << demDisplayUrl << "\n";
    }
    std::cout << "DEM Provider: " << config.demProvider << "\n";
    std::cout << "DEM API Key: " << (config.demApiKey.empty() ? "env/none" : "configured") << "\n";
    std::cout << "Tile Auth: " << (config.tileAuth.empty() ? "none" : "basic") << "\n";
    std::cout << "DEM Auth: " << (config.demAuth.empty() ? "none" : "basic") << "\n";
    if (config.demProvider == "google-earth") {
        bool hasAuthHeader = false;
        for (const auto& header : config.geHeaders) {
            if (header.first == "Authorization") {
                hasAuthHeader = true;
            }
        }
        bool hasEnvToken = !config.geTokenEnv.empty() && std::getenv(config.geTokenEnv.c_str()) != nullptr;
        if (!hasAuthHeader && !hasEnvToken) {
            std::cout << "WARNING: Google Earth elevation likely needs auth. "
                         "Set NATIVE_GLOBE_GE_TOKEN or pass --ge-header Authorization:Bearer <token>.\n";
        }
    }
    std::cout << "Fallback parent while children stream: "
              << (config.fallbackRequireParentUntilChildrenReady ? "enabled" : "disabled") << "\n";
    
    // Handle precision report first (CPU-only, no GPU/GUI needed)
    if (runPrecisionReport) {
        return RunPrecisionReport();
    }
    
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
