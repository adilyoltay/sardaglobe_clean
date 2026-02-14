// RockTree octree mapping determinism tests
// Verifies deterministic TileQuadKeyToOctreePaths ordering and depth filtering.

#include "../src/io/providers/rocktree_octree_index.h"
#include "../src/core/config.h"
#include <cstddef>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool ExpectEq(const std::vector<std::string>& a, const std::vector<std::string>& b, const char* message) {
    if (a != b) {
        std::cerr << "FAILED: " << message << '\n';
        std::cerr << "  expected: [";
        for (std::size_t i = 0; i < b.size(); i++) {
            if (i > 0) std::cerr << ", ";
            std::cerr << b[i];
        }
        std::cerr << "]\n  got:      [";
        for (std::size_t i = 0; i < a.size(); i++) {
            if (i > 0) std::cerr << ", ";
            std::cerr << a[i];
        }
        std::cerr << "]\n";
        return false;
    }
    return true;
}

globe::OctreeNodeInfo Node(bool hasData) {
    globe::OctreeNodeInfo info;
    info.hasNodeData = hasData;
    return info;
}

} // namespace

int main() {
    int failures = 0;
    globe::Config config;
    globe::RockTreeOctreeIndex index(config, nullptr);

    std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
    nodes["02"] = Node(true);   // face
    nodes["020"] = Node(true);
    nodes["021"] = Node(true);
    nodes["022"] = Node(false);
    nodes["0210"] = Node(true);
    nodes["0211"] = Node(true);
    nodes["0230"] = Node(true);
    nodes["02310"] = Node(true);
    nodes["0240"] = Node(true);
    nodes["033"] = Node(true);  // different face; must not leak in.

#ifdef NATIVE_GLOBE_TESTING
    index.TEST_SetNodesForUnitTests(std::move(nodes));
#endif

    // Test 1: deterministic ordering by ascending depth then lexicographic
    {
        std::vector<std::string> paths = index.TileQuadKeyToOctreePaths("023");
        std::vector<std::string> expected = {"02", "020", "021", "0210", "0211", "0230"};
        if (!ExpectEq(paths, expected, "TileQuadKeyToOctreePaths should return deterministic ordering")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeMappingOrderingIsDeterministic\n";
        }
    }

    // Test 2: same input should return stable output on repeated calls
    {
        auto first = index.TileQuadKeyToOctreePaths("023");
        auto second = index.TileQuadKeyToOctreePaths("023");
        if (!ExpectEq(first, second, "TileQuadKeyToOctreePaths must be stable across repeated calls")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeMappingStableAcrossCalls\n";
        }
    }

    // Test 3: depth bounds + face prefix filtering is respected
    {
        auto paths = index.TileQuadKeyToOctreePaths("023");
        bool ok = true;
        for (const auto& path : paths) {
            const int depth = static_cast<int>(path.size());
            if (!(depth >= 2 && depth <= 4)) ok = false;
            if (path.size() >= 2 && path.substr(0, 2) != "02") ok = false;
            if (path == "033") ok = false;
        }
        if (!Expect(ok, "Depth and face-prefix filters should be enforced")) {
            failures++;
        }
    }

    {
        auto paths = index.TileQuadKeyToOctreePaths("023");
        bool has02310 = false;
        bool has0240 = false;
        for (const auto& path : paths) {
            if (path == "02310") has02310 = true;
            if (path == "0240") has0240 = true;
        }
        if (!Expect(!has02310, "Paths deeper than maxDepth should be filtered")) {
            failures++;
        }
        if (!Expect(!has0240, "Different face prefix paths should be filtered")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeMappingDepthAndFaceFilters\n";
        }
    }

    // Test 4: short keys should still return exact-match candidate when available
    {
        auto shortPath = index.TileQuadKeyToOctreePaths("02");
        std::vector<std::string> expected = {"02"};
        if (!ExpectEq(shortPath, expected, "Short-path mapping should keep exact short key when node exists")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeShortPathExactMatch\n";
        }
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll RockTree octree mapping tests PASSED\n";
    return 0;
}
