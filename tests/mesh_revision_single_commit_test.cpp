#include <cstdint>
#include <iostream>
#include <vector>

namespace {

enum RevisionReason : uint8_t {
    REV_NONE = 0,
    REV_TOPOLOGY = 1 << 0,
    REV_DEM_TARGET_CONVERGENCE = 1 << 1,
    REV_DEM_PENDING_CONVERGENCE = 1 << 2,
    REV_EDGE_PACK_CHANGED = 1 << 3,
    REV_EDGE_AVAILABILITY = 1 << 4
};

struct RevisionFrameStats {
    int bumps = 0;
    int doubleReasonTiles = 0;
};

RevisionFrameStats AggregateRevisionStats(const std::vector<uint8_t>& reasonMasks) {
    RevisionFrameStats out{};
    for (uint8_t mask : reasonMasks) {
        if (mask == 0) {
            continue;
        }
        ++out.bumps;  // Single commit per tile per frame.
        const uint32_t bits = static_cast<uint32_t>(mask);
        if ((bits & (bits - 1u)) != 0u) {
            ++out.doubleReasonTiles;
        }
    }
    return out;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failures = 0;

    {
        const RevisionFrameStats stats = AggregateRevisionStats({REV_NONE, REV_NONE});
        if (!Expect(stats.bumps == 0, "No revision reasons must produce zero bumps")) failures++;
        if (!Expect(stats.doubleReasonTiles == 0, "No revision reasons must produce zero double-reason tiles")) failures++;
    }

    {
        const RevisionFrameStats stats = AggregateRevisionStats({REV_TOPOLOGY});
        if (!Expect(stats.bumps == 1, "Single reason must produce exactly one bump")) failures++;
        if (!Expect(stats.doubleReasonTiles == 0, "Single reason tile must not count as double-reason")) failures++;
    }

    {
        const uint8_t multiReason = static_cast<uint8_t>(REV_EDGE_PACK_CHANGED | REV_EDGE_AVAILABILITY);
        const RevisionFrameStats stats = AggregateRevisionStats({multiReason});
        if (!Expect(stats.bumps == 1, "Multi-reason tile must still produce one bump")) failures++;
        if (!Expect(stats.doubleReasonTiles == 1, "Multi-reason tile must count as double-reason")) failures++;
    }

    {
        const uint8_t multiReason = static_cast<uint8_t>(REV_DEM_TARGET_CONVERGENCE | REV_DEM_PENDING_CONVERGENCE);
        const RevisionFrameStats stats = AggregateRevisionStats(
            {REV_TOPOLOGY, multiReason, REV_NONE, REV_EDGE_PACK_CHANGED}
        );
        if (!Expect(stats.bumps == 3, "Bump count must equal number of non-zero reason masks")) failures++;
        if (!Expect(stats.doubleReasonTiles == 1, "Only masks with >1 bit must count as double-reason")) failures++;
    }

    if (failures > 0) {
        std::cerr << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "MeshRevisionSingleCommitTest PASSED\n";
    return 0;
}
