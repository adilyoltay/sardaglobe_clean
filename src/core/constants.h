#pragma once

namespace globe {

// WGS84 Earth parameters
constexpr double EARTH_RADIUS_KM = 6378.137;          // Equatorial radius (km)
constexpr double EARTH_RADIUS_M = 6378137.0;          // Equatorial radius (m)
constexpr double EARTH_CIRCUMFERENCE_M = 40075017.0;  // Equatorial circumference (m)

// Unit conversion
constexpr double KM_TO_WORLD = 1.0;                   // World units = km
constexpr double M_TO_WORLD = 0.001;                  // Meters to world units

// Tile system
constexpr int TILE_SIZE_PX = 256;                     // Standard tile size
constexpr int MIN_ZOOM = 0;
constexpr int MAX_ZOOM = 22;

// Rendering
constexpr float DEFAULT_FOV_DEG = 45.0f;
constexpr float NEAR_PLANE = 0.01f;
constexpr float FAR_PLANE_FACTOR = 100.0f;            // FAR = EARTH_RADIUS * factor

// LOD Selection
constexpr float DEFAULT_SSE_THRESHOLD = 1.4f;         // Screen-space error threshold (pixels)

// Resource limits
constexpr int MAX_CONCURRENT_FETCHES = 64;
constexpr int MAX_CONCURRENT_DECODES = 32;
constexpr int MAX_TILES_IN_MEMORY = 2048;
constexpr int MAX_TEXTURE_UPLOADS_PER_FRAME = 16;
constexpr double TEXTURE_UPLOAD_BUDGET_MS = 4.0;      // Max ms for texture uploads per frame

// Download
constexpr double DOWNLOAD_TIMEOUT_SEC = 10.0;
constexpr double CONNECT_TIMEOUT_SEC = 5.0;

} // namespace globe
