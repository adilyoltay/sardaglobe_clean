#include "../src/tile_key.h"
#include <iostream>
#include <cassert>
#include <string>

// Simple test framework
#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; \
        std::exit(1); \
    }

#define ASSERT_TRUE(a) \
    if (!(a)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " is false" << std::endl; \
        std::exit(1); \
    }

#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; \
        std::exit(1); \
    }

void TestTileKey() {
    std::cout << "Testing TileKey..." << std::endl;
    
    // Test construction
    TileKey k(3, 2, 5);
    ASSERT_EQ(k.level, 3);
    ASSERT_EQ(k.x, 2);
    ASSERT_EQ(k.y, 5);
    
    // Test ToString
    ASSERT_EQ(k.ToString(), "3/2/5");
    
    // Test FromString
    TileKey k2 = TileKey::FromString("3/2/5");
    ASSERT_EQ(k2, k);
    
    // Test QuadKey
    // Level 3, X=2 (010), Y=5 (101)
    // Bit 2: X=0, Y=1 -> 2
    // Bit 1: X=1, Y=0 -> 1
    // Bit 0: X=0, Y=1 -> 2
    // Expected: "212"
    ASSERT_EQ(k.ToQuadKey(), "212");
    
    TileKey k3 = TileKey::FromQuadKey("212");
    ASSERT_EQ(k3, k);
    
    // Test Parent
    TileKey parent = k.GetParent();
    ASSERT_EQ(parent.level, 2);
    ASSERT_EQ(parent.x, 1);
    ASSERT_EQ(parent.y, 2);
    
    // Test Children
    auto children = k.GetChildren();
    ASSERT_EQ(children[0].level, 4);
    ASSERT_EQ(children[0].x, 4);
    ASSERT_EQ(children[0].y, 10);
    
    ASSERT_EQ(children[3].level, 4);
    ASSERT_EQ(children[3].x, 5);
    ASSERT_EQ(children[3].y, 11);
}

void TestTileBounds() {
    std::cout << "Testing TileBounds..." << std::endl;
    
    // Root tile (0/0/0)
    TileKey root(0, 0, 0);
    TileBounds b = TileBounds::FromTileKey(root);
    
    ASSERT_NEAR(b.west, -180.0, 1e-6);
    ASSERT_NEAR(b.east, 180.0, 1e-6);
    // Web Mercator limits latitude to approx +/- 85.0511
    ASSERT_NEAR(b.north, 85.051128, 1e-4);
    ASSERT_NEAR(b.south, -85.051128, 1e-4);
    
    // Test center
    double lat, lon;
    b.GetCenter(lat, lon);
    ASSERT_NEAR(lat, 0.0, 1e-6);
    ASSERT_NEAR(lon, 0.0, 1e-6);
}

void TestTileUrlGenerator() {
    std::cout << "Testing TileUrlGenerator..." << std::endl;
    
    TileKey k(3, 2, 5);
    
    // Basic XYZ
    TileUrlGenerator gen1("http://server.com/{z}/{x}/{y}.png");
    ASSERT_EQ(gen1.GenerateUrl(k), "http://server.com/3/2/5.png");
    
    // QuadKey
    TileUrlGenerator gen2("http://server.com/q/{quadkey}.jpeg");
    ASSERT_EQ(gen2.GenerateUrl(k), "http://server.com/q/212.jpeg");
    
    // TMS (Inverted Y)
    // Level 3, max Y = 7. Y=5 -> Inverted Y = 2
    TileUrlGenerator gen3("http://server.com/{z}/{x}/{-y}.png");
    ASSERT_EQ(gen3.GenerateUrl(k), "http://server.com/3/2/2.png");
}

void TestTileMath() {
    std::cout << "Testing TileMath..." << std::endl;
    
    // 0,0 is in 0/0/0
    TileKey k = TileMath::LatLonToTileKey(0.0, 0.0, 0);
    ASSERT_EQ(k.level, 0);
    ASSERT_EQ(k.x, 0);
    ASSERT_EQ(k.y, 0);
    
    // Test slightly offset
    // 10, 10 at level 1 should be TR (1) quadrant -> x=1, y=0
    // Wait, lat 10 is north (upper half), lon 10 is east (right half)
    // Top-Right is x=1, y=0 (Mercator y=0 is top)
    TileKey k1 = TileMath::LatLonToTileKey(10.0, 10.0, 1);
    ASSERT_EQ(k1.level, 1);
    ASSERT_EQ(k1.x, 1);
    ASSERT_EQ(k1.y, 0); // Correct, Mercator Y increases downwards from North
}

int main() {
    TestTileKey();
    TestTileBounds();
    TestTileUrlGenerator();
    TestTileMath();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
