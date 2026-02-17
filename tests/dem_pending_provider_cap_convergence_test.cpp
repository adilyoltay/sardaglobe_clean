#include "../src/io/dem_manager.h"
#include "../src/core/tile.h"
#include "../src/core/tile_key.h"

#include <algorithm>
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

void Report(const char* test) {
    std::cerr << "PASSED: " << test << '\n';
}

TileKey KeyAtLevel(TileKey k, int targetLevel) {
    const int lvl = std::clamp(targetLevel, 0, k.level);
    while (k.level > lvl) {
        k = k.Parent();
    }
    return k;
}

DemGridData MakeFlatGrid() {
    DemGridData grid;
    grid.valid = true;
    grid.meshN = 2;
    grid.heights = {10.0, 10.0, 10.0, 10.0};
    grid.minHeight = 10.0;
    grid.maxHeight = 10.0;
    return grid;
}

uint8_t BuildReasonMask(bool hasOwnDem, bool hasEdgeCoherentDem, bool hasNeighborParentDem) {
    uint8_t reasons = 0;
    if (!hasOwnDem) {
        reasons |= Tile::DEM_PENDING_MISSING_OWN_TARGET;
    }
    if (!hasEdgeCoherentDem) {
        reasons |= Tile::DEM_PENDING_MISSING_EDGE_COHERENT;
    }
    if (!hasNeighborParentDem) {
        reasons |= Tile::DEM_PENDING_MISSING_NEIGHBOR_PARENT;
    }
    return reasons;
}

} // namespace

int main() {
    int failures = 0;

    DemManager::Config demCfg;
    demCfg.providerType = DemProviderType::TerrainRGB;
    demCfg.maxZoom = 15;
    demCfg.meshN = 2;

    DemManager demManager(demCfg);

    TileKey childKey(18, 100000, 70000);
    TileKey targetKey = KeyAtLevel(childKey, 15);
    TileKey parentKey = childKey.Parent();

    demManager.PutGridData(targetKey, MakeFlatGrid());
    demManager.PutGridData(parentKey, MakeFlatGrid());

    const bool hasOwnDem = demManager.HasData(targetKey);
    const bool hasAnyDem = demManager.HasDataOrAncestor(targetKey);
    const bool hasNeighborParentDem = demManager.HasDataOrAncestor(parentKey);
    const bool hasEdgeCoherentDem = true;  // demEdgeLevelPack == target level in this scenario

    const uint8_t reasons = BuildReasonMask(hasOwnDem, hasEdgeCoherentDem, hasNeighborParentDem);

    if (!Expect(hasOwnDem, "Clamped provider target DEM must exist in cache")) failures++;
    if (!Expect(hasAnyDem, "Clamped provider target DEM must satisfy ancestor availability")) failures++;
    if (!Expect(reasons == 0, "demPending reason mask must converge to 0 under provider cap")) failures++;
    if (!Expect((reasons & Tile::DEM_PENDING_MISSING_OWN_TARGET) == 0,
                "MISSING_OWN_TARGET bit must clear when clamped target exists")) failures++;

    if (failures == 0) {
        Report("DemPendingProviderCapConvergence");
        std::cerr << "\nAll DEM pending provider-cap convergence tests PASSED\n";
        return 0;
    }

    std::cerr << "\n" << failures << " test(s) FAILED\n";
    return 1;
}
