#pragma once

#include "../../core/tile_key.h"
#include <vector>
#include <cstdint>

namespace globe {

// Terrain mesh payload for a tile.
// Contains pre-built mesh data from provider (e.g., Google Earth NodeData).
struct TerrainMeshPayload {
    // Vertex positions in ECEF or local tile coordinates
    std::vector<float> positions;  // xyzxyz... 
    
    // Vertex indices for triangles
    std::vector<uint32_t> indices;
    
    // Optional: per-vertex normals
    std::vector<float> normals;    // xyzxyz...
    
    // Optional: texture coordinates
    std::vector<float> texCoords;  // uvuv...
    
    // Height domain for this tile (min/max meters)
    double minHeight = 0.0;
    double maxHeight = 0.0;
    
    // Grid dimensions (if regular grid)
    int gridWidth = 0;
    int gridHeight = 0;
    
    bool valid = false;
};

// Interface for terrain mesh tile providers.
// Implementations fetch pre-built terrain meshes (e.g., Google Earth NodeData).
// Primary use case: Google Earth RockTree/NodeData terrain mesh (Phase 5).
class ITerrainMeshProvider {
public:
    virtual ~ITerrainMeshProvider() = default;

    // Fetch terrain mesh for a tile.
    // Returns true on success with outPayload populated.
    // Returns false on failure (outPayload may be undefined).
    virtual bool FetchMeshTile(const TileKey& key, TerrainMeshPayload& outPayload) = 0;

    // Check if provider has mesh data for this tile (may be false for ocean, etc.).
    virtual bool HasMeshForTile(const TileKey& key) const = 0;

    // Get provider type identifier.
    virtual const char* GetProviderName() const = 0;
};

} // namespace globe
