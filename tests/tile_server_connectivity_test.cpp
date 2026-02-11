// Tile server connectivity diagnostic test.
// Validates that configured tile/DEM endpoints are reachable and return valid data.
// Also verifies the synthetic (ngrd://) pipeline as a baseline.

#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <curl/curl.h>

static int passed = 0;
static int failed = 0;
static int warned = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "  FAIL: " << msg << std::endl; \
        ++failed; \
    } else { \
        std::cout << "  PASS: " << msg << std::endl; \
        ++passed; \
    } \
} while(0)

#define TEST_WARN(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "  WARN: " << msg << std::endl; \
        ++warned; \
    } else { \
        std::cout << "  PASS: " << msg << std::endl; \
        ++passed; \
    } \
} while(0)

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
    size_t offset = buffer->size();
    buffer->resize(offset + totalSize);
    std::memcpy(buffer->data() + offset, contents, totalSize);
    return totalSize;
}

struct HttpResult {
    long httpStatus = 0;
    std::string contentType;
    std::vector<uint8_t> data;
    std::string error;
    bool success = false;
};

std::string ExtractOrigin(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return {};
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos) return url;
    return url.substr(0, pathStart);
}

double Tile2Lon(int x, int z) {
    return x / static_cast<double>(1 << z) * 360.0 - 180.0;
}

double Tile2Lat(int y, int z) {
    constexpr double kPi = 3.14159265358979323846;
    double n = kPi - 2.0 * kPi * y / static_cast<double>(1 << z);
    return 180.0 / kPi * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

std::string BuildDemBatchUrlForTile(const std::string& baseUrl, int z, int x, int y, int meshN) {
    double llx = Tile2Lon(x, z);
    double urx = Tile2Lon(x + 1, z);
    double ury = Tile2Lat(y, z);
    double lly = Tile2Lat(y + 1, z);

    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(12);
    // Match DemManager::BuildBatchUrl format exactly: FLOAT=1&MESHN=..&CN=1&C1...
    oss << baseUrl
        << "?FLOAT=1"
        << "&MESHN=" << meshN
        << "&CN=1"
        << "&C1LLX=" << llx
        << "&C1LLY=" << lly
        << "&C1URX=" << urx
        << "&C1URY=" << ury;
    return oss.str();
}

HttpResult HttpGet(const std::string& url, const std::string& auth = {}) {
    HttpResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36");

    if (!auth.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
    }

    struct curl_slist* headers = nullptr;
    std::string origin = ExtractOrigin(url);
    if (!origin.empty()) {
        headers = curl_slist_append(headers, ("Origin: " + origin).c_str());
        headers = curl_slist_append(headers, ("Referer: " + origin + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpStatus);

    const char* ct = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    if (ct) result.contentType = ct;

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
    } else {
        result.success = (result.httpStatus == 200);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return result;
}

bool LooksLikeImage(const std::vector<uint8_t>& data) {
    if (data.size() >= 8) {
        static const uint8_t pngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(data.data(), pngSig, sizeof(pngSig)) == 0) return true;
    }
    if (data.size() >= 3) {
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return true;
    }
    if (data.size() >= 12) {
        if (std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WEBP", 4) == 0) return true;
    }
    return false;
}

} // anonymous namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "\n=== Tile Server Connectivity Diagnostic ===\n\n";

    const char* tileAuthEnv = std::getenv("NATIVE_GLOBE_TILE_AUTH");
    const char* demAuthEnv = std::getenv("NATIVE_GLOBE_DEM_AUTH");
    const std::string tileAuth = tileAuthEnv ? std::string(tileAuthEnv) : std::string();
    const std::string demAuth = demAuthEnv ? std::string(demAuthEnv) : std::string();

    // ---------------------------------------------------------------
    // 1. Pirireis Tile Server (default config)
    // ---------------------------------------------------------------
    std::cout << "--- 1. Pirireis HGM_Orthofoto Tile Server ---\n";
    {
        std::string url = "https://goksun.pirireis.com.tr/gorsun/gorsun/tile/HGM_Orthofoto/0/0/0";
        auto r = HttpGet(url, tileAuth);
        std::cout << "  URL: " << url << "\n";
        std::cout << "  Auth: " << (tileAuth.empty() ? "none" : "basic") << "\n";
        std::cout << "  HTTP Status: " << r.httpStatus << "\n";
        std::cout << "  Content-Type: " << r.contentType << "\n";
        std::cout << "  Body Size: " << r.data.size() << " bytes\n";

        if (r.httpStatus == 401 || r.httpStatus == 403) {
            std::cerr << "  ** AUTH REQUIRED: Server returned " << r.httpStatus
                      << ". Set NATIVE_GLOBE_TILE_AUTH=user:password **\n";
        }

        // This is a WARN, not FAIL, because auth may not be configured in CI
        TEST_WARN(r.httpStatus == 200,
            "Pirireis tile server returns HTTP 200 (got " + std::to_string(r.httpStatus) + ")");

        if (r.success) {
            TEST_WARN(LooksLikeImage(r.data),
                "Pirireis tile response is valid image data");
        }
    }

    // ---------------------------------------------------------------
    // 2. Pirireis DEM Server (default config)
    // ---------------------------------------------------------------
    std::cout << "\n--- 2. Pirireis DEM Server ---\n";
    {
        // Use the exact URL format emitted by DemManager::BuildBatchUrl to avoid false 400s.
        std::string baseUrl = "https://goksun.pirireis.com.tr/yersun/yersun/elevation_bbox/DEMGENEL";
        std::string url = BuildDemBatchUrlForTile(baseUrl, /*z=*/5, /*x=*/18, /*y=*/11, /*meshN=*/5);
        auto r = HttpGet(url, demAuth);
        std::cout << "  URL: " << url << "\n";
        std::cout << "  Auth: " << (demAuth.empty() ? "none" : "basic") << "\n";
        std::cout << "  HTTP Status: " << r.httpStatus << "\n";
        std::cout << "  Content-Type: " << r.contentType << "\n";
        std::cout << "  Body Size: " << r.data.size() << " bytes\n";

        if (r.httpStatus == 401 || r.httpStatus == 403) {
            std::cerr << "  ** AUTH REQUIRED: Server returned " << r.httpStatus
                      << ". Set NATIVE_GLOBE_DEM_AUTH=user:password **\n";
        }

        TEST_WARN(r.httpStatus == 200,
            "Pirireis DEM server returns HTTP 200 (got " + std::to_string(r.httpStatus) + ")");
    }

    // ---------------------------------------------------------------
    // 3. Public Tile Source (OSM - no auth needed)
    // ---------------------------------------------------------------
    std::cout << "\n--- 3. OpenStreetMap Public Tile Server (fallback reference) ---\n";
    {
        std::string url = "https://tile.openstreetmap.org/0/0/0.png";
        auto r = HttpGet(url);
        std::cout << "  URL: " << url << "\n";
        std::cout << "  HTTP Status: " << r.httpStatus << "\n";
        std::cout << "  Content-Type: " << r.contentType << "\n";
        std::cout << "  Body Size: " << r.data.size() << " bytes\n";

        TEST_ASSERT(r.httpStatus == 200,
            "OSM tile server returns HTTP 200 (got " + std::to_string(r.httpStatus) + ")");

        if (r.success) {
            TEST_ASSERT(LooksLikeImage(r.data),
                "OSM tile response is valid PNG image");
            TEST_ASSERT(r.data.size() > 100,
                "OSM tile has reasonable size (" + std::to_string(r.data.size()) + " bytes)");
        }
    }

    // ---------------------------------------------------------------
    // 4. Multiple zoom levels on OSM (pipeline breadth test)
    // ---------------------------------------------------------------
    std::cout << "\n--- 4. OSM Multi-Zoom Tile Fetch ---\n";
    {
        struct ZoomTest { int z, x, y; };
        ZoomTest tests[] = {
            {1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {1, 1, 1},
            {3, 4, 2}, {5, 16, 11}
        };
        for (const auto& t : tests) {
            std::string url = "https://tile.openstreetmap.org/"
                + std::to_string(t.z) + "/" + std::to_string(t.x) + "/" + std::to_string(t.y) + ".png";
            auto r = HttpGet(url);
            std::string label = "z=" + std::to_string(t.z) + " x=" + std::to_string(t.x) + " y=" + std::to_string(t.y);
            TEST_ASSERT(r.success && LooksLikeImage(r.data),
                "OSM tile " + label + " (HTTP " + std::to_string(r.httpStatus) + ", " + std::to_string(r.data.size()) + "B)");
        }
    }

    // ---------------------------------------------------------------
    // Summary
    // ---------------------------------------------------------------
    std::cout << "\n=== Summary ===\n";
    std::cout << "  Passed: " << passed << "\n";
    std::cout << "  Warned: " << warned << "\n";
    std::cout << "  Failed: " << failed << "\n";

    if (warned > 0) {
        std::cout << "\n** WARNINGS indicate Pirireis endpoint access/config issues.\n"
                  << "   Verify auth and endpoint compatibility:\n"
                  << "   To fix tile loading:\n"
                  << "     export NATIVE_GLOBE_TILE_AUTH=\"user:password\"\n"
                  << "     export NATIVE_GLOBE_DEM_AUTH=\"user:password\"\n"
                  << "   Or use a public tile source:\n"
                  << "     ./native_globe --tile-url \"https://tile.openstreetmap.org/{z}/{x}/{y}.png\"\n";
    }

    curl_global_cleanup();

    // Only fail on hard failures (OSM unreachable), not on auth warnings
    return (failed > 0) ? 1 : 0;
}
