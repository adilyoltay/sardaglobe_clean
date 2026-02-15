#pragma once

#include <string>
#include <chrono>

namespace globe {

// Structured result from a DEM fetch operation.
// Used by DemManager to update stats, backoff state, and health status.
struct DemFetchResult {
    bool success = false;
    
    // HTTP status code (if applicable, 0 if no HTTP response)
    long httpStatusCode = 0;
    
    // CURL result code (CURLE_OK on success)
    int curlResult = 0;
    
    // Error categorization
    enum class ErrorType {
        None,           // Success
        Network,        // Connection/DNS (CURLE_* errors)
        Timeout,        // Request timeout (CURLE_OPERATION_TIMEDOUT=28)
        Auth,           // Auth token/header issue
        Blocked,        // Automated/bot protection response (e.g., CAPTCHA HTML)
        HttpError,      // Other HTTP 4xx/5xx
        Decode,         // Image decode failure
        Unknown
    } errorType = ErrorType::None;
    
    std::string errorMessage;  // Human-readable error
    
    // Timing
    double elapsedMs = 0.0;
    
    // Size
    size_t bytesReceived = 0;
    
    // Helper to determine if this is an auth failure
    bool IsAuthFailure() const {
        return errorType == ErrorType::Auth;
    }

    // Helper to determine if this is an anti-bot/blocked failure
    bool IsBlockedFailure() const {
        return errorType == ErrorType::Blocked;
    }
    
    // Helper to determine if this is a timeout
    bool IsTimeout() const {
        return errorType == ErrorType::Timeout || 
               (errorType == ErrorType::Network && curlResult == 28) ||
               errorMessage.find("timed out") != std::string::npos ||
               errorMessage.find("timeout") != std::string::npos;
    }
    
    // Create success result
    static DemFetchResult Success(long httpCode, size_t bytes, double elapsedMs);
    
    // Create common failure results
    static DemFetchResult NetworkError(int curlCode, const std::string& msg, double elapsedMs);
    static DemFetchResult TimeoutError(int curlCode, const std::string& msg, double elapsedMs);
    static DemFetchResult AuthError(long httpCode, const std::string& msg);
    static DemFetchResult BlockedError(long httpCode, const std::string& msg);
    static DemFetchResult HttpError(long httpCode, const std::string& msg);
    static DemFetchResult DecodeError(const std::string& msg);
};

} // namespace globe
