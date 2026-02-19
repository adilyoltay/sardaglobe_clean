#include "google_earth_elevation_provider.h"
#include "ge_headers.h"
#include "protobuf_wire.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>
#include <zlib.h>
#include <cstring>

namespace globe {

namespace {

static bool IsGzipPayload(const std::vector<uint8_t>& body) {
    return body.size() > 2 && body[0] == 0x1Fu && body[1] == 0x8Bu;
}

static bool DecodeGzipPayload(const std::vector<uint8_t>& input, std::string& outText) {
    if (!IsGzipPayload(input)) {
        return false;
    }
    
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        return false;
    }
    
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    zs.avail_in = static_cast<uInt>(input.size());
    
    constexpr size_t kChunk = 4096;
    unsigned char out[kChunk];
    bool ok = true;
    
    for (;;) {
        zs.next_out = out;
        zs.avail_out = kChunk;
        
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            size_t got = kChunk - zs.avail_out;
            if (got > 0) {
                outText.append(reinterpret_cast<const char*>(out), got);
            }
            break;
        }
        
        if (ret != Z_OK) {
            ok = false;
            break;
        }
        
        size_t got = kChunk - zs.avail_out;
        if (got > 0) {
            outText.append(reinterpret_cast<const char*>(out), got);
        }
        
        if (zs.avail_out != 0) {
            // Non-progress indicates malformed stream.
            ok = false;
            break;
        }
    }
    
    inflateEnd(&zs);
    return ok;
}

static std::string NormalizeErrorText(std::string text) {
    if (text.empty()) {
        return text;
    }

    // Remove protobuf-style binary prefix such as 0x08,0x03 before ASCII payload.
    size_t start = 0;
    while (start < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[start]);
        if (std::isprint(c) || std::isspace(c)) {
            break;
        }
        ++start;
    }
    text = text.substr(start);

    // Special-case known GE auth message payload.
    const std::string unsafeMarker = "Request unsafe for browser client domain";
    size_t markerPos = text.find(unsafeMarker);
    if (markerPos != std::string::npos) {
        text = text.substr(markerPos);
    }

    return text;
}

static std::string DecodeErrorBody(const std::vector<uint8_t>& body) {
    if (body.empty()) return {};
    
    std::string text(reinterpret_cast<const char*>(body.data()), body.size());
    if (IsGzipPayload(body)) {
        std::string gzText;
        if (DecodeGzipPayload(body, gzText)) {
            return NormalizeErrorText(gzText);
        }
    }
    
    bool mostlyPrintable = true;
    size_t printableCount = 0;
    for (unsigned char c : body) {
        if (std::isprint(c) || std::isspace(c)) {
            ++printableCount;
        }
    }
    if (!body.empty() && (printableCount * 100 / body.size()) < 40) {
        mostlyPrintable = false;
    }
    if (!mostlyPrintable) {
        return {};
    }
    return NormalizeErrorText(text);
}

static bool IsAuthRequiredBody(const std::string& text, bool hasAuthToken) {
    const std::string unsafeMarker = "Request unsafe for browser client domain";
    return !hasAuthToken && text.find(unsafeMarker) != std::string::npos;
}

static bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.empty()) {
        return false;
    }
    
    return std::search(haystack.begin(), haystack.end(),
                       needle.begin(), needle.end(),
                       [](char lhs, char rhs) {
                           return std::tolower(static_cast<unsigned char>(lhs)) ==
                                  std::tolower(static_cast<unsigned char>(rhs));
                       }) != haystack.end();
}

static bool IsBlockedResponse(const std::string& text, long httpStatusCode, size_t bodySize) {
    if (bodySize == 0) {
        return false;
    }
    if (httpStatusCode != 403 && httpStatusCode != 429 && httpStatusCode != 451) {
        return false;
    }
    if (text.empty()) {
        return false;
    }
    const bool looksLikeHtml = ContainsIgnoreCase(text, "<html") ||
                              ContainsIgnoreCase(text, "<!doctype");
    if (looksLikeHtml &&
        (ContainsIgnoreCase(text, "sorry") ||
         ContainsIgnoreCase(text, "automated queries") ||
         ContainsIgnoreCase(text, "google home") ||
         ContainsIgnoreCase(text, "we are sorry") ||
         ContainsIgnoreCase(text, "request unsafe for browser client domain"))) {
        return true;
    }
    return false;
}

} // namespace

GoogleEarthElevationProvider::GoogleEarthElevationProvider(const GoogleEarthElevationConfig& config)
    : config_(config) {
    // Create default CURL transport
    HttpTransportConfig transportConfig;
    transportConfig.timeoutSec = config.timeoutSec;
    transport_ = std::make_unique<CurlHttpTransport>(transportConfig);
}

GoogleEarthElevationProvider::GoogleEarthElevationProvider(
    const GoogleEarthElevationConfig& config,
    std::unique_ptr<IHttpTransport> transport)
    : config_(config), transport_(std::move(transport)) {
}

GoogleEarthElevationProvider::~GoogleEarthElevationProvider() = default;

std::vector<std::pair<std::string, std::string>> 
GoogleEarthElevationProvider::BuildRequestHeaders() const {
    auto headers = ge_headers::BuildStandardHeaders();
    // Elevation API uses POST with protobuf body — add Content-Type
    headers.push_back({"Content-Type", "application/x-protobuf"});
    // Elevation endpoint is same-site (kh.google.com -> earth.google.com)
    // Override the default cross-site value set by BuildStandardHeaders
    for (auto& [key, value] : headers) {
        if (key == "Sec-Fetch-Site") {
            value = "same-site";
            break;
        }
    }
    ge_headers::AppendCustomHeaders(headers, config_.headers);
    ge_headers::AppendBearerTokenIfNeeded(headers, config_.authToken);
    return headers;
}

bool GoogleEarthElevationProvider::ValidateResponse(const std::vector<double>& elevations,
                                                     size_t expectedCount,
                                                     std::string& errorMessage) const {
    if (elevations.size() != expectedCount) {
        errorMessage = "Elevation count mismatch: expected " + std::to_string(expectedCount) +
                       ", got " + std::to_string(elevations.size());
        return false;
    }
    
    // Check for invalid values (NaN, extreme values)
    for (size_t i = 0; i < elevations.size(); ++i) {
        double elev = elevations[i];
        if (std::isnan(elev)) {
            errorMessage = "Invalid elevation (NaN) at index " + std::to_string(i);
            return false;
        }
        // Allow -1000m (Dead Sea) to +9000m (Everest)
        if (elev < -2000.0 || elev > 10000.0) {
            errorMessage = "Elevation out of range at index " + std::to_string(i) +
                           ": " + std::to_string(elev);
            return false;
        }
    }
    
    return true;
}

ElevationBatchResult GoogleEarthElevationProvider::BatchQuery(const std::vector<GeoPoint>& points,
                                                               const ElevationOptions& opt) {
    ElevationBatchResult result;
    
    if (points.empty()) {
        result.ok = true;
        return result;
    }
    
    // Validate endpoint
    if (config_.endpoint.empty()) {
        result.error = "Google Earth elevation endpoint not configured";
        result.fetch = DemFetchResult::DecodeError(result.error);
        return result;
    }
    
    // Extract lat/lon arrays
    std::vector<double> lats;
    std::vector<double> lons;
    lats.reserve(points.size());
    lons.reserve(points.size());
    
    for (const auto& point : points) {
        lats.push_back(point.latDeg);
        lons.push_back(point.lonDeg);
    }
    
    // Build protobuf request
    std::vector<uint8_t> requestBody;
    try {
        requestBody = protobuf::ge::BuildElevationRequest(lats, lons, config_.elevationType);
    } catch (const std::exception& e) {
        result.error = std::string("Failed to build request: ") + e.what();
        result.fetch = DemFetchResult::DecodeError(result.error);
        return result;
    }
    
    // Perform HTTP POST
    auto headers = BuildRequestHeaders();
    HttpResponse httpResponse = transport_->Post(config_.endpoint, requestBody, headers);
    const std::string responseText = DecodeErrorBody(httpResponse.body);
    
    // Map HTTP response to DemFetchResult for telemetry/backoff
    // This preserves metadata (httpStatusCode, bytesReceived, elapsedMs, curlResult)
    result.fetch = HttpResponseToDemFetchResult(httpResponse);
    
    // Debug logging for troubleshooting auth/bad response
    if (!httpResponse.success || httpResponse.httpCode != 200) {
        std::cerr << "[GE Elevation] Request failed:\n"
                  << "  URL: " << config_.endpoint << "\n"
                  << "  HTTP Code: " << httpResponse.httpCode << "\n"
                  << "  Curl Result: " << httpResponse.curlResult << "\n"
                  << "  Body Size: " << httpResponse.body.size() << " bytes\n"
                  << "  Error: " << httpResponse.errorMessage << "\n";
        if (!responseText.empty()) {
            std::cerr << "  Body Text: " << responseText << "\n";
        } else if (!httpResponse.body.empty()) {
            std::cerr << "  Body (HEX): ";
            for (uint8_t byte : httpResponse.body) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X ", byte);
                std::cerr << hex;
            }
            std::cerr << "\n";
        }
    }

    // Check HTTP result
    if (!httpResponse.success) {
        const bool blockedResponse = IsBlockedResponse(responseText, httpResponse.httpCode, httpResponse.body.size());
        
        if (IsAuthRequiredBody(responseText, !config_.authToken.empty())) {
            result.fetch.errorType = DemFetchResult::ErrorType::Auth;
            result.fetch.errorMessage = "Google Earth elevation endpoint rejected request: browser auth required. "
                                      "Set NATIVE_GLOBE_GE_TOKEN or pass Authorization via --ge-header.";
            result.error = result.fetch.errorMessage;
            return result;
        }

        if (blockedResponse) {
            std::string blockedMessage = "Google Earth elevation endpoint returned anti-automation block page (likely CAPTCHA/protection).";
            if (!responseText.empty()) {
                blockedMessage += " Body preview: " + responseText.substr(0, 200);
            }
            result.fetch.errorType = DemFetchResult::ErrorType::Blocked;
            result.fetch.errorMessage = blockedMessage;
            result.error = blockedMessage;
            return result;
        }
        
        // Use httpCode and curlResult for specific error mapping (no substring matching)
        if (httpResponse.httpCode == 401) {
            result.error = "Authentication failed (401). Check GE auth token.";
        } else if (httpResponse.httpCode == 403) {
            result.error = "Access denied (403). Check GE permissions.";
        } else if (httpResponse.curlResult == 28) {
            result.error = "Request timeout (curl=28)";
        } else if (httpResponse.httpCode == 0) {
            result.error = "Network error: " + httpResponse.errorMessage;
        } else {
            result.error = "HTTP error " + std::to_string(httpResponse.httpCode) 
                         + ": " + httpResponse.errorMessage;
        }
        return result;
    }
    
    // Parse protobuf response
    std::vector<double> elevations;
    try {
        elevations = protobuf::ge::ParseElevationResponse(httpResponse.body);
    } catch (const std::exception& e) {
        // Decode error: preserve HTTP metadata (Phase 3 "decode metadata preserve")
        result.error = std::string("Failed to parse response: ") + e.what();
        result.fetch.errorType = DemFetchResult::ErrorType::Decode;
        result.fetch.errorMessage = result.error;
        // httpStatusCode, bytesReceived, elapsedMs already preserved from HttpResponseToDemFetchResult
        return result;
    }
    
    // Validate response
    std::string validationError;
    if (!ValidateResponse(elevations, points.size(), validationError)) {
        // Validation error: preserve HTTP metadata
        result.error = validationError;
        result.fetch.errorType = DemFetchResult::ErrorType::Decode;
        result.fetch.errorMessage = result.error;
        return result;
    }
    
    // Success
    result.ok = true;
    result.heights = std::move(elevations);
    return result;
}

} // namespace globe
