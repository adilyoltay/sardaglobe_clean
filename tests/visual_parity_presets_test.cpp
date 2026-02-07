// Visual parity preset gate test.
// Encodes screenshot-derived camera presets and metric thresholds used by regression gates.

#include <cmath>
#include <iostream>
#include <vector>

namespace {

struct Preset {
    double lat;
    double lon;
    double altitudeKm;
    double tiltDeg;
    double headingDeg;
};

struct VisualMetrics {
    int leafUnderflowFrames = 0;
    double seamGapP95M = 0.0;
    double seamGapMaxM = 0.0;
    int cliffEdgeCount = 0;
    double ancestorDemRatio = 0.0;
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool PassMetrics(const VisualMetrics& metrics) {
    return metrics.leafUnderflowFrames == 0 &&
           metrics.seamGapP95M <= 4.0 &&
           metrics.seamGapMaxM <= 15.0 &&
           metrics.cliffEdgeCount == 0 &&
           metrics.ancestorDemRatio <= 0.20;
}

} // namespace

int main() {
    int failed = 0;

    const std::vector<Preset> presets = {
        {38.0568, 37.1468, 2053.5, 10.7, 1.5},
        {37.2361, 33.5648, 141.3, 10.7, 1.5},
        {36.7096, 35.2644, 197.8, 10.7, 1.5},
        {35.6315, 36.7466, 101.5, 59.6, 11.2},
    };

    failed += !Expect(presets.size() == 4, "preset count should match parity suite");
    for (const Preset& preset : presets) {
        failed += !Expect(std::isfinite(preset.lat) && std::fabs(preset.lat) <= 90.0,
                          "latitude must be valid");
        failed += !Expect(std::isfinite(preset.lon) && std::fabs(preset.lon) <= 180.0,
                          "longitude must be valid");
        failed += !Expect(std::isfinite(preset.altitudeKm) && preset.altitudeKm > 0.0,
                          "altitude must be positive");
        failed += !Expect(std::isfinite(preset.tiltDeg) && preset.tiltDeg >= 0.0 && preset.tiltDeg <= 90.0,
                          "tilt must be within [0, 90]");
    }

    failed += !Expect(PassMetrics(VisualMetrics{0, 2.0, 8.0, 0, 0.10}), "good metrics should pass");
    failed += !Expect(!PassMetrics(VisualMetrics{1, 2.0, 8.0, 0, 0.10}), "leaf underflow must fail gate");
    failed += !Expect(!PassMetrics(VisualMetrics{0, 7.0, 8.0, 0, 0.10}), "seam p95 overflow must fail gate");
    failed += !Expect(!PassMetrics(VisualMetrics{0, 2.0, 18.0, 0, 0.10}), "seam max overflow must fail gate");
    failed += !Expect(!PassMetrics(VisualMetrics{0, 2.0, 8.0, 2, 0.10}), "cliff edges must fail gate");
    failed += !Expect(!PassMetrics(VisualMetrics{0, 2.0, 8.0, 0, 0.45}), "ancestor ratio overflow must fail gate");

    if (failed == 0) {
        std::cout << "VisualParityPresetsTest PASSED\n";
        return 0;
    }

    std::cerr << "VisualParityPresetsTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
