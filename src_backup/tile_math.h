#pragma once

#include <glm/glm.hpp>
#include <cmath>

// ============================================================================
// TILE MATH UTILITIES
// Shared tile geometry calculations used by TileLodSelector and GlobeEngine
// Created: 2026-02-04 (consolidated from duplicates)
// ============================================================================

namespace earth {

// Globe constants
constexpr double GLOBE_RADIUS = 6378.137;      // km (WGS84 equatorial radius)
constexpr double GLOBE_RADIUS_K = 0.001;       // km to world units conversion
constexpr double EARTH_CIRCUMFERENCE = 40075017.0;  // meters
constexpr int TILE_SIZE = 256;                 // pixels

// Mathematical constants
constexpr double PI_VAL = 3.14159265358979323846;

// ============================================================================
// Coordinate Conversions (Web Mercator / EPSG:3857)
// ============================================================================

inline double Tile2Lon(int x, int z) {
    return static_cast<double>(x) / static_cast<double>(1 << z) * 360.0 - 180.0;
}

inline double Tile2Lat(int y, int z) {
    double n = PI_VAL - 2.0 * PI_VAL * static_cast<double>(y) / static_cast<double>(1 << z);
    return 180.0 / PI_VAL * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

// ============================================================================
// Tile Geometry Functions
// ============================================================================

// Returns the center of a tile as a unit normal vector on the globe surface
inline glm::vec3 TileCenterNormal(int z, int x, int y) {
    double n = static_cast<double>(1 << z);
    double lonRad = (x + 0.5) / n * 2.0 * PI_VAL - PI_VAL;
    double mercY = (y + 0.5) / n;
    double latRad = std::atan(std::sinh(PI_VAL * (1.0 - 2.0 * mercY)));
    
    return glm::vec3(
        static_cast<float>(std::cos(latRad) * std::cos(lonRad)),
        static_cast<float>(std::sin(latRad)),
        static_cast<float>(std::cos(latRad) * std::sin(lonRad))
    );
}

// Returns the angular radius of a tile (in radians)
inline float TileAngularRadius(int z, int /*x*/, int /*y*/) {
    double n = static_cast<double>(1 << z);
    double angularSize = PI_VAL / n;  // Radians per tile
    return static_cast<float>(angularSize * 0.7071);  // sqrt(2)/2 for diagonal
}

// Returns the bounding sphere radius of a tile in world units
inline float TileBoundingRadius(int z, int x, int y) {
    float angRad = TileAngularRadius(z, x, y);
    return static_cast<float>(GLOBE_RADIUS * std::sin(angRad) * 1.5);
}

// Returns the center of a tile in world coordinates (scaled by GLOBE_RADIUS)
inline glm::vec3 TileCenterWorld(int z, int x, int y) {
    return TileCenterNormal(z, x, y) * static_cast<float>(GLOBE_RADIUS);
}

// ============================================================================
// Screen-Space Error (SSE) Functions
// ============================================================================

// Returns geometric error in meters for a tile at given zoom level
// This represents the real-world size of one pixel at this LOD
inline float ComputeGeometricError(int z) {
    return static_cast<float>(EARTH_CIRCUMFERENCE / (std::pow(2.0, z) * TILE_SIZE));
}

// Computes Screen-Space Error in pixels
// SSE = (geometricError / distance) * (height / (2 * tan(fov/2)))
inline float ComputeGeometricSSE(int z, double distanceMeters, int viewportHeight, float fovDegrees) {
    float geometricError = ComputeGeometricError(z);
    float fovRad = glm::radians(fovDegrees);
    float sseFactor = static_cast<float>(viewportHeight) / (2.0f * std::tan(fovRad / 2.0f));
    return (geometricError / static_cast<float>(distanceMeters)) * sseFactor;
}

// Computes SSE ratio for LOD decision (ratio > 1.0 means subdivide)
inline float ComputeTileSseRatio(const glm::vec3& cameraPos, int viewportHeight, float fovRad,
                                  int z, int x, int y, float sseThreshold, float tiltFactor) {
    glm::vec3 center = TileCenterNormal(z, x, y) * static_cast<float>(GLOBE_RADIUS);
    float radius = TileBoundingRadius(z, x, y);
    float distance = glm::length(center - cameraPos);
    
    // Distance to closest point on bounding sphere
    distance = std::max(1.0f, distance - radius);
    
    // Convert to meters
    double distanceMeters = static_cast<double>(distance) / GLOBE_RADIUS_K;
    
    float ssePx = ComputeGeometricSSE(z, distanceMeters, viewportHeight, glm::degrees(fovRad));
    return (ssePx * tiltFactor) / sseThreshold;
}

} // namespace earth
