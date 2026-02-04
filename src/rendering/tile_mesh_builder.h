#pragma once

#include "../core/tile.h"
#include "../core/config.h"
#include "../io/dem_manager.h"
#include <vector>

namespace globe {

// TileMeshBuilder - Separated mesh generation logic (GE-style)
// Handles: vertex generation, DEM sampling, Mercator projection, skirt generation
class TileMeshBuilder {
public:
    struct BuildResult {
        std::vector<float> vertices;      // pos(3) + normal(3) + uv(2)
        std::vector<unsigned int> indices;
        bool demUsed = false;
        bool demPending = false;
    };
    
    // Build mesh geometry for a tile
    // Returns vertex/index data; caller uploads to GPU
    static BuildResult Build(
        const Tile& tile,
        DemManager* demManager,
        const Config& config
    );
    
    // Upload mesh to GPU (creates VAO/VBO/EBO)
    static void UploadToGPU(Tile& tile, const BuildResult& result);
    
    // Delete existing mesh resources
    static void DeleteMesh(Tile& tile);

private:
    // Skirt generation (GE-style seam hiding)
    static void GenerateSkirts(
        std::vector<float>& vertices,
        std::vector<unsigned int>& indices,
        int segments,
        int level
    );
};

} // namespace globe
