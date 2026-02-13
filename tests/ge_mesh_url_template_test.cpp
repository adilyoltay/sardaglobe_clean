// GE Mesh URL Template Test
// Verifies URL template placeholder replacement for RockTree/NodeData

#include "../src/core/tile_key.h"
#include "../src/core/config.h"
#include <iostream>
#include <string>

using namespace globe;

// Simple URL template replacer (minimal implementation for test)
std::string ReplaceUrlTemplate(const std::string& templateUrl, const TileKey& key) {
    std::string result = templateUrl;
    
    // Replace {quadkey}
    std::string qk = key.ToQuadKey();
    size_t pos;
    while ((pos = result.find("{quadkey}")) != std::string::npos) {
        result.replace(pos, 9, qk);
    }
    
    // Replace {z}
    while ((pos = result.find("{z}")) != std::string::npos) {
        result.replace(pos, 3, std::to_string(key.level));
    }
    
    // Replace {x}
    while ((pos = result.find("{x}")) != std::string::npos) {
        result.replace(pos, 3, std::to_string(key.x));
    }
    
    // Replace {y}
    while ((pos = result.find("{y}")) != std::string::npos) {
        result.replace(pos, 3, std::to_string(key.y));
    }
    
    return result;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

int main() {
    int failed = 0;

    std::cout << "=== GE Mesh URL Template Test ===\n";

    // Test 1: QuadKey only template
    {
        std::string tmpl = "https://example.com/mesh?pb=!1s{quadkey}!2e1";
        TileKey key(5, 8, 12);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        std::string expected = "https://example.com/mesh?pb=!1s" + key.ToQuadKey() + "!2e1";
        failed += !Expect(url == expected, "QuadKey template replacement failed");
        std::cout << "  QuadKey template: " << url << "\n";
    }

    // Test 2: Multiple placeholders
    {
        std::string tmpl = "https://example.com/{z}/{x}/{y}?qk={quadkey}";
        TileKey key(10, 512, 340);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        std::string expected = "https://example.com/10/512/340?qk=" + key.ToQuadKey();
        failed += !Expect(url == expected, "Multi-placeholder template failed");
        std::cout << "  Multi template: " << url << "\n";
    }

    // Test 3: Level 0 (empty quadkey)
    {
        std::string tmpl = "https://example.com/mesh/{quadkey}";
        TileKey key(0, 0, 0);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        std::string expected = "https://example.com/mesh/";
        failed += !Expect(url == expected, "Level 0 quadkey should be empty");
        std::cout << "  Level 0: " << url << "\n";
    }

    // Test 4: No placeholders (passthrough)
    {
        std::string tmpl = "https://example.com/static/mesh.bin";
        TileKey key(3, 2, 2);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        failed += !Expect(url == tmpl, "No-placeholder template should pass through");
        std::cout << "  Static: " << url << "\n";
    }

    // Test 5: Realistic GE-style URL
    {
        std::string tmpl = "https://kh.google.com/rpc/NodeData?pb=!1s{quadkey}!2e1!4e0";
        TileKey key(15, 16384, 10000);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        std::string expectedQk = key.ToQuadKey();
        failed += !Expect(url.find(expectedQk) != std::string::npos, 
                         "GE-style URL should contain quadkey");
        std::cout << "  GE-style: " << url << "\n";
    }

    // Test 6: Missing {quadkey} placeholder (fail-fast policy check)
    // In real implementation, this should log error and disable mesh fetch
    {
        std::string tmpl = "https://example.com/mesh/{z}/{x}/{y}";
        TileKey key(8, 128, 128);
        std::string url = ReplaceUrlTemplate(tmpl, key);
        // Template without {quadkey} should still work (just no quadkey)
        // but real code should validate this
        bool hasQuadKey = (url.find(key.ToQuadKey()) != std::string::npos);
        failed += !Expect(!hasQuadKey, "Template without {quadkey} should not contain quadkey");
    }

    if (failed == 0) {
        std::cout << "GeMeshUrlTemplateTest PASSED\n";
        return 0;
    }

    std::cerr << "GeMeshUrlTemplateTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
