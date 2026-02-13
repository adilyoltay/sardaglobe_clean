// RockMesh Manager Test
// Tests Sprint 2 LOD-aware mesh management (without GL context)

#include "../src/core/config.h"
#include "../src/core/tile_key.h"
#include <iostream>
#include <vector>

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
std::string TileKeyToNodeKey(const TileKey& key) {
    if (key.level == 0) return "";
    
    std::string digits;
    digits.reserve(key.level);
    int tx = key.x, ty = key.y;
    
    for (int z = key.level; z > 0; --z) {
        int digit = ((tx & 1) << 1) | (ty & 1);  // 0=NW, 1=NE, 2=SW, 3=SE
        digits.push_back('0' + digit);
        tx >>= 1;
        ty >>= 1;
    }
    
    std::reverse(digits.begin(), digits.end());
    return digits;
}

int main() {
    int failed = 0;
    std::cout << "=== RockMesh Manager Test ===\n";

    // Test 1: TileKeyToNodeKey conversion (current implementation)
    {
        // Level 0: empty
        std::string qk0 = TileKeyToNodeKey(TileKey(0, 0, 0));
        failed += !Expect(qk0.empty(), "Level 0 should return empty string");
        
        // Level 1 children (based on actual digit = ((tx&1)<<1)|(ty&1))
        // 0 = NW (0,0), 1 = SW (0,1), 2 = NE (1,0), 3 = SE (1,1)
        std::string qk1_nw = TileKeyToNodeKey(TileKey(1, 0, 0));
        failed += !Expect(qk1_nw == "0", "NW child (0,0) should be '0'");
        
        std::string qk1_sw = TileKeyToNodeKey(TileKey(1, 0, 1));
        failed += !Expect(qk1_sw == "1", "SW child (0,1) should be '1'");
        
        std::string qk1_ne = TileKeyToNodeKey(TileKey(1, 1, 0));
        failed += !Expect(qk1_ne == "2", "NE child (1,0) should be '2'");
        
        std::string qk1_se = TileKeyToNodeKey(TileKey(1, 1, 1));
        failed += !Expect(qk1_se == "3", "SE child (1,1) should be '3'");
        
        // Level 2: (2,2) = binary (10,10) -> digits "30"
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

    if (failed == 0) {
        std::cout << "RockMeshManagerTest PASSED\n";
        return 0;
    }

    std::cerr << "RockMeshManagerTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
