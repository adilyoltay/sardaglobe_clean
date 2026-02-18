// P1-4: DEM batch fetch configuration validation tests (standalone)

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
    std::cout << "=== P1-4 DEM Batch Config Test ===\n";
    
    // Test 1: Default values
    {
        Config config;
        Check(config.demBatchDefaultSize == 8, "Default demBatchDefaultSize is 8");
        Check(config.demBatchBackoffMs == 0, "Default demBatchBackoffMs is 0");
    }
    
    // Test 2: Batch size clamp (too low)
    {
        Config config;
        config.demBatchDefaultSize = 0;
        config.Validate();
        Check(config.demBatchDefaultSize == 1, "Batch size 0 clamped to 1");
    }
    
    // Test 3: Batch size clamp (too high)
    {
        Config config;
        config.demBatchDefaultSize = 500;
        config.Validate();
        Check(config.demBatchDefaultSize == 256, "Batch size 500 clamped to 256");
    }
    
    // Test 4: Valid batch size preserved
    {
        Config config;
        config.demBatchDefaultSize = 16;
        config.Validate();
        Check(config.demBatchDefaultSize == 16, "Valid batch size 16 preserved");
    }
    
    // Test 5: Backoff Ms non-negative
    {
        Config config;
        config.demBatchBackoffMs = -100;
        config.Validate();
        Check(config.demBatchBackoffMs == 0, "Negative backoff clamped to 0");
    }
    
    // Test 6: Valid backoff preserved
    {
        Config config;
        config.demBatchBackoffMs = 50;
        config.Validate();
        Check(config.demBatchBackoffMs == 50, "Valid backoff 50ms preserved");
    }
    
    std::cout << "\nResults: " << passCount << "/" << testCount << " tests passed\n";
    
    return testPassed ? 0 : 1;
}
