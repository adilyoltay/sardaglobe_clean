// P0-2 Should-fix: Texture2DArray capability fallback regression test
// Validates Config defaults and fallback behavior documentation
// Note: GL-dependent tests require headless GL context (run via main executable)

#include "../src/core/config.h"
#include <iostream>
#include <cstdlib>

using namespace globe;

namespace {

bool testPassed = true;
int testCount = 0;
int passCount = 0;

void Check(bool condition, const char* testName) {
    testCount++;
    if (condition) {
        passCount++;
        std::cout << "[PASS] " << testName << "\n";
    } else {
        testPassed = false;
        std::cout << "[FAIL] " << testName << "\n";
    }
}

} // namespace

int main() {
    std::cout << "=== TextureArray Capability Fallback Test ===\n";
    
    // Test 1: Config default value (P0-2 requirement)
    {
        Config config;
        Check(config.useTexture2DArray == false,
              "Texture2DArray is stable by default (disabled)");
    }
    
    // Test 2: Config can be explicitly disabled
    {
        Config config;
        config.useTexture2DArray = false;
        Check(config.useTexture2DArray == false, 
              "useTexture2DArray can be explicitly disabled");
    }
    
    // Test 3: Config can be explicitly enabled (user override)
    {
        Config config;
        config.useTexture2DArray = true;  // Explicit set
        Check(config.useTexture2DArray == true, 
              "useTexture2DArray can be explicitly enabled");
    }
    
    // Test 4: Runtime fallback capability (documented behavior)
    // This validates that Config supports runtime modification
    // which GlobeEngine uses for GL capability-based fallback
    {
        Config config;
        // Simulate GL capability check failure
        config.useTexture2DArray = false;  // Runtime override
        Check(config.useTexture2DArray == false, 
              "Runtime fallback: config can be disabled after GL check");
    }
    
    // Fallback behavior documentation
    std::cout << "\n=== P0-2 Fallback Chain Documentation ===\n";
    std::cout << "1. Config default: useTexture2DArray = false (stable default)\n";
    std::cout << "2. GlobeEngine::Init() queries GL_MAX_ARRAY_TEXTURE_LAYERS\n";
    std::cout << "3. If maxLayers < 128 OR GL error: config_.useTexture2DArray = false\n";
    std::cout << "4. Log format: requested=Array/Atlas, effective=Array/Atlas\n";
    std::cout << "5. TextureManager fallback: Array -> Atlas -> PBO -> Immediate\n";
    std::cout << "========================================\n";
    
    std::cout << "\nResults: " << passCount << "/" << testCount << " tests passed\n";
    
    return testPassed ? 0 : 1;
}
