#include "http_transport.h"
#include <curl/curl.h>
#include <chrono>
#include <sstream>

namespace globe {

namespace {

// CURL write callback for response body
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<std::vector<uint8_t>*>(userp);
    size_t totalSize = size * nmemb;
    data->insert(data->end(),
                 static_cast<uint8_t*>(contents),
                 static_cast<uint8_t*>(contents) + totalSize);
    return totalSize;
}

// CURL read callback for request body
static size_t ReadCallback(void* dest, size_t size, size_t nmemb, void* userp) {
    auto* data = static_cast<const std::vector<uint8_t>*>(userp);
    size_t totalSize = size * nmemb;
    size_t toCopy = std::min(totalSize, data->size());
    if (toCopy > 0) {
        std::memcpy(dest, data->data(), toCopy);
        // Remove copied bytes from front
        const_cast<std::vector<uint8_t>*>(data)->erase(
            const_cast<std::vector<uint8_t>*>(data)->begin(),
            const_cast<std::vector<uint8_t>*>(data)->begin() + toCopy);
    }
    return toCopy;
}

} // anonymous namespace

CurlHttpTransport::CurlHttpTransport(const HttpTransportConfig& config)
    : config_(config) {
}

CurlHttpTransport::~CurlHttpTransport() = default;

HttpResponse CurlHttpTransport::DoRequest(const std::string& url,
                                          const std::string& method,
                                          const std::vector<uint8_t>& requestBody,
                                          const std::vector<std::pair<std::string, std::string>>& headers) {
    HttpResponse response;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.errorMessage = "curl_easy_init failed";
        return response;
    }
    
    const auto startTime = std::chrono::high_resolution_clock::now();
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set method and body
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, requestBody.size());
    }
    
    // Set write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    
    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeoutSec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSec);
    
    // SSL verification
    // verifySsl=false OR insecureMode=true -> disable both peer and host verification
    long sslVerify = (config_.verifySsl && !config_.insecureMode) ? 1L : 0L;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, sslVerify);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, sslVerify ? 2L : 0L);
    
    // User agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.userAgent.c_str());
    
    // Headers
    struct curl_slist* headerList = nullptr;
    for (const auto& [key, value] : headers) {
        std::string headerLine = key + ": " + value;
        headerList = curl_slist_append(headerList, headerLine.c_str());
    }
    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    response.curlResult = static_cast<int>(res);
    
    // Get timing
    const auto endTime = std::chrono::high_resolution_clock::now();
    response.elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    // Get HTTP code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.httpCode);
    
    // Cleanup
    if (headerList) {
        curl_slist_free_all(headerList);
    }
    curl_easy_cleanup(curl);
    
    // Determine success
    if (res != CURLE_OK) {
        response.errorMessage = curl_easy_strerror(res);
        response.success = false;
    } else if (response.httpCode >= 200 && response.httpCode < 300) {
        response.success = true;
    } else {
        response.errorMessage = "HTTP error " + std::to_string(response.httpCode);
        response.success = false;
    }
    
    return response;
}

HttpResponse CurlHttpTransport::Post(const std::string& url,
                                     const std::vector<uint8_t>& requestBody,
                                     const std::vector<std::pair<std::string, std::string>>& headers) {
    return DoRequest(url, "POST", requestBody, headers);
}

HttpResponse CurlHttpTransport::Get(const std::string& url,
                                    const std::vector<std::pair<std::string, std::string>>& headers) {
    return DoRequest(url, "GET", {}, headers);
}

DemFetchResult HttpResponseToDemFetchResult(const HttpResponse& response) {
    if (response.success) {
        return DemFetchResult::Success(response.httpCode, response.body.size(), response.elapsedMs);
    }
    
    // Map HTTP errors to DemFetchResult error types
    if (response.httpCode == 401 || response.httpCode == 403) {
        return DemFetchResult::AuthError(response.httpCode, response.errorMessage);
    }
    
    // Check for timeout (curlResult=28) or timeout in error message
    if (response.curlResult == 28 ||
        response.errorMessage.find("Timeout") != std::string::npos ||
        response.errorMessage.find("timeout") != std::string::npos ||
        response.errorMessage.find("timed out") != std::string::npos) {
        auto result = DemFetchResult::TimeoutError(28, response.errorMessage, response.elapsedMs);
        result.httpStatusCode = response.httpCode;
        return result;
    }
    
    // Generic HTTP error
    return DemFetchResult::HttpError(response.httpCode, response.errorMessage);
}

} // namespace globe
