#pragma once

#include "../core/tile.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace globe {

// Build per-corner LOD interpolation weights from edge-coarser flags.
// Returns vec4(NW, NE, SE, SW), each component in [0,1].
inline glm::vec4 CornerLodsFromEdgeMask(uint8_t edgeMask) {
    float north = (edgeMask & Tile::EDGE_NORTH) ? 1.0f : 0.0f;
    float east  = (edgeMask & Tile::EDGE_EAST)  ? 1.0f : 0.0f;
    float south = (edgeMask & Tile::EDGE_SOUTH) ? 1.0f : 0.0f;
    float west  = (edgeMask & Tile::EDGE_WEST)  ? 1.0f : 0.0f;

    return glm::vec4(
        std::max(north, west),  // NW
        std::max(north, east),  // NE
        std::max(south, east),  // SE
        std::max(south, west)   // SW
    );
}

// Build per-corner LOD levels from edge LOD deltas.
// Inputs are per-edge delta levels (N,E,S,W): tile.level - neighborCoverLevel, clamped >= 0.
// Returns vec4(NW, NE, SE, SW). These values are fed to textureLod() via bilinear interpolation.
inline glm::vec4 CornerLodsFromEdgeDeltas(float northDelta, float eastDelta,
                                          float southDelta, float westDelta) {
    northDelta = std::max(0.0f, northDelta);
    eastDelta  = std::max(0.0f, eastDelta);
    southDelta = std::max(0.0f, southDelta);
    westDelta  = std::max(0.0f, westDelta);

    return glm::vec4(
        std::max(northDelta, westDelta),  // NW
        std::max(northDelta, eastDelta),  // NE
        std::max(southDelta, eastDelta),  // SE
        std::max(southDelta, westDelta)   // SW
    );
}

} // namespace globe
