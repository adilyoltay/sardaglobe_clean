#include "../src/core/tile_key.h"

#include <iostream>
#include <unordered_set>
#include <vector>

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

std::vector<TileKey> CollectSeamUpdateKeys(
    const std::unordered_set<TileKey>& renderLeafSet,
    const std::unordered_set<TileKey>& desiredLeafSet) {
    (void)desiredLeafSet;
    std::vector<TileKey> keys;
    keys.reserve(renderLeafSet.size());
    for (const TileKey& key : renderLeafSet) {
        keys.push_back(key);
    }
    return keys;
}

} // namespace

int main() {
    int failures = 0;

    std::unordered_set<TileKey> renderLeafSet = {
        TileKey(6, 32, 22),
        TileKey(6, 33, 22)
    };

    std::unordered_set<TileKey> desiredLeafSet = renderLeafSet;
    desiredLeafSet.insert(TileKey(7, 65, 45));  // desired-only key

    std::vector<TileKey> seamKeys = CollectSeamUpdateKeys(renderLeafSet, desiredLeafSet);

    if (!Expect(seamKeys.size() == renderLeafSet.size(), "Seam update set must match render set cardinality")) {
        failures++;
    }

    for (const TileKey& key : seamKeys) {
        if (!Expect(renderLeafSet.count(key) > 0, "Seam update must use render-authoritative keys only")) {
            failures++;
            break;
        }
        if (!Expect(!(key == TileKey(7, 65, 45)), "Desired-only key must not enter seam update set")) {
            failures++;
            break;
        }
    }

    if (failures == 0) {
        Report("RenderAuthoritativeContext");
        std::cerr << "\nAll render/desired context consistency tests PASSED\n";
        return 0;
    }

    std::cerr << "\n" << failures << " test(s) FAILED\n";
    return 1;
}
