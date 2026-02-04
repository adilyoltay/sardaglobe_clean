#include "ellipsoid.h"
#include <algorithm>

namespace globe {

namespace {
    constexpr double EPS1 = 0.1;
    constexpr double EPS12 = 1e-12;
}

Ellipsoid::Ellipsoid(double equatorialSize, double polarSize)
    : a_(equatorialSize)
    , b_(polarSize)
{
    flattening_ = (a_ - b_) / a_;
    a2_ = a_ * a_;
    b2_ = b_ * b_;
    
    double qa2b2 = std::sqrt(a2_ - b2_);
    e_ = qa2b2 / a_;
    e2_ = e_ * e_;
    k_ = qa2b2 / b_;
    k2_ = k_ * k_;
    
    radii_ = glm::dvec3(a_, a_, b_);
    radii2_ = glm::dvec3(a2_, a2_, b2_);
    invRadii_ = glm::dvec3(1.0 / a_, 1.0 / a_, 1.0 / b_);
    invRadii2_ = glm::dvec3(1.0 / a2_, 1.0 / a2_, 1.0 / b2_);
}

const Ellipsoid& Ellipsoid::WGS84() {
    static Ellipsoid wgs84(WGS84_A, WGS84_B);
    return wgs84;
}

const Ellipsoid& Ellipsoid::WGS84_KM() {
    static Ellipsoid wgs84km(WGS84_A_KM, WGS84_B_KM);
    return wgs84km;
}

glm::dvec3 Ellipsoid::GeodeticToCartesian(double lonDeg, double latDeg, double heightM) const {
    double latRad = latDeg * DEG_TO_RAD;
    double lonRad = lonDeg * DEG_TO_RAD;
    
    double sinLat = std::sin(latRad);
    double cosLat = std::cos(latRad);
    double sinLon = std::sin(lonRad);
    double cosLon = std::cos(lonRad);
    
    // Prime vertical radius of curvature
    double N = a_ / std::sqrt(1.0 - e2_ * sinLat * sinLat);
    double nc = (N + heightM) * cosLat;
    
    return glm::dvec3(
        nc * cosLon,
        nc * sinLon,
        (N * (1.0 - e2_) + heightM) * sinLat
    );
}

glm::dvec3 Ellipsoid::LonLatToCartesian(const LonLat& lonlat) const {
    return GeodeticToCartesian(lonlat.lon, lonlat.lat, lonlat.height);
}

glm::dvec3 Ellipsoid::GetSurfaceNormal(const glm::dvec3& cart) const {
    glm::dvec3 n = cart * invRadii2_;
    return glm::normalize(n);
}

glm::dvec3 Ellipsoid::ProjectToSurface(const glm::dvec3& p) const {
    double length = glm::length(p);
    if (length == 0.0) {
        return LonLatToCartesian(LonLat());
    }
    
    double x2 = p.x * p.x * invRadii2_.x;
    double y2 = p.y * p.y * invRadii2_.y;
    double z2 = p.z * p.z * invRadii2_.z;
    
    double norm = x2 + y2 + z2;
    double ratio = std::sqrt(1.0 / norm);
    glm::dvec3 first = p * ratio;
    
    if (norm < EPS1) {
        return std::isfinite(ratio) ? first : glm::dvec3(0.0);
    }
    
    glm::dvec3 scaled = first * invRadii2_;
    double lambda = ((1.0 - ratio) * length) / glm::length(scaled);
    
    double mX, mY, mZ;
    
    for (int iter = 0; iter < 100; ++iter) {
        mX = 1.0 / (1.0 + lambda * invRadii2_.x);
        mY = 1.0 / (1.0 + lambda * invRadii2_.y);
        mZ = 1.0 / (1.0 + lambda * invRadii2_.z);
        
        double mX2 = mX * mX;
        double mY2 = mY * mY;
        double mZ2 = mZ * mZ;
        
        double func = x2 * mX2 + y2 * mY2 + z2 * mZ2 - 1.0;
        
        if (std::abs(func) < EPS12) {
            break;
        }
        
        double mX3 = mX2 * mX;
        double mY3 = mY2 * mY;
        double mZ3 = mZ2 * mZ;
        
        lambda += 0.5 * func / (x2 * mX3 * invRadii2_.x + 
                                 y2 * mY3 * invRadii2_.y + 
                                 z2 * mZ3 * invRadii2_.z);
    }
    
    return glm::dvec3(p.x * mX, p.y * mY, p.z * mZ);
}

LonLat Ellipsoid::CartesianToLonLat(const glm::dvec3& cart) const {
    glm::dvec3 surfacePoint = ProjectToSurface(cart);
    glm::dvec3 n = GetSurfaceNormal(surfacePoint);
    glm::dvec3 h = cart - surfacePoint;
    
    LonLat result;
    result.lon = std::atan2(n.y, n.x) * RAD_TO_DEG;
    result.lat = std::asin(std::clamp(n.z, -1.0, 1.0)) * RAD_TO_DEG;
    result.height = (glm::dot(h, cart) >= 0 ? 1.0 : -1.0) * glm::length(h);
    
    return result;
}

double Ellipsoid::RhumbDistanceTo(const LonLat& start, const LonLat& end) const {
    double f1 = start.lat * DEG_TO_RAD;
    double f2 = end.lat * DEG_TO_RAD;
    double df = f2 - f1;
    double dLon = std::abs(end.lon - start.lon) * DEG_TO_RAD;
    
    if (std::abs(dLon) > M_PI) {
        dLon = dLon > 0 ? -(2 * M_PI - dLon) : (2 * M_PI + dLon);
    }
    
    double dd = std::log(std::tan(f2 / 2 + M_PI / 4) / std::tan(f1 / 2 + M_PI / 4));
    double q = std::abs(dd) > 1e-11 ? df / dd : std::cos(f1);
    double t = std::sqrt(df * df + q * q * dLon * dLon);
    
    return t * a_;
}

double Ellipsoid::GreatCircleDistance(const LonLat& start, const LonLat& end) const {
    double lat1 = start.lat * DEG_TO_RAD;
    double lat2 = end.lat * DEG_TO_RAD;
    double dLat = lat2 - lat1;
    double dLon = (end.lon - start.lon) * DEG_TO_RAD;
    
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1) * std::cos(lat2) * 
               std::sin(dLon / 2) * std::sin(dLon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    
    return a_ * c;
}

bool Ellipsoid::RayIntersection(const glm::dvec3& origin, const glm::dvec3& direction, 
                                 double& t, glm::dvec3& hitPoint) const {
    // Scale ray to unit sphere space
    glm::dvec3 scaledOrigin = origin * invRadii_;
    glm::dvec3 scaledDir = direction * invRadii_;
    
    // Quadratic coefficients for unit sphere
    double a = glm::dot(scaledDir, scaledDir);
    double b = 2.0 * glm::dot(scaledOrigin, scaledDir);
    double c = glm::dot(scaledOrigin, scaledOrigin) - 1.0;
    
    double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) {
        return false;
    }
    
    double sqrtD = std::sqrt(discriminant);
    double t1 = (-b - sqrtD) / (2.0 * a);
    double t2 = (-b + sqrtD) / (2.0 * a);
    
    // Pick closest positive t
    t = (t1 > 0.0) ? t1 : t2;
    if (t < 0.0) {
        return false;
    }
    
    hitPoint = origin + t * direction;
    return true;
}

} // namespace globe
