#include "dem_fetch_result.h"

namespace globe {

DemFetchResult DemFetchResult::Success(long httpCode, size_t bytes, double elapsedMs) {
    DemFetchResult r;
    r.success = true;
    r.httpStatusCode = httpCode;
    r.elapsedMs = elapsedMs;
    r.bytesReceived = bytes;
    r.errorType = ErrorType::None;
    return r;
}

DemFetchResult DemFetchResult::NetworkError(int curlCode, const std::string& msg, double elapsedMs) {
    DemFetchResult r;
    r.success = false;
    r.curlResult = curlCode;
    r.errorMessage = msg;
    r.elapsedMs = elapsedMs;
    r.errorType = ErrorType::Network;
    return r;
}

DemFetchResult DemFetchResult::TimeoutError(int curlCode, const std::string& msg, double elapsedMs) {
    DemFetchResult r;
    r.success = false;
    r.curlResult = curlCode;
    r.errorMessage = msg;
    r.elapsedMs = elapsedMs;
    r.errorType = ErrorType::Timeout;
    return r;
}

DemFetchResult DemFetchResult::AuthError(long httpCode, const std::string& msg) {
    DemFetchResult r;
    r.success = false;
    r.httpStatusCode = httpCode;
    r.errorMessage = msg;
    r.errorType = ErrorType::Auth;
    return r;
}

DemFetchResult DemFetchResult::BlockedError(long httpCode, const std::string& msg) {
    DemFetchResult r;
    r.success = false;
    r.httpStatusCode = httpCode;
    r.errorMessage = msg;
    r.errorType = ErrorType::Blocked;
    return r;
}

DemFetchResult DemFetchResult::HttpError(long httpCode, const std::string& msg) {
    DemFetchResult r;
    r.success = false;
    r.httpStatusCode = httpCode;
    r.errorMessage = msg;
    r.errorType = ErrorType::HttpError;
    return r;
}

DemFetchResult DemFetchResult::DecodeError(const std::string& msg) {
    DemFetchResult r;
    r.success = false;
    r.errorMessage = msg;
    r.errorType = ErrorType::Decode;
    return r;
}

} // namespace globe
