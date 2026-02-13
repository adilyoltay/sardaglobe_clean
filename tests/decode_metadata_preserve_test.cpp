// Decode metadata preservation test.
// 
// IMPORTANT: This is a UNIT TEST of the DemFetchResult structure and its
// helper methods. It does NOT test the full production flow through
// TerrainRGBProvider::FetchDemTile.
//
// For integration testing of the actual decode error path with metadata
// preservation, a mock HTTP server returning 200 + invalid PNG would be needed.
// This test ensures the result structure correctly supports the preservation
// semantics expected by the production code.

#include "../src/io/providers/dem_fetch_result.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace globe;

int main() {
    std::cout << "=== Decode Metadata Preservation Test (Unit) ===" << std::endl;
    std::cout << "Note: This tests DemFetchResult structure, not full provider flow" << std::endl;
    std::cout << std::endl;
    
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
    
    // Test 2: Simulate the production decode error preservation pattern
    // This mirrors what TerrainRGBProvider::FetchDemTile does on line ~194
    {
        // Start with successful fetch result (as returned by HttpFetch)
        DemFetchResult result = DemFetchResult::Success(200, 1500, 45.5);
        
        // Simulate decode failure - ONLY update error fields, preserve metadata
        // This is the key pattern: don't do "result = DecodeError(...)" which
        // would overwrite everything
        result.success = false;
        result.errorType = DemFetchResult::ErrorType::Decode;
        result.errorMessage = "Failed to decode PNG/terrain-rgb";
        // httpStatusCode, bytesReceived, elapsedMs intentionally NOT modified
        
        if (!result.success && 
            result.httpStatusCode == 200 &&  // Preserved!
            result.bytesReceived == 1500 &&   // Preserved!
            std::abs(result.elapsedMs - 45.5) < 0.001 &&  // Preserved!
            result.errorType == DemFetchResult::ErrorType::Decode &&
            result.errorMessage == "Failed to decode PNG/terrain-rgb") {
            std::cout << "PASS: Decode error preserves fetch metadata (production pattern)" << std::endl;
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
    
    // Test 3: Demonstrate the OLD (bad) behavior that would lose metadata
    {
        DemFetchResult result = DemFetchResult::Success(200, 1500, 45.5);
        
        // BAD: This overwrites all fields including metadata
        result = DemFetchResult::DecodeError("Failed to decode PNG/terrain-rgb");
        
        if (result.httpStatusCode == 0 && result.bytesReceived == 0 && result.elapsedMs == 0) {
            std::cout << "PASS: Old destructive behavior confirmed (for regression comparison)" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Old behavior test unexpected result" << std::endl;
            failed++;
        }
    }
    
    // Test 4: Network error (auth) - should have error-specific metadata
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
    
    // Test 5: Timeout detection with CURL code 28
    {
        DemFetchResult result = DemFetchResult::NetworkError(28, "Timeout", 30.0);
        
        if (result.IsTimeout() && 
            result.errorType == DemFetchResult::ErrorType::Network &&
            std::abs(result.elapsedMs - 30.0) < 0.001) {
            std::cout << "PASS: Timeout detection works correctly (CURL 28)" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Timeout detection failed" << std::endl;
            failed++;
        }
    }
    
    // Test 6: Auth failure detection via HTTP status
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
    
    // Test 7: IsAuthFailure also works with 401
    {
        DemFetchResult result = DemFetchResult::AuthError(401, "Unauthorized");
        
        if (result.IsAuthFailure() && result.httpStatusCode == 401) {
            std::cout << "PASS: Auth failure detection (401) works" << std::endl;
            passed++;
        } else {
            std::cout << "FAIL: Auth failure detection (401) failed" << std::endl;
            failed++;
        }
    }
    
    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: These unit tests verify the result structure supports" << std::endl;
    std::cout << "metadata preservation. Full integration testing requires a" << std::endl;
    std::cout << "mock HTTP server that returns 200 + invalid PNG data." << std::endl;
    
    return failed > 0 ? 1 : 0;
}
