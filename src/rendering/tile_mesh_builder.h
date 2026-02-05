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
        TileKey key;
        std::vector<float> vertices;      // pos(3) + normal(3) + uv(2)
        std::vector<unsigned int> indices;
        int segments = 0;
        uint32_t indexCount = 0;
        bool useSharedEBO = false;
        bool demUsed = false;
        bool demPending = false;
        uint32_t meshRevision = 0;
        double minHeightKm = 0.0;         // For height-aware skirt depth
        double maxHeightKm = 0.0;
    };
    
    // Build mesh geometry for a tile
    // Returns vertex/index data; caller uploads to GPU
    static BuildResult Build(
        const TileKey& key,
        const Extent& extent,
        uint8_t edgeMask,
        DemManager* demManager,
        const Config& config,
        bool useSharedEBO
    );
    
    // Upload mesh to GPU (creates VAO/VBO/EBO)
    static void UploadToGPU(Tile& tile, const BuildResult& result);
    
    // Delete existing mesh resources
    static void DeleteMesh(Tile& tile);

private:
    // Skirt generation (GE-style seam hiding)
    // heightRange: max-min height in km (for height-aware skirt depth)
    static void GenerateSkirts(
        std::vector<float>& vertices,
        std::vector<unsigned int>* indices,
        int segments,
        int level,
        double heightRange = 0.0
    );
};

} // namespace globe
