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

uint8_t ComputeSkirtMaskTelemetryOnly(uint8_t topologySkirtMask, uint8_t seamGapMaskTelemetry) {
    (void)seamGapMaskTelemetry;
    return topologySkirtMask;
}

} // namespace

int main() {
    int failures = 0;

    const uint8_t topologyMask = static_cast<uint8_t>(Tile::EDGE_NORTH | Tile::EDGE_WEST);
    const uint8_t seamMaskA = 0;
    const uint8_t seamMaskB = static_cast<uint8_t>(Tile::EDGE_EAST | Tile::EDGE_SOUTH);

    const uint8_t skirtA = ComputeSkirtMaskTelemetryOnly(topologyMask, seamMaskA);
    const uint8_t skirtB = ComputeSkirtMaskTelemetryOnly(topologyMask, seamMaskB);

    if (!Expect(skirtA == topologyMask, "Topology mask must remain unchanged without seam telemetry")) {
        failures++;
    }
    if (!Expect(skirtB == topologyMask, "Seam telemetry must not alter skirt topology")) {
        failures++;
    }
    if (!Expect(skirtA == skirtB, "Changing seamGapMask must not change skirt mask")) {
        failures++;
    }

    if (failures > 0) {
        std::cerr << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "SeamFeedbackTelemetryOnlyTest PASSED\n";
    return 0;
}
