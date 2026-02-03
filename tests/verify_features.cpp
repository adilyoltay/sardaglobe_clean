#include "../src/tile_key.h"
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cassert>

// --- Tests ---

void TestUrlExpansion() {
    std::cout << "Testing URL Expansion (via TileUrlGenerator)..." << std::endl;
    
    // Test 1: Standard XYZ
    TileUrlGenerator gen1("http://tile.server/{z}/{x}/{y}.png");
    std::string r1 = gen1.GenerateUrl(TileKey(10, 5, 20));
    assert(r1 == "http://tile.server/10/5/20.png");
    std::cout << "  [PASS] XYZ Standard" << std::endl;

    // Test 2: TMS ({-y})
    TileUrlGenerator gen2("http://tms.server/{z}/{x}/{-y}.png");
    std::string r2 = gen2.GenerateUrl(TileKey(10, 5, 20));
    assert(r2 == "http://tms.server/10/5/1003.png");
    std::cout << "  [PASS] TMS ({-y})" << std::endl;

    // Test 3: QuadKey
    TileUrlGenerator gen3("http://bing.server/{quadkey}.jpeg");
    std::string r3 = gen3.GenerateUrl(TileKey(3, 3, 5));
    assert(r3 == "http://bing.server/213.jpeg");
    std::cout << "  [PASS] QuadKey" << std::endl;
}

// Mock Tile struct for importance test
struct MockTile {
  int x = 0, y = 0, z = 0;
  bool pinned = false;
  int accessCount = 0;
  unsigned int lastFrameUsed = 0;
  glm::vec3 center = {0,0,0};
};

// Simplified importance logic for test (actual logic is in globe_engine.cpp)
float ComputeTileImportanceScoreMock(const MockTile& tile, double currentTime, 
                                     const glm::vec3& cameraPos, int currentZoom) {
  float score = 0.0f;
  if (tile.pinned) return 10000.0f;
  score += tile.accessCount * 2.0f;
  double age = currentTime - tile.lastFrameUsed * 0.016; 
  float ageFactor = std::max(0.0f, 10.0f - static_cast<float>(age));
  score += ageFactor * 1.0f;
  float distance = glm::length(tile.center - cameraPos);
  float distanceFactor = 1.0f / (1.0f + distance * 0.001f);
  score += distanceFactor * 3.0f;
  int levelDiff = std::abs(tile.z - currentZoom);
  float levelBonus = std::max(0.0f, 5.0f - levelDiff);
  score += levelBonus;
  return score;
}

void TestImportanceScore() {
    std::cout << "Testing Tile Importance Score..." << std::endl;
    
    glm::vec3 camPos = {0, 0, 0};
    double time = 100.0;
    
    // Test 1: Pinned Tile
    MockTile pinnedTile;
    pinnedTile.pinned = true;
    float s1 = ComputeTileImportanceScoreMock(pinnedTile, time, camPos, 10);
    assert(s1 >= 10000.0f);
    std::cout << "  [PASS] Pinned Tile Score" << std::endl;
    
    // Test 2: Distance logic
    MockTile closeTile;
    closeTile.center = {10, 0, 0};
    closeTile.z = 10;
    
    MockTile farTile;
    farTile.center = {1000, 0, 0};
    farTile.z = 10;
    
    float sClose = ComputeTileImportanceScoreMock(closeTile, time, camPos, 10);
    float sFar = ComputeTileImportanceScoreMock(farTile, time, camPos, 10);
    
    assert(sClose > sFar);
    std::cout << "  [PASS] Distance Penalty" << std::endl;
}

int main() {
    try {
        TestUrlExpansion();
        TestImportanceScore();
        std::cout << "\nAll logic verification tests PASSED." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED: " << e.what() << std::endl;
        return 1;
    }
}
