#include <iostream>

namespace {

struct AtomicResult {
    int revisionBumpCount = 0;
    int atomicRebuildMetric = 0;
};

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

AtomicResult EvaluateAtomicPolicy(bool packChanged, bool edgeAvailabilityRebuild) {
    AtomicResult out;
    if (packChanged || edgeAvailabilityRebuild) {
        out.revisionBumpCount = 1;
        if (packChanged && edgeAvailabilityRebuild) {
            out.atomicRebuildMetric = 1;
        }
    }
    return out;
}

} // namespace

int main() {
    int failures = 0;

    {
        AtomicResult r = EvaluateAtomicPolicy(true, true);
        if (!Expect(r.revisionBumpCount == 1, "Pack+edge-ready must produce exactly one revision bump")) failures++;
        if (!Expect(r.atomicRebuildMetric == 1, "Pack+edge-ready must increment atomic rebuild metric")) failures++;
        else Report("AtomicSameFramePackAndEdge");
    }

    {
        AtomicResult r = EvaluateAtomicPolicy(true, false);
        if (!Expect(r.revisionBumpCount == 1, "Pack-only change must still rebuild once")) failures++;
        if (!Expect(r.atomicRebuildMetric == 0, "Pack-only change must not count as atomic combo")) failures++;
        else Report("PackOnlyRevision");
    }

    {
        AtomicResult r = EvaluateAtomicPolicy(false, false);
        if (!Expect(r.revisionBumpCount == 0, "No change must not rebuild")) failures++;
        if (!Expect(r.atomicRebuildMetric == 0, "No change must not increment metric")) failures++;
        else Report("NoOpNoRevision");
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll DEM edge-pack atomicity tests PASSED\n";
    return 0;
}
