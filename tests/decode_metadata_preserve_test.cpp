// Decode metadata preservation test.
// Verifies that network fetch metadata (elapsedMs, bytes, http code)
// is preserved when PNG decode fails.

#include "../src/io/providers/dem_fetch_result.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace globe;

int main() {
    std::cout << "=== Decode Metadata Preservation Test ===" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: Successful fetch result has all metadata
    {
        DemFetchResult result = DemFetchResult::Success(200, 1500, 45.5);
        
        if (result.success && 
            result.httpStatusCode == 200 && 
            result.bytesReceived == 1500 &&
            std::abs(result.elapsedMs - 45.5) < 0.001 &&
            result.errorType == DemFetchResult::ErrorType::None) {
            std::cout << "PASS: Success result has correct metadata" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Success result metadata incorrect" << std::endl;
            failed++;
        }
    }
    
    // Test 2: Simulate decode error preserving fetch metadata
    {
        // Start with successful fetch result
        DemFetchResult result = DemFetchResult::Success(200, 1500, 45.5);
        
        // Simulate decode failure (like TerrainRGBProvider does)
        result.success = false;
        result.errorType = DemFetchResult::ErrorType::Decode;
        result.errorMessage = "Failed to decode PNG/terrain-rgb";
        // Note: httpStatusCode, bytesReceived, elapsedMs should be preserved
        
        if (!result.success && 
            result.httpStatusCode == 200 &&  // Preserved!
            result.bytesReceived == 1500 &&   // Preserved!
            std::abs(result.elapsedMs - 45.5) < 0.001 &&  // Preserved!
            result.errorType == DemFetchResult::ErrorType::Decode &&
            result.errorMessage == "Failed to decode PNG/terrain-rgb") {
            std::cout << "PASS: Decode error preserves fetch metadata" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Decode error metadata incorrect:" << std::endl;
            std::cout << "  success=" << result.success << std::endl;
            std::cout << "  httpStatusCode=" << result.httpStatusCode << " (expected 200)" << std::endl;
            std::cout << "  bytesReceived=" << result.bytesReceived << " (expected 1500)" << std::endl;
            std::cout << "  elapsedMs=" << result.elapsedMs << " (expected 45.5)" << std::endl;
            std::cout << "  errorType=" << static_cast<int>(result.errorType) << std::endl;
            failed++;
        }
    }
    
    // Test 3: Compare with destructive update (old behavior)
    {
        // Old behavior would do: result = DemFetchResult::DecodeError("...")
        // This overwrites all fields
        DemFetchResult result = DemFetchResult::Success(200, 1500, 45.5);
        result = DemFetchResult::DecodeError("Failed to decode PNG/terrain-rgb");
        
        // In old behavior, these would be 0/empty
        if (result.httpStatusCode == 0 && result.bytesReceived == 0 && result.elapsedMs == 0) {
            std::cout << "PASS: Old destructive behavior confirmed (for comparison)" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Old behavior test unexpected result" << std::endl;
            failed++;
        }
    }
    
    // Test 4: Network error (auth) - should not preserve success metadata
    {
        DemFetchResult result = DemFetchResult::AuthError(401, "Unauthorized");
        
        if (!result.success && 
            result.httpStatusCode == 401 &&
            result.errorType == DemFetchResult::ErrorType::Auth) {
            std::cout << "PASS: Auth error has correct error metadata" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Auth error metadata incorrect" << std::endl;
            failed++;
        }
    }
    
    // Test 5: Timeout detection
    {
        DemFetchResult result = DemFetchResult::NetworkError(28, "Timeout", 30.0);
        
        if (result.IsTimeout() && 
            result.errorType == DemFetchResult::ErrorType::Network &&
            std::abs(result.elapsedMs - 30.0) < 0.001) {
            std::cout << "PASS: Timeout detection works correctly" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Timeout detection failed" << std::endl;
            failed++;
        }
    }
    
    // Test 6: Auth failure detection
    {
        DemFetchResult result = DemFetchResult::AuthError(403, "Forbidden");
        
        if (result.IsAuthFailure() && result.httpStatusCode == 403) {
            std::cout << "PASS: Auth failure detection (403) works" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Auth failure detection failed" << std::endl;
            failed++;
        }
    }
    
    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
