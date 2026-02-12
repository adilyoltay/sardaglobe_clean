#include "../src/core/config.h"
#include <cassert>
#include <iostream>

using globe::AdaptiveMeshSegments;

int main() {
    // DEM disabled: preserve configured fixed tessellation.
    assert(AdaptiveMeshSegments(/*level=*/10, /*meshSegments=*/64, /*demMeshN=*/5, /*hasDem=*/false) == 64);

    // DEM enabled: halve every two levels, floor at max(demMeshN-1, 8).
    assert(AdaptiveMeshSegments(0, 64, 5, true) == 64);
    assert(AdaptiveMeshSegments(1, 64, 5, true) == 64);
    assert(AdaptiveMeshSegments(2, 64, 5, true) == 32);
    assert(AdaptiveMeshSegments(3, 64, 5, true) == 32);
    assert(AdaptiveMeshSegments(4, 64, 5, true) == 16);
    assert(AdaptiveMeshSegments(5, 64, 5, true) == 16);
    assert(AdaptiveMeshSegments(6, 64, 5, true) == 8);
    assert(AdaptiveMeshSegments(7, 64, 5, true) == 8);
    assert(AdaptiveMeshSegments(8, 64, 5, true) == 8);
    assert(AdaptiveMeshSegments(9, 64, 5, true) == 8);

    // Respect DEM floor for denser DEM grids.
    assert(AdaptiveMeshSegments(8, 64, 17, true) == 16);  // floor from demMeshN-1

    // Never collapse below hard minimum floor.
    assert(AdaptiveMeshSegments(7, 12, 5, true) == 8);

    std::cout << "AdaptiveMeshSegmentsTest passed\n";
    return 0;
}
