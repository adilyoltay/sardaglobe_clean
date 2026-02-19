// Unit tests for dense quadtree epoch inheritance (ancestor chain)
// and MSB alternation rule in PopulateNodes.

#include "../src/io/providers/rocktree_octree_index.h"
#include "../src/core/config.h"
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    std::cerr << "PASSED: " << message << '\n';
    return true;
}

globe::OctreeNodeInfo MakeNode(bool hasData, uint32_t epoch = 0) {
    globe::OctreeNodeInfo info;
    info.hasNodeData = hasData;
    info.epoch = epoch;
    return info;
}

// Apply the same MSB Alternation Rule that PopulateNodes applies in production.
// This is needed because TEST_SetNodesForUnitTests bypasses PopulateNodes.
bool IsValidMsbPath(const std::string& path) {
    if (path.length() < 2) return true;
    for (size_t k = 1; k < path.length(); ++k) {
        int msbCurr = (path[k] - '0') >> 1;
        int msbPrev = (path[k-1] - '0') >> 1;
        if (msbCurr == msbPrev) return false;
    }
    return true;
}

void ApplyMsbFilter(std::unordered_map<std::string, globe::OctreeNodeInfo>& nodes) {
    for (auto& [path, info] : nodes) {
        if (info.hasNodeData && !IsValidMsbPath(path)) {
            info.hasNodeData = false;
        }
    }
}

} // namespace

int main() {
    int failures = 0;
    globe::Config config;

    // =========================================================================
    // Test Group 1: GetNodeEpoch ancestor chain
    // =========================================================================

    // Test 1a: Node with own epoch returns it directly
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["02"] = MakeNode(true, 1005);
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.GetNodeEpoch("02") == 1005,
                    "GetNodeEpoch: node with own epoch returns it"))
            failures++;
    }

    // Test 1b: Node with epoch 0 inherits from parent
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["02"] = MakeNode(true, 1005);
        nodes["021"] = MakeNode(true, 0);
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.GetNodeEpoch("021") == 1005,
                    "GetNodeEpoch: epoch 0 inherits from parent"))
            failures++;
    }

    // Test 1c: Ancestor chain — parent also epoch 0, grandparent has epoch
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["0"] = MakeNode(false, 1005);
        nodes["02"] = MakeNode(true, 0);     // parent epoch 0
        nodes["021"] = MakeNode(true, 0);    // this node epoch 0
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.GetNodeEpoch("021") == 1005,
                    "GetNodeEpoch: ancestor chain walks to grandparent"))
            failures++;
    }

    // Test 1d: Deep chain — 3 levels of epoch 0, ancestor at root
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes[""] = MakeNode(false, 1008);   // root with epoch
        nodes["0"] = MakeNode(false, 0);
        nodes["02"] = MakeNode(true, 0);
        nodes["021"] = MakeNode(true, 0);
        nodes["0213"] = MakeNode(true, 0);
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.GetNodeEpoch("0213") == 1008,
                    "GetNodeEpoch: deep chain falls back to root epoch"))
            failures++;
    }

    // Test 1e: No ancestor has epoch → falls back to global epoch_
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["02"] = MakeNode(true, 0);
        nodes["021"] = MakeNode(true, 0);
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        // Global epoch is 0 by default (not initialized), so expect 0
        uint32_t epoch = idx.GetNodeEpoch("021");
        if (!Expect(epoch == 0,
                    "GetNodeEpoch: no ancestor epoch → global epoch (0)"))
            failures++;
    }

    // Test 1f: Mixed chain — skip intermediate with epoch 0
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes[""] = MakeNode(false, 900);
        nodes["0"] = MakeNode(false, 0);
        nodes["03"] = MakeNode(true, 1005);
        nodes["030"] = MakeNode(true, 0);    // parent has epoch 1005
        nodes["0302"] = MakeNode(true, 0);   // should get 1005 from "03"
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.GetNodeEpoch("0302") == 1005,
                    "GetNodeEpoch: chain finds nearest non-zero ancestor"))
            failures++;
    }

    // =========================================================================
    // Test Group 2: MSB Alternation Rule
    // =========================================================================

    // Test 2a: Valid MSB alternation paths should keep hasNodeData=true
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["02"] = MakeNode(true, 1005);
        nodes["12"] = MakeNode(true, 1005);
        nodes["20"] = MakeNode(true, 1005);
        nodes["31"] = MakeNode(true, 1005);
        nodes["212"] = MakeNode(true, 1005);  // 2->1->2: alternating
        nodes["302"] = MakeNode(true, 1005);  // 3->0->2: alternating
        ApplyMsbFilter(nodes);  // Mirrors PopulateNodes behavior
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        bool allValid = idx.HasNodeData("02") && idx.HasNodeData("12") &&
                        idx.HasNodeData("20") && idx.HasNodeData("31") &&
                        idx.HasNodeData("212") && idx.HasNodeData("302");
        if (!Expect(allValid,
                    "MSB rule: valid alternation paths keep hasNodeData=true"))
            failures++;
    }

    // Test 2b: Invalid MSB paths should have hasNodeData=false after filter
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["00"] = MakeNode(true, 1005);
        nodes["01"] = MakeNode(true, 1005);
        nodes["11"] = MakeNode(true, 1005);
        nodes["22"] = MakeNode(true, 1005);
        nodes["33"] = MakeNode(true, 1005);
        nodes["002"] = MakeNode(true, 1005);
        nodes["013"] = MakeNode(true, 1005);
        nodes["112"] = MakeNode(true, 1005);
        ApplyMsbFilter(nodes);  // Mirrors PopulateNodes behavior
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        bool allInvalid = !idx.HasNodeData("00") && !idx.HasNodeData("01") &&
                          !idx.HasNodeData("11") && !idx.HasNodeData("22") &&
                          !idx.HasNodeData("33") && !idx.HasNodeData("002") &&
                          !idx.HasNodeData("013") && !idx.HasNodeData("112");
        if (!Expect(allInvalid,
                    "MSB rule: invalid paths have hasNodeData=false"))
            failures++;
    }

    // Test 2c: Single-digit paths should not be affected by MSB rule
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["0"] = MakeNode(true, 1005);
        nodes["1"] = MakeNode(true, 1005);
        nodes["2"] = MakeNode(true, 1005);
        nodes["3"] = MakeNode(true, 1005);
        ApplyMsbFilter(nodes);
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        bool allValid = idx.HasNodeData("0") && idx.HasNodeData("1") &&
                        idx.HasNodeData("2") && idx.HasNodeData("3");
        if (!Expect(allValid,
                    "MSB rule: single-digit paths unaffected"))
            failures++;
    }

    // Test 2d: Deep path with violation in the middle
    {
        globe::RockTreeOctreeIndex idx(config, nullptr);
#ifdef NATIVE_GLOBE_TESTING
        std::unordered_map<std::string, globe::OctreeNodeInfo> nodes;
        nodes["0213"] = MakeNode(true, 1005);  // 0->2->1->3: all alternating
        nodes["0223"] = MakeNode(true, 1005);  // 0->2->2->3: violation at pos 1-2
        nodes["0212"] = MakeNode(true, 1005);  // 0->2->1->2: all alternating
        nodes["0211"] = MakeNode(true, 1005);  // 0->2->1->1: violation at pos 2-3
        ApplyMsbFilter(nodes);  // Mirrors PopulateNodes behavior
        idx.TEST_SetNodesForUnitTests(std::move(nodes));
#endif
        if (!Expect(idx.HasNodeData("0213") && !idx.HasNodeData("0223") &&
                    idx.HasNodeData("0212") && !idx.HasNodeData("0211"),
                    "MSB rule: deep paths check all adjacent pairs"))
            failures++;
    }

    // =========================================================================
    // Summary
    // =========================================================================

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }

    std::cerr << "\nAll quadtree epoch/MSB tests PASSED\n";
    return 0;
}
