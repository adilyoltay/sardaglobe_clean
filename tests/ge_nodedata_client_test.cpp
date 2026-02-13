// GE NodeData Client Test
// Tests GoogleEarthNodeDataClient with stub transport

#include "../src/io/providers/google_earth_nodedata_client.h"
#include "../src/io/providers/http_transport.h"
#include "../src/core/config.h"
#include <iostream>
#include <cstring>
#include <cassert>
#include <cstdlib>

using namespace globe;

// Stub HTTP transport for testing
class StubHttpTransport : public IHttpTransport {
public:
    // Configurable response
    HttpResponse nextResponse;
    std::string lastUrl;
    std::vector<std::pair<std::string, std::string>> lastHeaders;
    
    HttpResponse Post(const std::string& url,
                      const std::vector<uint8_t>& requestBody,
                      const std::vector<std::pair<std::string, std::string>>& headers) override {
        lastUrl = url;
        lastHeaders = headers;
        return nextResponse;
    }
    
    HttpResponse Get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers) override {
        lastUrl = url;
        lastHeaders = headers;
        return nextResponse;
    }
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

int main() {
    int failed = 0;
    std::cout << "=== GE NodeData Client Test ===\n";

    // Test 1: URL construction with template
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/mesh/{quadkey}";
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        
        // Setup success response
        stubPtr->nextResponse.success = true;
        stubPtr->nextResponse.httpCode = 200;
        stubPtr->nextResponse.body = {0x01, 0x02, 0x03};  // Fake protobuf data
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("1234567");
        
        failed += !Expect(result.success, "Should succeed with 200");
        failed += !Expect(stubPtr->lastUrl == "https://example.com/mesh/1234567", 
                         "URL should have quadkey replaced");
        failed += !Expect(result.data.size() == 3, "Should return body data");
        std::cout << "  URL construction: " << stubPtr->lastUrl << "\n";
    }

    // Test 2: Headers - Accept and custom headers
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        config.geHeaders.push_back({"X-Custom-Header", "custom-value"});
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = true;
        stubPtr->nextResponse.httpCode = 200;
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("7654321");
        
        // Check Accept header
        bool hasAccept = false;
        bool hasCustom = false;
        for (const auto& [k, v] : stubPtr->lastHeaders) {
            if (k == "Accept" && v == "application/x-protobuf") hasAccept = true;
            if (k == "X-Custom-Header" && v == "custom-value") hasCustom = true;
        }
        failed += !Expect(hasAccept, "Should have Accept: application/x-protobuf");
        failed += !Expect(hasCustom, "Should have custom header from config");
        std::cout << "  Headers: Accept protobuf OK, custom header OK\n";
    }

    // Test 2b: Authorization from env token (Bearer)
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        config.geTokenEnv = "TEST_GE_TOKEN";
        
        // Set env token
        setenv("TEST_GE_TOKEN", "my-secret-token-123", 1);
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = true;
        stubPtr->nextResponse.httpCode = 200;
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("1111111");
        
        bool hasBearer = false;
        for (const auto& [k, v] : stubPtr->lastHeaders) {
            if (k == "Authorization" && v == "Bearer my-secret-token-123") {
                hasBearer = true;
                break;
            }
        }
        failed += !Expect(hasBearer, "Should have Bearer token from env");
        std::cout << "  Bearer auth: OK\n";
        
        unsetenv("TEST_GE_TOKEN");
    }

    // Test 2c: Custom Authorization header takes precedence over env token
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        config.geTokenEnv = "TEST_GE_TOKEN2";
        config.geHeaders.push_back({"Authorization", "CustomAuth abc123"});
        
        // Set env token (should be ignored)
        setenv("TEST_GE_TOKEN2", "env-token-ignored", 1);
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = true;
        stubPtr->nextResponse.httpCode = 200;
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("2222222");
        
        bool hasCustomAuth = false;
        bool hasBearer = false;
        for (const auto& [k, v] : stubPtr->lastHeaders) {
            if (k == "Authorization") {
                if (v == "CustomAuth abc123") hasCustomAuth = true;
                if (v.find("Bearer") == 0) hasBearer = true;
            }
        }
        failed += !Expect(hasCustomAuth, "Should have custom Authorization header");
        failed += !Expect(!hasBearer, "Should NOT have Bearer when custom Auth provided");
        std::cout << "  Custom auth precedence: OK\n";
        
        unsetenv("TEST_GE_TOKEN2");
    }

    // Test 3: 404 error handling
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = false;
        stubPtr->nextResponse.httpCode = 404;
        stubPtr->nextResponse.errorMessage = "Not found";
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("0000000");
        
        failed += !Expect(!result.success, "Should fail with 404");
        failed += !Expect(result.httpCode == 404, "Should report 404 status");
        std::cout << "  404 handling: OK\n";
    }

    // Test 4: Timeout error handling (curlResult=28)
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = false;
        stubPtr->nextResponse.httpCode = 0;
        stubPtr->nextResponse.curlResult = 28;  // CURLE_OPERATION_TIMEDOUT
        stubPtr->nextResponse.errorMessage = "Request timed out";
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("1111111");
        
        failed += !Expect(!result.success, "Should fail on timeout");
        failed += !Expect(result.curlResult == 28, "curlResult should be 28");
        failed += !Expect(result.IsTimeout(), "IsTimeout() should be true with curlResult=28");
        std::cout << "  Timeout handling: OK\n";
    }

    // Test 4b: Timeout detection without message (curlResult only)
    {
        Config config;
        config.geMeshEndpoint = "https://example.com/{quadkey}";
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = false;
        stubPtr->nextResponse.httpCode = 0;
        stubPtr->nextResponse.curlResult = 28;
        stubPtr->nextResponse.errorMessage = "Connection failed";  // No "timeout" word
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("3333333");
        
        // Should still detect timeout by curlResult
        failed += !Expect(result.IsTimeout(), "IsTimeout() should detect by curlResult even without message");
        std::cout << "  Timeout by curlResult only: OK\n";
    }

    // Test 5: IsEnabled check
    {
        Config configEmpty;
        configEmpty.geMeshEndpoint = "";
        
        auto stub = std::make_unique<StubHttpTransport>();
        GoogleEarthNodeDataClient client(configEmpty, std::move(stub));
        
        failed += !Expect(!client.IsEnabled(), "Should be disabled with empty endpoint");
        
        Config configValid;
        configValid.geMeshEndpoint = "https://x/{quadkey}";
        
        auto stub2 = std::make_unique<StubHttpTransport>();
        GoogleEarthNodeDataClient client2(configValid, std::move(stub2));
        
        failed += !Expect(client2.IsEnabled(), "Should be enabled with valid endpoint");
        std::cout << "  IsEnabled: OK\n";
    }

    // Test 6: GE-style URL with complex template
    {
        Config config;
        config.geMeshEndpoint = "https://kh.google.com/rpc/NodeData?pb=!1s{quadkey}!2e1!4e0";
        
        auto stub = std::make_unique<StubHttpTransport>();
        StubHttpTransport* stubPtr = stub.get();
        stubPtr->nextResponse.success = true;
        stubPtr->nextResponse.httpCode = 200;
        
        GoogleEarthNodeDataClient client(config, std::move(stub));
        
        NodeDataResult result = client.FetchNodeData("123456");
        
        std::string expected = "https://kh.google.com/rpc/NodeData?pb=!1s123456!2e1!4e0";
        failed += !Expect(stubPtr->lastUrl == expected, "GE-style URL should be correct");
        std::cout << "  GE-style URL: " << stubPtr->lastUrl << "\n";
    }

    if (failed == 0) {
        std::cout << "GeNodeDataClientTest PASSED\n";
        return 0;
    }

    std::cerr << "GeNodeDataClientTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
