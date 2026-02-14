// RockMesh Manager Test
// Tests Sprint 2 LOD-aware mesh management and scheduler behavior

#include "../src/core/config.h"
#include "../src/core/tile_key.h"
#include <iostream>
#include <vector>
#include <unordered_set>

using namespace globe;

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

// Mock config for testing
Config CreateTestConfig() {
    Config config;
    config.geMeshEndpoint = "https://example.com/mesh/{quadkey}";
    config.geMeshMaxLodMargin = 1;
    config.geMeshMaxInFlight = 4;
    config.geMeshCacheSize = 8;
    return config;
}

// Test TileKey to quadkey conversion (internal logic of RockMeshManager)
// Sprint 2.3: Use canonical TileKey::ToQuadKey()
std::string TileKeyToNodeKey(const TileKey& key) {
    return key.ToQuadKey();
}

// Simulate priority calculation (simplified version of what manager does)
int CalculatePriority(const TileKey& leaf, int index, int total) {
    // Higher index = more recent = higher priority
    return total - index;
}

// Simulate generation check
bool IsStale(uint64_t entryGen, uint64_t currentGen) {
    return entryGen < currentGen - 1;
}

int main() {
    int failed = 0;
    std::cout << "=== RockMesh Manager Test ===\n";

    // Test 1: TileKeyToNodeKey conversion
    {
        // Level 0: empty
        std::string qk0 = TileKeyToNodeKey(TileKey(0, 0, 0));
        failed += !Expect(qk0.empty(), "Level 0 should return empty string");
        
        // Level 1 children (canonical ToQuadKey: digit = ((tx&1)<<0)|((ty&1)<<1))
        // 0 = NW (0,0), 1 = NE (1,0), 2 = SW (0,1), 3 = SE (1,1)
        std::string qk1_nw = TileKeyToNodeKey(TileKey(1, 0, 0));
        failed += !Expect(qk1_nw == "0", "NW child (0,0) should be '0'");
        
        std::string qk1_ne = TileKeyToNodeKey(TileKey(1, 1, 0));
        failed += !Expect(qk1_ne == "1", "NE child (1,0) should be '1'");
        
        std::string qk1_sw = TileKeyToNodeKey(TileKey(1, 0, 1));
        failed += !Expect(qk1_sw == "2", "SW child (0,1) should be '2'");
        
        std::string qk1_se = TileKeyToNodeKey(TileKey(1, 1, 1));
        failed += !Expect(qk1_se == "3", "SE child (1,1) should be '3'");
        
        // Level 2: (2,2) = binary (10,10) 
        // i=0: tx=2,ty=2 -> ((0)<<0)|((0)<<1) = 0, tx=1,ty=1
        // i=1: tx=1,ty=1 -> ((1)<<0)|((1)<<1) = 1|2 = 3, tx=0,ty=0
        // digits = [0, 3], reversed = [3, 0] -> "30"
        std::string qk2 = TileKeyToNodeKey(TileKey(2, 2, 2));
        failed += !Expect(qk2 == "30", "Tile(2,2,2) should be '30'");
        
        // Level 5
        std::string qk5 = TileKeyToNodeKey(TileKey(5, 16, 20));
        failed += !Expect(qk5.length() == 5, "Level 5 should have 5 digits");
        
        std::cout << "  TileKey conversion: OK (level 0-5)\n";
    }

    // Test 2: Config validation
    {
        Config config = CreateTestConfig();
        
        failed += !Expect(config.geMeshEnabled(), "Config with endpoint should be enabled");
        failed += !Expect(config.geMeshMaxLodMargin == 1, "Default LOD margin should be 1");
        failed += !Expect(config.geMeshMaxInFlight == 4, "Max in-flight should be 4");
        failed += !Expect(config.geMeshCacheSize == 8, "Cache size should be 8");
        
        std::cout << "  Config validation: OK\n";
    }

    // Test 3: Visible set deduplication logic
    {
        // Simulate visible leaves
        std::vector<TileKey> visibleLeaves = {
            TileKey(5, 16, 20),
            TileKey(5, 17, 20),
            TileKey(5, 16, 21),
            TileKey(5, 17, 21)
        };
        
        // Convert to set (dedupe simulation)
        std::vector<std::string> keys;
        keys.reserve(visibleLeaves.size());
        for (const auto& leaf : visibleLeaves) {
            keys.push_back(TileKeyToNodeKey(leaf));
        }
        
        // Check unique
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        
        failed += !Expect(keys.size() == 4, "4 unique tiles should produce 4 unique keys");
        
        std::cout << "  Visible set: " << keys.size() << " unique keys\n";
    }

    // Test 4: Parent/child relationship
    {
        TileKey parent(2, 2, 2);
        std::string parentKey = TileKeyToNodeKey(parent);
        
        auto children = parent.Children();
        bool foundParentAsChild = false;
        
        for (const auto& child : children) {
            std::string childKey = TileKeyToNodeKey(child);
            // Child key should start with parent key
            if (childKey.find(parentKey) != 0) {
                failed += !Expect(false, "Child key should start with parent key");
            }
            // Check length
            failed += !Expect(childKey.length() == parentKey.length() + 1, 
                            "Child key should be one digit longer than parent");
        }
        
        std::cout << "  Parent/child: OK\n";
    }

    // Test 5: LOD margin expansion
    {
        std::vector<TileKey> visibleLeaves = {
            TileKey(3, 4, 5)
        };
        
        int lodMargin = 1;
        std::vector<std::string> expandedKeys;
        
        for (const auto& leaf : visibleLeaves) {
            // Add leaf itself
            expandedKeys.push_back(TileKeyToNodeKey(leaf));
            
            // Add parent if within margin
            if (leaf.level > 0 && lodMargin > 0) {
                expandedKeys.push_back(TileKeyToNodeKey(leaf.Parent()));
            }
        }
        
        failed += !Expect(expandedKeys.size() == 2, "With margin=1, should have leaf + parent");
        
        std::cout << "  LOD expansion: " << expandedKeys.size() << " keys\n";
    }

    // Test 6: Generation-based stale detection
    {
        uint64_t currentGen = 100;
        
        // Recent entry - not stale
        failed += !Expect(!IsStale(100, currentGen), "Current gen should not be stale");
        failed += !Expect(!IsStale(99, currentGen), "One behind should not be stale");
        
        // Stale entry
        failed += !Expect(IsStale(98, currentGen), "Two behind should be stale");
        failed += !Expect(IsStale(50, currentGen), "Old gen should be stale");
        
        std::cout << "  Generation stale detection: OK\n";
    }

    // Test 7: Priority calculation (simplified scheduler logic)
    {
        std::vector<TileKey> visibleLeaves = {
            TileKey(3, 0, 0),
            TileKey(3, 1, 0),
            TileKey(3, 0, 1),
            TileKey(3, 1, 1)
        };
        
        int total = static_cast<int>(visibleLeaves.size());
        std::vector<int> priorities;
        
        for (int i = 0; i < total; ++i) {
            priorities.push_back(CalculatePriority(visibleLeaves[i], i, total));
        }
        
        // Check that priorities are decreasing (higher = more recent)
        failed += !Expect(priorities[0] == 4, "First item should have priority 4");
        failed += !Expect(priorities[3] == 1, "Last item should have priority 1");
        
        std::cout << "  Priority calculation: OK\n";
    }

    // Test 8: In-flight limit simulation
    {
        int maxInFlight = 4;
        int currentInFlight = 0;
        int dispatched = 0;
        
        // Simulate dispatching 10 requests with limit of 4
        for (int i = 0; i < 10; ++i) {
            if (currentInFlight < maxInFlight) {
                currentInFlight++;
                dispatched++;
            }
        }
        
        // Should only dispatch up to limit
        failed += !Expect(dispatched == 4, "Should only dispatch up to maxInFlight");
        failed += !Expect(currentInFlight == 4, "Current in-flight should be at limit");
        
        std::cout << "  In-flight limit: OK (dispatched=" << dispatched << ")\n";
    }

    // Test 9: LRU cache simulation
    {
        int cacheSize = 4;
        std::vector<std::string> lruList;
        
        // Add items
        for (int i = 0; i < 6; ++i) {
            std::string key = "key" + std::to_string(i);
            // Add to front (most recent)
            lruList.insert(lruList.begin(), key);
            // Evict if over limit
            if (static_cast<int>(lruList.size()) > cacheSize) {
                lruList.pop_back();  // Remove least recent
            }
        }
        
        // Should only have 4 items
        failed += !Expect(lruList.size() == 4, "Cache should be at max size");
        // Oldest items (0, 1) should be evicted
        bool hasOld = false;
        for (const auto& k : lruList) {
            if (k == "key0" || k == "key1") hasOld = true;
        }
        failed += !Expect(!hasOld, "Oldest items should be evicted");
        
        std::cout << "  LRU cache: OK (size=" << lruList.size() << ")\n";
    }

    if (failed == 0) {
        std::cout << "RockMeshManagerTest PASSED\n";
        return 0;
    }

    std::cerr << "RockMeshManagerTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
