// Google Earth Elevation Provider Test
// Phase 4: Network-isolated unit tests using stub transport
//
// Covers:
// - 200 OK with valid protobuf response
// - 401/403 Auth errors
// - Network timeout (curl=28)
// - Decode failures (malformed protobuf)
// - Response size validation

#include "../src/io/providers/google_earth_elevation_provider.h"
#include "../src/io/providers/http_transport.h"
#include <iostream>
#include <cstring>

using namespace globe;

// Stub HTTP transport for deterministic testing
class StubHttpTransport : public IHttpTransport {
public:
    // Configure response
    void SetResponse(const HttpResponse& response) {
        nextResponse_ = response;
    }
    
    void SetNextResponse(long httpCode, const std::vector<uint8_t>& body, 
                         const std::string& errorMsg = "", bool success = true) {
        nextResponse_.httpCode = httpCode;
        nextResponse_.body = body;
        nextResponse_.errorMessage = errorMsg;
        nextResponse_.success = success;
        nextResponse_.elapsedMs = 50.0;
    }
    
    // Record last request for verification
    std::string lastUrl;
    std::vector<uint8_t> lastBody;
    std::vector<std::pair<std::string, std::string>> lastHeaders;
    
    HttpResponse Post(const std::string& url,
                      const std::vector<uint8_t>& requestBody,
                      const std::vector<std::pair<std::string, std::string>>& headers) override {
        lastUrl = url;
        lastBody = requestBody;
        lastHeaders = headers;
        return nextResponse_;
    }
    
    HttpResponse Get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers) override {
        (void)url;
        (void)headers;
        return nextResponse_;
    }

private:
    HttpResponse nextResponse_;
};

// Build synthetic protobuf elevation response
// Format: field 1 (elevations) = packed repeated double
std::vector<uint8_t> BuildSyntheticElevationResponse(const std::vector<double>& elevations) {
    std::vector<uint8_t> result;
    
    // Field 1, wire type 2 (length-delimited) = (1 << 3) | 2 = 0x0A
    result.push_back(0x0A);
    
    // Length = 8 bytes per double
    size_t length = elevations.size() * 8;
    // Encode varint length
    size_t len = length;
    while (len > 0x7F) {
        result.push_back(static_cast<uint8_t>((len & 0x7F) | 0x80));
        len >>= 7;
    }
    result.push_back(static_cast<uint8_t>(len));
    
    // Encode doubles (little-endian)
    for (double elev : elevations) {
        uint64_t bits;
        static_assert(sizeof(bits) == sizeof(elev), "Size mismatch");
        std::memcpy(&bits, &elev, sizeof(elev));
        for (int i = 0; i < 8; ++i) {
            result.push_back(static_cast<uint8_t>(bits & 0xFF));
            bits >>= 8;
        }
    }
    
    return result;
}

// Find header value in request
bool HasHeader(const std::vector<std::pair<std::string, std::string>>& headers,
               const std::string& key, const std::string& expectedValue) {
    for (const auto& [k, v] : headers) {
        if (k == key && v == expectedValue) return true;
    }
    return false;
}

bool HasHeaderContaining(const std::vector<std::pair<std::string, std::string>>& headers,
                         const std::string& key, const std::string& substring) {
    for (const auto& [k, v] : headers) {
        if (k == key && v.find(substring) != std::string::npos) return true;
    }
    return false;
}

int main() {
    std::cout << "=== Google Earth Elevation Provider Test ===" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: 200 OK with valid elevation response
    {
        std::cout << "\nTest 1: 200 OK with valid elevations..." << std::endl;
        
        // Create stub transport
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        // Configure provider
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        config.authToken = "test-token-123";
        config.elevationType = 0; // ELLIPSOID
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up synthetic response: 3 elevations
        std::vector<double> expectedElevations = {100.5, 200.75, 150.25};
        std::vector<uint8_t> responseBody = BuildSyntheticElevationResponse(expectedElevations);
        transportPtr->SetNextResponse(200, responseBody, "", true);
        
        // Query 3 points
        std::vector<GeoPoint> points = {
            {0.0, 0.0},
            {1.0, 1.0},
            {2.0, 2.0}
        };
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        // Verify
        if (result.ok && 
            result.heights.size() == 3 &&
            std::abs(result.heights[0] - 100.5) < 0.001 &&
            std::abs(result.heights[1] - 200.75) < 0.001 &&
            std::abs(result.heights[2] - 150.25) < 0.001) {
            std::cout << "  PASS: Valid elevation response parsed correctly" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected 3 elevations [100.5, 200.75, 150.25], got "
                      << result.heights.size() << " elevations"
                      << (result.ok ? "" : " (error: " + result.error + ")") << std::endl;
            failed++;
        }
        
        // Verify Authorization header was sent
        if (HasHeaderContaining(transportPtr->lastHeaders, "Authorization", "Bearer test-token-123")) {
            std::cout << "  PASS: Authorization header present" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Authorization header missing or incorrect" << std::endl;
            failed++;
        }
        
        // Verify Content-Type header
        if (HasHeader(transportPtr->lastHeaders, "Content-Type", "application/x-protobuf")) {
            std::cout << "  PASS: Content-Type header correct" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Content-Type header missing or incorrect" << std::endl;
            failed++;
        }
    }
    
    // Test 2: 401 Unauthorized
    {
        std::cout << "\nTest 2: 401 Unauthorized..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        config.authToken = "invalid-token";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up 401 response
        transportPtr->SetNextResponse(401, {}, "Unauthorized", false);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}};
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (!result.ok && result.error.find("401") != std::string::npos) {
            std::cout << "  PASS: 401 error detected and reported" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected 401 error, got ok=" << result.ok 
                      << ", error=" << result.error << std::endl;
            failed++;
        }
    }
    
    // Test 3: 403 Forbidden
    {
        std::cout << "\nTest 3: 403 Forbidden..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        config.authToken = "valid-but-unauthorized";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up 403 response
        transportPtr->SetNextResponse(403, {}, "Forbidden", false);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}};
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (!result.ok && result.error.find("403") != std::string::npos) {
            std::cout << "  PASS: 403 error detected and reported" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected 403 error, got ok=" << result.ok 
                      << ", error=" << result.error << std::endl;
            failed++;
        }
    }
    
    // Test 4: Network timeout (curl=28 simulation)
    {
        std::cout << "\nTest 4: Network timeout..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up timeout response (httpCode=0 indicates network error)
        transportPtr->SetNextResponse(0, {}, "Operation timed out", false);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}};
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (!result.ok && result.error.find("Network") != std::string::npos) {
            std::cout << "  PASS: Network timeout detected and reported" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected network error, got ok=" << result.ok 
                      << ", error=" << result.error << std::endl;
            failed++;
        }
    }
    
    // Test 5: Decode failure (malformed protobuf)
    {
        std::cout << "\nTest 5: Decode failure (malformed protobuf)..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up malformed protobuf response (200 but invalid data)
        std::vector<uint8_t> malformedData = {0xFF, 0xFF, 0xFF, 0xFF}; // Invalid protobuf
        transportPtr->SetNextResponse(200, malformedData, "", true);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}};
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (!result.ok && result.error.find("parse") != std::string::npos) {
            std::cout << "  PASS: Decode failure detected and reported" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected parse error, got ok=" << result.ok 
                      << ", error=" << result.error << std::endl;
            failed++;
        }
    }
    
    // Test 6: Elevation count mismatch
    {
        std::cout << "\nTest 6: Elevation count mismatch..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up response with wrong number of elevations (requested 3, got 2)
        std::vector<double> elevations = {100.0, 200.0}; // Only 2
        std::vector<uint8_t> responseBody = BuildSyntheticElevationResponse(elevations);
        transportPtr->SetNextResponse(200, responseBody, "", true);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}}; // Requested 3
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (!result.ok && result.error.find("mismatch") != std::string::npos) {
            std::cout << "  PASS: Count mismatch detected and reported" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected count mismatch error, got ok=" << result.ok 
                      << ", error=" << result.error << std::endl;
            failed++;
        }
    }
    
    // Test 7: Empty points (edge case)
    {
        std::cout << "\nTest 7: Empty points edge case..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Query with empty points
        std::vector<GeoPoint> points; // Empty
        ElevationOptions opt;
        ElevationBatchResult result = provider.BatchQuery(points, opt);
        
        if (result.ok && result.heights.empty()) {
            std::cout << "  PASS: Empty points handled correctly" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Expected ok=true with empty result, got ok=" << result.ok 
                      << ", heights.size=" << result.heights.size() << std::endl;
            failed++;
        }
    }
    
    // Test 8: Custom headers in request
    {
        std::cout << "\nTest 8: Custom headers..." << std::endl;
        
        auto transport = std::make_unique<StubHttpTransport>();
        StubHttpTransport* transportPtr = transport.get();
        
        GoogleEarthElevationConfig config;
        config.endpoint = "https://test.google.com/rpc/eh";
        config.headers = {
            {"X-Custom-Auth", "custom-value"},
            {"X-Request-ID", "abc123"}
        };
        
        GoogleEarthElevationProvider provider(config, std::move(transport));
        
        // Set up valid response
        std::vector<double> elevations = {100.0};
        std::vector<uint8_t> responseBody = BuildSyntheticElevationResponse(elevations);
        transportPtr->SetNextResponse(200, responseBody, "", true);
        
        std::vector<GeoPoint> points = {{0.0, 0.0}};
        ElevationOptions opt;
        provider.BatchQuery(points, opt);
        
        // Verify custom headers were sent
        bool customAuthFound = HasHeader(transportPtr->lastHeaders, "X-Custom-Auth", "custom-value");
        bool requestIdFound = HasHeader(transportPtr->lastHeaders, "X-Request-ID", "abc123");
        
        if (customAuthFound && requestIdFound) {
            std::cout << "  PASS: Custom headers present in request" << std::endl;
            passed++;
        } else {
            std::cout << "  FAIL: Custom headers missing (X-Custom-Auth=" 
                      << (customAuthFound ? "found" : "missing")
                      << ", X-Request-ID=" << (requestIdFound ? "found" : "missing") << ")" << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
