// TileKey QuadKey Test
// Verifies Bing/GE compatible quadkey conversion
// Digit mapping: 0=NW, 1=NE, 2=SW, 3=SE

#include "../src/core/tile_key.h"
#include <iostream>
#include <cstring>

using namespace globe;

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

int main() {
    int failed = 0;

    std::cout << "=== TileKey QuadKey Test ===\n";

    // Level 1: 4 basic quadrants
    // (1,0,0) = NW = "0"
    TileKey z1_nw(1, 0, 0);
    failed += !Expect(z1_nw.ToQuadKey() == "0", "Z=1, (0,0) should be '0' (NW)");

    // (1,1,0) = NE = "1"
    TileKey z1_ne(1, 1, 0);
    failed += !Expect(z1_ne.ToQuadKey() == "1", "Z=1, (1,0) should be '1' (NE)");

    // (1,0,1) = SW = "2"
    TileKey z1_sw(1, 0, 1);
    failed += !Expect(z1_sw.ToQuadKey() == "2", "Z=1, (0,1) should be '2' (SW)");

    // (1,1,1) = SE = "3"
    TileKey z1_se(1, 1, 1);
    failed += !Expect(z1_se.ToQuadKey() == "3", "Z=1, (1,1) should be '3' (SE)");

    // Level 0: root
    TileKey z0(0, 0, 0);
    failed += !Expect(z0.ToQuadKey() == "", "Z=0 should return empty string");

    // Level 2: examples
    // (2,0,0) = "00" (NW of NW)
    TileKey z2_00(2, 0, 0);
    failed += !Expect(z2_00.ToQuadKey() == "00", "Z=2, (0,0) should be '00'");

    // (2,3,3) = "33" (SE of SE)
    TileKey z2_33(2, 3, 3);
    failed += !Expect(z2_33.ToQuadKey() == "33", "Z=2, (3,3) should be '33'");

    // (2,2,2) = "30" (SE of SE)
    // Path: x=2(10), y=2(10) -> bit0: (0,0)=NW(0), bit1: (1,1)=SE(3) -> "03" reverse "30"
    TileKey z2_30(2, 2, 2);
    failed += !Expect(z2_30.ToQuadKey() == "30", "Z=2, (2,2) should be '30'");

    // Property checks
    // quadkey.size() == level
    for (int z = 1; z <= 10; ++z) {
        int mid = (1 << (z - 1));  // Middle coordinate
        TileKey key(z, mid, mid);
        std::string qk = key.ToQuadKey();
        bool sizeOk = (qk.size() == static_cast<size_t>(z));
        bool charsOk = true;
        for (char c : qk) {
            if (c < '0' || c > '3') {
                charsOk = false;
                break;
            }
        }
        if (!sizeOk || !charsOk) {
            std::cerr << "FAILED: Z=" << z << " quadkey='" << qk 
                      << "' size=" << qk.size() << " (expected " << z << ")\n";
            failed++;
        }
    }

    // Additional property: Children() relationship
    // Parent's quadkey should be prefix of child's quadkey
    TileKey parent(2, 1, 2);  // "12"
    std::string parentQK = parent.ToQuadKey();
    auto children = parent.Children();
    bool childPrefixOk = true;
    for (const auto& child : children) {
        std::string childQK = child.ToQuadKey();
        if (childQK.size() != parentQK.size() + 1) {
            childPrefixOk = false;
            break;
        }
        if (childQK.substr(0, parentQK.size()) != parentQK) {
            childPrefixOk = false;
            break;
        }
    }
    failed += !Expect(childPrefixOk, "Children's quadkeys should extend parent's");

    if (failed == 0) {
        std::cout << "TileKeyQuadKeyTest PASSED\n";
        return 0;
    }

    std::cerr << "TileKeyQuadKeyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
