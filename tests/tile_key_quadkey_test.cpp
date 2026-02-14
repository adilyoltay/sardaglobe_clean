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

    // Sprint 3: CenterEcef() test
    {
        // Level 0: center should be (0,0,0) or near origin
        TileKey z0(0, 0, 0);
        glm::dvec3 center0 = z0.CenterEcef();
        // Level 0 covers whole earth, center could be defined as (R,0,0) or similar
        // Just verify it doesn't crash and returns finite values
        bool center0Valid = std::isfinite(center0.x) && std::isfinite(center0.y) && std::isfinite(center0.z);
        failed += !Expect(center0Valid, "Level 0 center should be finite");
        
        // Level 1: NW corner tile should be in positive X, negative Y hemisphere (roughly)
        TileKey z1_nw(1, 0, 0);  // NW = "0"
        glm::dvec3 center1 = z1_nw.CenterEcef();
        bool center1Valid = std::isfinite(center1.x) && std::isfinite(center1.y) && std::isfinite(center1.z);
        failed += !Expect(center1Valid, "Level 1 center should be finite");
        
        // Verify magnitude is approximately Earth radius
        double magnitude = glm::length(center1);
        bool radiusOk = magnitude > 6000.0 && magnitude < 7000.0;  // ~6378 km
        failed += !Expect(radiusOk, "Center magnitude should be ~Earth radius (km)");
        
        // Test CenterEcefMeters() is 1000x CenterEcef()
        glm::dvec3 centerMeters = z1_nw.CenterEcefMeters();
        glm::dvec3 expectedMeters = center1 * 1000.0;
        bool metersOk = glm::length(centerMeters - expectedMeters) < 0.001;
        failed += !Expect(metersOk, "CenterEcefMeters should be 1000x CenterEcef");
        
        std::cout << "  CenterEcef: magnitude=" << magnitude << " km\n";
    }

    if (failed == 0) {
        std::cout << "TileKeyQuadKeyTest PASSED\n";
        return 0;
    }

    std::cerr << "TileKeyQuadKeyTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
