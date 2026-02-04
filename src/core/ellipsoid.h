#pragma once

#include "lonlat.h"
#include <glm/glm.hpp>
#include <cmath>

namespace globe {

/**
 * Ellipsoid class (OpenGlobus port)
 * Represents a planetary ellipsoid with geodetic calculations
 */
class Ellipsoid {
public:
    // WGS84 constants
    static constexpr double WGS84_A = 6378137.0;           // Equatorial radius (meters)
    static constexpr double WGS84_B = 6356752.3142;        // Polar radius (meters)
    static constexpr double WGS84_A_KM = 6378.137;         // Equatorial radius (km)
    static constexpr double WGS84_B_KM = 6356.7523142;     // Polar radius (km)
    
    Ellipsoid(double equatorialSize = WGS84_A, double polarSize = WGS84_B);
    
    // Getters
    double GetEquatorialSize() const { return a_; }
    double GetPolarSize() const { return b_; }
    double GetFlattening() const { return flattening_; }
    double GetEccentricitySq() const { return e2_; }
    
    // Geodetic <-> Cartesian conversions
    glm::dvec3 GeodeticToCartesian(double lonDeg, double latDeg, double heightM) const;
    glm::dvec3 LonLatToCartesian(const LonLat& lonlat) const;
    LonLat CartesianToLonLat(const glm::dvec3& cart) const;
    
    // Project point to surface
    glm::dvec3 ProjectToSurface(const glm::dvec3& p) const;
    
    // Get surface normal at cartesian point
    glm::dvec3 GetSurfaceNormal(const glm::dvec3& cart) const;
    
    // Distance calculations
    double RhumbDistanceTo(const LonLat& start, const LonLat& end) const;
    double GreatCircleDistance(const LonLat& start, const LonLat& end) const;
    
    // Ray-ellipsoid intersection
    bool RayIntersection(const glm::dvec3& origin, const glm::dvec3& direction, 
                         double& t, glm::dvec3& hitPoint) const;
    
    // Singleton for WGS84
    static const Ellipsoid& WGS84();
    static const Ellipsoid& WGS84_KM();  // km units for camera

private:
    double a_;          // Equatorial radius
    double b_;          // Polar radius
    double flattening_;
    double a2_;         // a^2
    double b2_;         // b^2
    double e_;          // Eccentricity
    double e2_;         // e^2
    double k_;          // Second eccentricity
    double k2_;         // k^2
    
    glm::dvec3 radii_;
    glm::dvec3 radii2_;
    glm::dvec3 invRadii_;
    glm::dvec3 invRadii2_;
};

} // namespace globe
