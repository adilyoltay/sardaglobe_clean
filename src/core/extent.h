#pragma once

#include "lonlat.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace globe {

/**
 * Geographic extent (OpenGlobus Extent port)
 * Represents a bounding box in geographic coordinates
 */
struct Extent {
    LonLat southWest;  // SW corner (min lon, min lat)
    LonLat northEast;  // NE corner (max lon, max lat)
    
    Extent() = default;
    Extent(const LonLat& sw, const LonLat& ne) 
        : southWest(sw), northEast(ne) {}
    Extent(double minLon, double minLat, double maxLon, double maxLat)
        : southWest(minLon, minLat), northEast(maxLon, maxLat) {}
    
    // Accessors
    double West() const { return southWest.lon; }
    double South() const { return southWest.lat; }
    double East() const { return northEast.lon; }
    double North() const { return northEast.lat; }
    
    double Width() const { return northEast.lon - southWest.lon; }
    double Height() const { return northEast.lat - southWest.lat; }
    
    // Center point
    LonLat GetCenter() const {
        return LonLat(
            (southWest.lon + northEast.lon) * 0.5,
            (southWest.lat + northEast.lat) * 0.5
        );
    }
    
    // Check if point is inside extent
    bool Contains(double lon, double lat) const {
        return lon >= southWest.lon && lon <= northEast.lon &&
               lat >= southWest.lat && lat <= northEast.lat;
    }
    
    bool Contains(const LonLat& point) const {
        return Contains(point.lon, point.lat);
    }
    
    // Check if extents intersect
    bool Intersects(const Extent& other) const {
        return !(other.northEast.lon < southWest.lon ||
                 other.southWest.lon > northEast.lon ||
                 other.northEast.lat < southWest.lat ||
                 other.southWest.lat > northEast.lat);
    }
    
    // Merge with another extent
    void Merge(const Extent& other) {
        southWest.lon = std::min(southWest.lon, other.southWest.lon);
        southWest.lat = std::min(southWest.lat, other.southWest.lat);
        northEast.lon = std::max(northEast.lon, other.northEast.lon);
        northEast.lat = std::max(northEast.lat, other.northEast.lat);
    }
    
    // Create extent from tile coordinates (Mercator)
    static Extent FromTile(int x, int y, int z) {
        int numTiles = 1 << z;
        double lonSize = 360.0 / numTiles;
        double latSize = 180.0 / numTiles;
        
        double west = -180.0 + x * lonSize;
        double east = west + lonSize;
        double north = 90.0 - y * latSize;
        double south = north - latSize;
        
        return Extent(west, south, east, north);
    }
    
    // Create extent from WGS84 tile coordinates
    static Extent FromTileWGS84(int x, int y, int z) {
        int numTiles = 1 << z;
        double lonSize = 360.0 / numTiles;
        
        // WGS84 uses Web Mercator latitude limits
        double west = -180.0 + x * lonSize;
        double east = west + lonSize;
        
        // Convert tile Y to latitude using Mercator projection
        double n = M_PI - 2.0 * M_PI * y / numTiles;
        double north = RAD_TO_DEG * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
        
        n = M_PI - 2.0 * M_PI * (y + 1) / numTiles;
        double south = RAD_TO_DEG * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
        
        return Extent(west, south, east, north);
    }
    
    // Get children extents (for quadtree subdivision)
    void GetChildren(Extent& nw, Extent& ne, Extent& sw, Extent& se) const {
        LonLat center = GetCenter();
        
        sw = Extent(southWest.lon, southWest.lat, center.lon, center.lat);
        se = Extent(center.lon, southWest.lat, northEast.lon, center.lat);
        nw = Extent(southWest.lon, center.lat, center.lon, northEast.lat);
        ne = Extent(center.lon, center.lat, northEast.lon, northEast.lat);
    }
};

} // namespace globe
