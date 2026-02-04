#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace globe {

// Constants
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / M_PI;

/**
 * Geographic coordinate (OpenGlobus LonLat port)
 * Represents longitude, latitude, and height
 */
struct LonLat {
    double lon = 0.0;     // Longitude in degrees
    double lat = 0.0;     // Latitude in degrees
    double height = 0.0;  // Height in meters
    
    LonLat() = default;
    LonLat(double lon_, double lat_, double height_ = 0.0)
        : lon(lon_), lat(lat_), height(height_) {}
    
    // Check if zero
    bool IsZero() const {
        return lon == 0.0 && lat == 0.0 && height == 0.0;
    }
    
    // Clone
    LonLat Clone() const {
        return LonLat(lon, lat, height);
    }
    
    // Equality
    bool operator==(const LonLat& other) const {
        return lon == other.lon && lat == other.lat && height == other.height;
    }
    
    // Radians conversion
    double LonRad() const { return lon * DEG_TO_RAD; }
    double LatRad() const { return lat * DEG_TO_RAD; }
    
    // Set from array [lon, lat, height]
    void FromArray(const double* arr) {
        lon = arr[0];
        lat = arr[1];
        height = arr[2];
    }
    
    // To glm vec3 (lon, lat, height)
    glm::dvec3 ToVec3() const {
        return glm::dvec3(lon, lat, height);
    }
    
    // Create from glm vec3
    static LonLat FromVec3(const glm::dvec3& v) {
        return LonLat(v.x, v.y, v.z);
    }
};

} // namespace globe
