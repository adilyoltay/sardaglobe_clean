#pragma once

#include "../core/constants.h"
#include "../core/tile_key.h"
#include <glm/glm.hpp>
#include <cmath>

namespace globe {

// Tile coordinate conversions
inline double Tile2Lon(int x, int z) {
    double n = static_cast<double>(1 << z);
    return (static_cast<double>(x) / n) * 360.0 - 180.0;
}

inline double Tile2Lat(int y, int z) {
    double n = static_cast<double>(1 << z);
    double latRad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * y / n)));
    return latRad * 180.0 / M_PI;
}

inline std::pair<int, int> LatLon2Tile(double lat, double lon, int z) {
    double n = static_cast<double>(1 << z);
    double latRad = lat * M_PI / 180.0;
    int x = static_cast<int>((lon + 180.0) / 360.0 * n);
    int y = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);
    return {x, y};
}

// Tile center in lat/lon
inline std::pair<double, double> TileCenterLatLon(const TileKey& key) {
    double lonLeft = Tile2Lon(key.x, key.level);
    double lonRight = Tile2Lon(key.x + 1, key.level);
    double latTop = Tile2Lat(key.y, key.level);
    double latBottom = Tile2Lat(key.y + 1, key.level);
    return {(latTop + latBottom) * 0.5, (lonLeft + lonRight) * 0.5};
}

// Convert lat/lon to 3D sphere position (world units = km)
inline glm::vec3 LatLonToWorld(double latDeg, double lonDeg, double altitude = 0.0) {
    double latRad = latDeg * M_PI / 180.0;
    double lonRad = lonDeg * M_PI / 180.0;
    double r = EARTH_RADIUS_KM + altitude * M_TO_WORLD;
    return glm::vec3(
        std::cos(latRad) * std::cos(lonRad) * r,
        std::cos(latRad) * std::sin(lonRad) * r,
        std::sin(latRad) * r
    );
}

// Tile center normal (unit vector)
inline glm::vec3 TileCenterNormal(const TileKey& key) {
    auto [lat, lon] = TileCenterLatLon(key);
    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    return glm::vec3(
        std::cos(latRad) * std::cos(lonRad),
        std::cos(latRad) * std::sin(lonRad),
        std::sin(latRad)
    );
}

// Tile center in world coordinates
inline glm::vec3 TileCenterWorld(const TileKey& key) {
    return TileCenterNormal(key) * static_cast<float>(EARTH_RADIUS_KM);
}

// Angular radius of tile (radians)
inline float TileAngularRadius(const TileKey& key) {
    auto [lat, lon] = TileCenterLatLon(key);
    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    glm::vec3 center(std::cos(latRad) * std::cos(lonRad),
                     std::cos(latRad) * std::sin(lonRad),
                     std::sin(latRad));
    
    // Check all four corners
    double lons[2] = {Tile2Lon(key.x, key.level), Tile2Lon(key.x + 1, key.level)};
    double lats[2] = {Tile2Lat(key.y, key.level), Tile2Lat(key.y + 1, key.level)};
    
    float maxAngle = 0.0f;
    for (double cornerLat : lats) {
        for (double cornerLon : lons) {
            double cLatRad = cornerLat * M_PI / 180.0;
            double cLonRad = cornerLon * M_PI / 180.0;
            glm::vec3 corner(std::cos(cLatRad) * std::cos(cLonRad),
                            std::cos(cLatRad) * std::sin(cLonRad),
                            std::sin(cLatRad));
            float dot = glm::clamp(glm::dot(center, corner), -1.0f, 1.0f);
            float angle = std::acos(dot);
            maxAngle = std::max(maxAngle, angle);
        }
    }
    return maxAngle;
}

// Bounding sphere radius in world units (base version - terrain elevation ignored)
inline float TileBoundingRadius(const TileKey& key) {
    float angular = TileAngularRadius(key);
    return 2.0f * std::sin(angular * 0.5f) * static_cast<float>(EARTH_RADIUS_KM);
}

// Bounding sphere radius with elevation and skirt offset (conservative culling)
// Use this for horizon culling of high terrain tiles (Himalayas, etc.)
inline float TileBoundingRadius(const TileKey& key, float maxElevationKm, float skirtDepthKm = 0.4f) {
    float angular = TileAngularRadius(key);
    float baseRadius = 2.0f * std::sin(angular * 0.5f) * static_cast<float>(EARTH_RADIUS_KM);
    // Add elevation and skirt depth for conservative bounding (prevents false-positive culling)
    return baseRadius + maxElevationKm + skirtDepthKm;
}

// Geometric error for SSE calculation (meters)
inline float ComputeGeometricError(int level) {
    return static_cast<float>(EARTH_CIRCUMFERENCE_M / (std::pow(2.0, level) * TILE_SIZE_PX));
}

// Latitude-aware geometric error (meters per pixel at tile center)
inline float ComputeGeometricError(const TileKey& key) {
    auto [lat, lon] = TileCenterLatLon(key);
    double latRad = lat * M_PI / 180.0;
    double metersPerTile = EARTH_CIRCUMFERENCE_M * std::cos(latRad) / static_cast<double>(1 << key.level);
    return static_cast<float>(metersPerTile / TILE_SIZE_PX);
}

// Screen-space error calculation
inline float ComputeSSE(int level, double distanceMeters, int viewportHeight, float fovDegrees) {
    float geometricError = ComputeGeometricError(level);
    float fovRad = fovDegrees * static_cast<float>(M_PI) / 180.0f;
    float sseFactor = static_cast<float>(viewportHeight) / (2.0f * std::tan(fovRad / 2.0f));
    return (geometricError / static_cast<float>(distanceMeters)) * sseFactor;
}

inline float ComputeSSE(const TileKey& key, double distanceMeters, int viewportHeight, float fovDegrees) {
    float geometricError = ComputeGeometricError(key);
    float fovRad = fovDegrees * static_cast<float>(M_PI) / 180.0f;
    float sseFactor = static_cast<float>(viewportHeight) / (2.0f * std::tan(fovRad / 2.0f));
    return (geometricError / static_cast<float>(distanceMeters)) * sseFactor;
}

} // namespace globe
