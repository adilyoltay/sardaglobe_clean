#pragma once

#include "tile.h"
#include "tile_math.h"  // Globe constants and tile geometry
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cmath>

namespace earth {

// Height sampler callback type
using HeightSampler = std::function<bool(double lonDeg, double latDeg, int level, double& heightMeters)>;

// Constants imported from tile_math.h (GLOBE_RADIUS, GLOBE_RADIUS_K, etc.)

// Mesh build result (CPU side, ready for GPU upload)
struct MeshData {
    std::vector<float> vertices;      // Interleaved: pos(3) + normal(3) + uv(2)
    std::vector<unsigned int> indices;
    bool demUsed = false;
    bool demPending = false;
    double minHeight = 0.0;
    double maxHeight = 0.0;
};

// High-performance tile mesh builder
// - Optimized vertex generation
// - Google Earth style skirts
// - LOD edge stitching
class TileMeshBuilder {
public:
    struct Config {
        int segments = 16;              // Grid resolution (16x16 default)
        bool generateSkirts = true;     // Generate skirts to hide LOD seams
        double skirtDepthPercent = 0.01;// Skirt depth as % of tile arc length
        double skirtMinKm = 0.001;      // Minimum skirt depth (1m)
        double skirtMaxPercent = 0.5;   // Maximum skirt as % of tile size
        bool adaptiveSegments = true;   // Use more segments at higher zoom
        int minSegments = 8;            // Minimum segments (low zoom)
        int maxSegments = 32;           // Maximum segments (high zoom)
    };
    
    // Get adaptive segment count based on zoom level
    static int GetAdaptiveSegments(int z, const Config& config) {
        if (!config.adaptiveSegments) return config.segments;
        // Linear interpolation: z=2 -> minSegments, z=18 -> maxSegments
        float t = std::clamp((z - 2.0f) / 16.0f, 0.0f, 1.0f);
        return static_cast<int>(config.minSegments + t * (config.maxSegments - config.minSegments));
    }
    
    TileMeshBuilder();
    explicit TileMeshBuilder(const Config& config);
    
    // Build mesh for a tile
    // edgeFlags: EDGE_LEFT|EDGE_RIGHT|EDGE_TOP|EDGE_BOTTOM for stitching
    MeshData Build(int x, int y, int z, int edgeFlags = EDGE_NONE,
                   const HeightSampler* heightSampler = nullptr);
    
    // Upload mesh data to GPU
    static TileMesh UploadToGPU(const MeshData& data);
    
    // Delete GPU resources
    static void DeleteMesh(TileMesh& mesh);
    
    // Configuration
    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }
    
    // Public helper functions (used by PoleMeshBuilder too)
    static glm::vec3 LatLonToSphere(double latRad, double lonRad, double radius);
    static double Tile2Lon(int x, int z);
    static double Tile2Lat(int y, int z);
    
private:
    Config config_;
    
    void GenerateGridVertices(MeshData& data, int x, int y, int z,
                              const HeightSampler* heightSampler);
    void CalculateNormals(MeshData& data, int vertsPerRow);
    void ApplyEdgeStitching(MeshData& data, int edgeFlags, int vertsPerRow);
    void GenerateIndices(MeshData& data, int segments);
    void GenerateSkirts(MeshData& data, int z, int segments);
};

// Pole mesh builder for north/south pole caps
class PoleMeshBuilder {
public:
    static MeshData BuildNorthPole(int segments = 32);
    static MeshData BuildSouthPole(int segments = 32);
    static PoleMesh UploadToGPU(const MeshData& data);
};

} // namespace earth
