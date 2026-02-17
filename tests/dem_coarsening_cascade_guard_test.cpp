#include <algorithm>
#include <iostream>

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

int ApplyUnstableGuard(int keyLevel, int maxReachableDemLevel, int coarseningDelta, int minReducedLevel) {
    const int minUnstableLevel = std::clamp(keyLevel - (coarseningDelta + 2), 0, maxReachableDemLevel);
    return std::max(minReducedLevel, minUnstableLevel);
}

} // namespace

int main() {
    int failures = 0;

    {
        const int guarded = ApplyUnstableGuard(12, 12, 2, 0);
        if (!Expect(guarded == 8, "Unstable guard must block root collapse for level=12, delta=2")) failures++;
        else Report("RootCollapseGuard");
    }

    {
        const int guarded = ApplyUnstableGuard(6, 6, 1, 1);
        if (!Expect(guarded == 3, "Unstable guard must clamp to key-(delta+2)")) failures++;
        else Report("LevelClampGuard");
    }

    {
        const int guarded = ApplyUnstableGuard(1, 1, 4, 0);
        if (!Expect(guarded == 0, "Unstable guard must respect level-0 floor")) failures++;
        else Report("LevelZeroFloor");
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll DEM coarsening guard tests PASSED\n";
    return 0;
}
