// GE Mesh URL Template Test
// Verifies URL template placeholder replacement for RockTree/NodeData

#include "../src/io/ge_mesh_url_template.h"
#include "../src/core/tile_key.h"
#include <iostream>
#include <string>

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

    std::cout << "=== GE Mesh URL Template Test ===\n";

    // Test 1: QuadKey only template
    {
        std::string tmpl = "https://example.com/mesh?pb=!1s{quadkey}!2e1";
        TileKey key(5, 8, 12);
        std::string nodeKey = key.ToQuadKey();  // Sprint 1: still uses TileKey for test convenience
        std::string url = BuildGeMeshUrl(tmpl, nodeKey);
        std::string expected = "https://example.com/mesh?pb=!1s" + nodeKey + "!2e1";
        failed += !Expect(url == expected, "QuadKey template replacement failed");
        std::cout << "  QuadKey template: " << url << "\n";
    }

    // Test 2: Multiple placeholders
    {
        std::string tmpl = "https://example.com/{z}/{x}/{y}?qk={quadkey}";
        TileKey key(10, 512, 340);
        std::string nodeKey = key.ToQuadKey();
        std::string url = BuildGeMeshUrl(tmpl, nodeKey, key.level, key.x, key.y);
        std::string expected = "https://example.com/10/512/340?qk=" + nodeKey;
        failed += !Expect(url == expected, "Multi-placeholder template failed");
        std::cout << "  Multi template: " << url << "\n";
    }

    // Test 3: Level 0 (empty quadkey) - valid for global root
    {
        std::string tmpl = "https://example.com/mesh/{quadkey}";
        std::string url = BuildGeMeshUrl(tmpl, "");  // Empty nodeKey is valid
        std::string expected = "https://example.com/mesh/";
        failed += !Expect(url == expected, "Empty quadkey should produce valid URL");
        std::cout << "  Empty nodeKey: " << url << "\n";
    }

    // Test 4: No optional z/x/y provided (they remain unreplaced in template)
    {
        std::string tmpl = "https://example.com/static/mesh.bin?k={quadkey}";
        std::string url = BuildGeMeshUrl(tmpl, "1234");  // z omitted, defaults to -1
        std::string expected = "https://example.com/static/mesh.bin?k=1234";
        failed += !Expect(url == expected, "Template without z/x/y should only replace {quadkey}");
        std::cout << "  Static (no zxy): " << url << "\n";
    }

    // Test 5: Realistic GE-style URL
    {
        std::string tmpl = "https://kh.google.com/rpc/NodeData?pb=!1s{quadkey}!2e1!4e0";
        TileKey key(15, 16384, 10000);
        std::string nodeKey = key.ToQuadKey();
        std::string url = BuildGeMeshUrl(tmpl, nodeKey);
        std::string expectedQk = nodeKey;
        failed += !Expect(url.find(expectedQk) != std::string::npos, 
                         "GE-style URL should contain quadkey");
        std::cout << "  GE-style: " << url << "\n";
    }

    // Test 6: Multiple occurrences of same placeholder
    {
        std::string tmpl = "https://example.com/{quadkey}/data/{quadkey}/mesh.bin";
        std::string url = BuildGeMeshUrl(tmpl, "5678");
        std::string expected = "https://example.com/5678/data/5678/mesh.bin";
        failed += !Expect(url == expected, "Multiple {quadkey} occurrences should all be replaced");
        std::cout << "  Multi-occurrence: " << url << "\n";
    }

    // Test 7: ValidateGeMeshEndpointTemplate - valid template
    {
        std::string err;
        bool valid = ValidateGeMeshEndpointTemplate("https://x/{quadkey}", err);
        failed += !Expect(valid, "Valid template should pass validation");
        failed += !Expect(err.empty(), "Valid template should have no error");
    }

    // Test 8: ValidateGeMeshEndpointTemplate - missing {quadkey}
    {
        std::string err;
        bool valid = ValidateGeMeshEndpointTemplate("https://x/{z}/{x}/{y}", err);
        failed += !Expect(!valid, "Missing {quadkey} should fail validation");
        failed += !Expect(err.find("{quadkey}") != std::string::npos, 
                         "Error should mention {quadkey}");
        std::cout << "  Missing quadkey error: " << err << "\n";
    }

    // Test 9: ValidateGeMeshEndpointTemplate - unknown placeholder
    {
        std::string err;
        bool valid = ValidateGeMeshEndpointTemplate("https://x/{quadkey}/{typo}", err);
        failed += !Expect(!valid, "Unknown placeholder should fail validation");
        failed += !Expect(err.find("{typo}") != std::string::npos, 
                         "Error should mention unknown placeholder");
        std::cout << "  Unknown placeholder error: " << err << "\n";
    }

    // Test 10: ValidateGeMeshEndpointTemplate - unclosed brace
    {
        std::string err;
        bool valid = ValidateGeMeshEndpointTemplate("https://x/{quadkey}/{unclosed", err);
        failed += !Expect(!valid, "Unclosed brace should fail validation");
        failed += !Expect(err.find("Unclosed") != std::string::npos, 
                         "Error should mention 'Unclosed brace'");
        std::cout << "  Unclosed brace error: " << err << "\n";
    }

    // Test 11: Sprint 1 NodeData key (direct string, NOT TileKey)
    {
        std::string tmpl = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "1234567";  // Raw octal key from CLI
        std::string url = BuildGeMeshUrl(tmpl, nodeKey);
        std::string expected = "https://example.com/mesh/1234567";
        failed += !Expect(url == expected, "Direct nodeKey string should work");
        std::cout << "  Direct nodeKey: " << url << "\n";
    }

    if (failed == 0) {
        std::cout << "GeMeshUrlTemplateTest PASSED\n";
        return 0;
    }

    std::cerr << "GeMeshUrlTemplateTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
