// Visual parity preset gate test.
// Encodes camera presets and smoke/visual metric thresholds used by parity regression gates.

#include <cmath>
#include <iostream>
#include <vector>

namespace {

struct Preset {
    const char* name;
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

struct SmokeGateMetrics {
    int missingTilesMax = 0;
    int placeholderTilesEnd = 0;
    int demPendingLeavesEnd = 0;
    double seamGapMaxPeakM = 0.0;
    double seamGapMaxEndM = 0.0;
    int cliffEdgeEnd = 0;
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool PassVisualMetrics(const VisualMetrics& metrics) {
    return metrics.leafUnderflowFrames == 0 &&
           metrics.seamGapP95M <= 4.0 &&
           metrics.seamGapMaxM <= 15.0 &&
           metrics.cliffEdgeCount == 0 &&
           metrics.ancestorDemRatio <= 0.20;
}

bool PassSmokeGate(const SmokeGateMetrics& metrics) {
    // Decision-complete parity thresholds for smoke scene gates.
    return metrics.missingTilesMax == 0 &&
           metrics.placeholderTilesEnd == 0 &&
           metrics.demPendingLeavesEnd == 0 &&
           metrics.seamGapMaxPeakM < 30.0 &&
           metrics.seamGapMaxEndM < 10.0 &&
           metrics.cliffEdgeEnd == 0;
}

} // namespace

int main() {
    int failed = 0;

    const std::vector<Preset> presets = {
        {"anatolia_high", 38.0568, 37.1468, 2053.5, 10.7, 1.5},
        {"anatolia_mid", 37.2361, 33.5648, 141.3, 10.7, 1.5},
        {"mediterranean_mid", 36.7096, 35.2644, 197.8, 10.7, 1.5},
        {"levant_tilt", 35.6315, 36.7466, 101.5, 59.6, 11.2},
        {"aegean_smoke_start", 39.0000, 27.0000, 1800.0, 0.0, 0.0},
        {"aegean_smoke_pan", 38.4500, 26.3500, 900.0, 0.0, 0.0},
    };

    failed += !Expect(presets.size() == 6, "preset count should match parity suite + aegean smoke");
    for (const Preset& preset : presets) {
        failed += !Expect(preset.name != nullptr && preset.name[0] != '\0', "preset name must be non-empty");
        failed += !Expect(std::isfinite(preset.lat) && std::fabs(preset.lat) <= 90.0,
                          "latitude must be valid");
        failed += !Expect(std::isfinite(preset.lon) && std::fabs(preset.lon) <= 180.0,
                          "longitude must be valid");
        failed += !Expect(std::isfinite(preset.altitudeKm) && preset.altitudeKm > 0.0,
                          "altitude must be positive");
        failed += !Expect(std::isfinite(preset.tiltDeg) && preset.tiltDeg >= 0.0 && preset.tiltDeg <= 90.0,
                          "tilt must be within [0, 90]");
        failed += !Expect(std::isfinite(preset.headingDeg), "heading must be finite");
    }

    failed += !Expect(PassVisualMetrics(VisualMetrics{0, 2.0, 8.0, 0, 0.10}), "good visual metrics should pass");
    failed += !Expect(!PassVisualMetrics(VisualMetrics{1, 2.0, 8.0, 0, 0.10}), "leaf underflow must fail visual gate");
    failed += !Expect(!PassVisualMetrics(VisualMetrics{0, 7.0, 8.0, 0, 0.10}), "seam p95 overflow must fail visual gate");
    failed += !Expect(!PassVisualMetrics(VisualMetrics{0, 2.0, 18.0, 0, 0.10}), "seam max overflow must fail visual gate");
    failed += !Expect(!PassVisualMetrics(VisualMetrics{0, 2.0, 8.0, 2, 0.10}), "cliff edges must fail visual gate");
    failed += !Expect(!PassVisualMetrics(VisualMetrics{0, 2.0, 8.0, 0, 0.45}), "ancestor ratio overflow must fail visual gate");

    failed += !Expect(PassSmokeGate(SmokeGateMetrics{0, 0, 0, 18.0, 6.0, 0}), "clean smoke metrics should pass");
    failed += !Expect(!PassSmokeGate(SmokeGateMetrics{3, 0, 0, 24.0, 24.0, 2}),
                      "missing tiles or residual cliffs must fail smoke gate");
    failed += !Expect(!PassSmokeGate(SmokeGateMetrics{0, 0, 7, 24.0, 8.0, 0}),
                      "demPending residual must fail smoke gate");
    failed += !Expect(!PassSmokeGate(SmokeGateMetrics{0, 0, 0, 42.0, 8.0, 0}),
                      "transient seam peak overflow must fail smoke gate");

    if (failed == 0) {
        std::cout << "VisualParityPresetsTest PASSED\n";
        return 0;
    }

    std::cerr << "VisualParityPresetsTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
