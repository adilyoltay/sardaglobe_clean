// Google Earth Web Spoofing Headers — Single Source of Truth
// All GE API clients (NodeData, Elevation, Octree) must use these headers
// to mimic the official Google Earth web client and avoid CAPTCHA/blocking.
//
// Header set reverse-engineered from Chrome DevTools on earth.google.com
// matching the exact request signature of the GE WASM client.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace globe {
namespace ge_headers {

// Chrome version to spoof (update periodically to avoid detection)
inline constexpr const char* kChromeVersion = "121.0.0.0";

// User-Agent matching Chrome on macOS (Intel)
inline const std::string kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/" + std::string(kChromeVersion) + " Safari/537.36";

// X-Client-Data: base64-encoded client capabilities token
// Extracted from live GE Web session; opaque to the server but required
// for request fingerprint validation.
inline constexpr const char* kXClientData =
    "CI+2yQEIprbJAQipncoBCKDhygEIkqHLAQj6mM0B";

// Build the standard GE spoofing header set for protobuf API calls.
// Includes Origin, Referer, Sec-* headers to pass server-side fingerprint checks.
inline std::vector<std::pair<std::string, std::string>>
BuildStandardHeaders() {
    return {
        {"Accept",             "application/x-protobuf"},
        {"Accept-Language",    "en-US,en;q=0.9"},
        {"Accept-Encoding",    "gzip, deflate, br"},
        {"User-Agent",         kUserAgent},
        {"Referer",            "https://earth.google.com/"},
        {"Origin",             "https://earth.google.com"},
        {"Sec-Ch-Ua",          "\"Not A(Brand)\";v=\"99\", \"Google Chrome\";v=\""
                               + std::string(kChromeVersion) + "\", \"Chromium\";v=\""
                               + std::string(kChromeVersion) + "\""},
        {"Sec-Ch-Ua-Mobile",   "?0"},
        {"Sec-Ch-Ua-Platform", "\"macOS\""},
        {"Sec-Fetch-Dest",     "empty"},
        {"Sec-Fetch-Mode",     "cors"},
        {"Sec-Fetch-Site",     "cross-site"},
        {"X-Client-Data",      kXClientData},
    };
}

// Append Authorization: Bearer <token> if token is non-empty and no custom
// Authorization header already exists in |headers|.
inline void AppendBearerTokenIfNeeded(
    std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& token) {
    if (token.empty()) return;
    for (const auto& [key, value] : headers) {
        if (key == "Authorization") return;  // Custom auth takes precedence
    }
    headers.push_back({"Authorization", "Bearer " + token});
}

// Append custom headers from config (e.g., --ge-header CLI overrides).
// Custom Authorization headers are preserved and prevent automatic Bearer injection.
inline void AppendCustomHeaders(
    std::vector<std::pair<std::string, std::string>>& headers,
    const std::vector<std::pair<std::string, std::string>>& customHeaders) {
    for (const auto& h : customHeaders) {
        headers.push_back(h);
    }
}

}  // namespace ge_headers
}  // namespace globe
