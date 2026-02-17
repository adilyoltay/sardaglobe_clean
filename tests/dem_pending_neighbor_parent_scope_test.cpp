#include "../src/core/tile.h"
#include <iostream>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

uint8_t BuildPendingReasons(bool hasOwnDem,
                            bool hasEdgeCoherentDem,
                            bool hasCoarserNeighborParentDem,
                            uint8_t edgeCoarserMask) {
    uint8_t reasons = 0;
    if (!hasOwnDem) {
        reasons |= Tile::DEM_PENDING_MISSING_OWN_TARGET;
    }
    if (!hasEdgeCoherentDem) {
        reasons |= Tile::DEM_PENDING_MISSING_EDGE_COHERENT;
    }
    if (edgeCoarserMask != 0 && !hasCoarserNeighborParentDem) {
        reasons |= Tile::DEM_PENDING_MISSING_NEIGHBOR_PARENT;
    }
    return reasons;
}

} // namespace

int main() {
    int failures = 0;

    {
        uint8_t reasons = BuildPendingReasons(true, true, false, 0);
        if (!Expect((reasons & Tile::DEM_PENDING_MISSING_NEIGHBOR_PARENT) == 0,
                    "edgeCoarserMask==0 must not set MISSING_NEIGHBOR_PARENT")) {
            failures++;
        }
    }

    {
        uint8_t reasons = BuildPendingReasons(true, true, false, Tile::EDGE_EAST);
        if (!Expect((reasons & Tile::DEM_PENDING_MISSING_NEIGHBOR_PARENT) != 0,
                    "coarser edge without neighbor parent must set MISSING_NEIGHBOR_PARENT")) {
            failures++;
        }
    }

    if (failures > 0) {
        std::cerr << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "DemPendingNeighborParentScopeTest PASSED\n";
    return 0;
}
