// RockTree octree mapping determinism tests
// Verifies deterministic TileQuadKeyToOctreePaths ordering and depth filtering.

#include "../src/io/providers/rocktree_octree_index.h"
#include "../src/core/config.h"
#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
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

    // Test 4b: empty and non-face keys should return no candidates
    {
        auto emptyPath = index.TileQuadKeyToOctreePaths("");
        if (!ExpectEq(emptyPath, {}, "Empty tile key should return no candidates")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeEmptyTileKey\n";
        }

        auto invalidFace = index.TileQuadKeyToOctreePaths("99");
        if (!ExpectEq(invalidFace, {}, "Non-face quadkey should return no candidates")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeInvalidFaceFiltered\n";
        }
    }

    // Test 4c: one-digit keys should not generate deeper candidates (short-key fallback contract)
    {
        auto oneDigit = index.TileQuadKeyToOctreePaths("0");
        if (!ExpectEq(oneDigit, {}, "One-digit keys should not expand to deeper paths")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeOneDigitNoExpansion\n";
        }
    }

    // Test 5: empty mapping should return no candidates for invalid root key
    {
        globe::Config config2;
        globe::RockTreeOctreeIndex emptyIndex(config2, nullptr);

#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> emptyNodes;
        emptyIndex.TEST_SetNodesForUnitTests(std::move(emptyNodes));
#endif

        auto emptyResult = emptyIndex.TileQuadKeyToOctreePaths("023");
        if (!ExpectEq(emptyResult, {}, "Empty index should return empty path list")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeEmptyNodes\n";
        }
    }

    // Test 5b: malformed quadkeys should return no candidates
    {
        auto malformed = index.TileQuadKeyToOctreePaths("0A");
        if (!ExpectEq(malformed, {}, "Non-decimal quadkey should return no candidates")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeMalformedTileKey\n";
        }
    }

    // Test 6: path without mesh data should be filtered out
    {
        std::unordered_map<std::string, globe::OctreeNodeInfo> noDataNodes;
        noDataNodes["0230"] = Node(false);
        noDataNodes["0231"] = Node(true);
        noDataNodes["0240"] = Node(false);

#ifdef NATIVE_GLOBE_TESTING
        index.TEST_SetNodesForUnitTests(std::move(noDataNodes));
#endif

        auto paths = index.TileQuadKeyToOctreePaths("023");
        bool seenNoData = false;
        bool seenMissing = false;
        bool seenData = false;
        for (const auto& path : paths) {
            if (path == "0230") seenNoData = true;
            if (path == "0240") seenMissing = true;
            if (path == "0231") seenData = true;
        }
        if (!Expect(!seenNoData && !seenMissing && seenData,
                    "hasNodeData filter should only include nodes with mesh data")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeHasNodeDataFilter\n";
        }
    }

    // Test 7: concurrent callers should receive identical deterministic order
    {
#ifdef NATIVE_GLOBE_TESTING
        std::vector<std::string> expected = index.TileQuadKeyToOctreePaths("023");
        std::atomic<int> badThreads{0};
        auto worker = [&]() {
            for (int i = 0; i < 500; ++i) {
                auto actual = index.TileQuadKeyToOctreePaths("023");
                if (actual != expected) {
                    ++badThreads;
                    return;
                }
            }
        };
        std::vector<std::thread> threads;
        for (int t = 0; t < 6; ++t) {
            threads.emplace_back(worker);
        }
        for (auto& th : threads) th.join();
        if (!Expect(badThreads.load() == 0, "Concurrent TileQuadKeyToOctreePaths calls must remain deterministic")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeMappingConcurrentDeterminism\n";
        }
#else
        std::cerr << "SKIPPED: concurrent determinism requires NATIVE_GLOBE_TESTING\n";
#endif
    }

    // Test 8: GetRenderableNodes should be deterministic and depth-ordered
    {
        std::vector<std::string> expected = {
            "02", "033", "020", "021", "0210", "0211", "0230", "0240"
        };
        auto nodes = index.GetRenderableNodes(2, 4);
        if (!ExpectEq(nodes, expected, "GetRenderableNodes should return deterministic depth/lexicographic order")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeGetRenderableNodesDeterministic\n";
        }
    }

    // Test 9: One-digit keys are intentionally ignored even when path exists
    {
        std::unordered_map<std::string, globe::OctreeNodeInfo> singleDigitNodes;
        singleDigitNodes["0"] = Node(true);
        singleDigitNodes["02"] = Node(true);
#ifdef NATIVE_GLOBE_TESTING
        index.TEST_SetNodesForUnitTests(std::move(singleDigitNodes));
#endif
        auto singleDigit = index.TileQuadKeyToOctreePaths("0");
        if (!ExpectEq(singleDigit, {}, "Single-digit tile key must be ignored")) {
            failures++;
        } else {
            std::cerr << "PASSED: OctreeSingleDigitIgnored\n";
        }
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll RockTree octree mapping tests PASSED\n";
    return 0;
}
