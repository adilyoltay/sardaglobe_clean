#pragma once

#include "dem_fetch_result.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace globe {

// HTTP transport configuration
struct HttpTransportConfig {
    std::string userAgent = "NativeGlobe/1.0";
    long timeoutSec = 30;
    long connectTimeoutSec = 10;
    bool verifySsl = true;      // Default: secure (MITM protection)
    bool insecureMode = false;  // For local mock/self-signed (explicit opt-in)
};

// HTTP response structure
struct HttpResponse {
    long httpCode = 0;
    int curlResult = 0;       // CURLcode (e.g., 0=OK, 28=timeout)
    std::vector<uint8_t> body;
    std::string errorMessage;
    double elapsedMs = 0.0;
    bool success = false;
};

// HTTP transport interface
// Abstracts CURL operations for testability
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    
    // Perform HTTP POST request
    // Returns HttpResponse with status, body, and timing
    virtual HttpResponse Post(const std::string& url,
                              const std::vector<uint8_t>& requestBody,
                              const std::vector<std::pair<std::string, std::string>>& headers) = 0;
    
    // Perform HTTP GET request
    virtual HttpResponse Get(const std::string& url,
                             const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

// CURL-based HTTP transport implementation
class CurlHttpTransport : public IHttpTransport {
public:
    explicit CurlHttpTransport(const HttpTransportConfig& config);
    ~CurlHttpTransport() override;
    
    HttpResponse Post(const std::string& url,
                      const std::vector<uint8_t>& requestBody,
                      const std::vector<std::pair<std::string, std::string>>& headers) override;
    
    HttpResponse Get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers) override;

private:
    HttpTransportConfig config_;
    
    // Perform request with given method and body
    HttpResponse DoRequest(const std::string& url,
                           const std::string& method,
                           const std::vector<uint8_t>& requestBody,
                           const std::vector<std::pair<std::string, std::string>>& headers);
};

// Convert HttpResponse to DemFetchResult for telemetry
DemFetchResult HttpResponseToDemFetchResult(const HttpResponse& response);

} // namespace globe
