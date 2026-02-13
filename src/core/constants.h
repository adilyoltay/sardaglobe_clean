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
// Google Earth parity: 2.0f is GE standard quality mode threshold.
// Lower values = more tiles (1.4f = ~43% more tiles than GE).
constexpr float DEFAULT_SSE_THRESHOLD = 2.0f;         // Screen-space error threshold (pixels) - GE standard

// Resource limits
constexpr int MAX_CONCURRENT_FETCHES = 16;
constexpr int MAX_CONCURRENT_DECODES = 8;
constexpr int MAX_IN_FLIGHT_FETCHES = 64;
constexpr int MAX_TILES_IN_MEMORY = 2048;
constexpr int MAX_TEXTURE_UPLOADS_PER_FRAME = 8;
constexpr double TEXTURE_UPLOAD_BUDGET_MS = 2.0;      // Max ms for texture uploads per frame
constexpr double MESH_UPLOAD_BUDGET_MS = 2.0;         // Max ms for mesh uploads per frame
constexpr int MAX_EVICTS_PER_FRAME = 32;
constexpr double EVICT_BUDGET_MS = 2.0;               // Max ms for eviction per frame
constexpr int MESH_SCHEDULER_WORKERS = 4;

// Download
constexpr double DOWNLOAD_TIMEOUT_SEC = 10.0;
constexpr double CONNECT_TIMEOUT_SEC = 5.0;

} // namespace globe
