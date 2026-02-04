#pragma once

#include <string>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <ostream>

// Google Earth style TileKey with QuadKey support
// Enables efficient tile hierarchy navigation and cache lookup
struct TileKey {
  int level = 0;  // Zoom level (0 = root)
  int x = 0;      // Column [0, 2^level)
  int y = 0;      // Row [0, 2^level)
  
  TileKey() = default;
  TileKey(int l, int tx, int ty) : level(l), x(tx), y(ty) {}
  
  friend std::ostream& operator<<(std::ostream& os, const TileKey& k) {
    return os << k.ToString();
  }
  
  // Create from "z/x/y" string format
  static TileKey FromString(const std::string& key) {
    TileKey result;
    std::sscanf(key.c_str(), "%d/%d/%d", &result.level, &result.x, &result.y);
    return result;
  }
  
  // Convert to "z/x/y" string format
  std::string ToString() const {
    return std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(y);
  }
  
  // Convert to QuadKey string (e.g., "0312" for Bing Maps style addressing)
  // Each digit represents a quadrant: 0=TL, 1=TR, 2=BL, 3=BR
  std::string ToQuadKey() const {
    std::string key;
    key.reserve(level);
    for (int i = level; i > 0; --i) {
      char digit = '0';
      int mask = 1 << (i - 1);
      if ((x & mask) != 0) digit += 1;  // Right half
      if ((y & mask) != 0) digit += 2;  // Bottom half
      key += digit;
    }
    return key;
  }
  
  // Create from QuadKey string
  static TileKey FromQuadKey(const std::string& quadkey) {
    TileKey result;
    result.level = static_cast<int>(quadkey.length());
    result.x = 0;
    result.y = 0;
    for (size_t i = 0; i < quadkey.length(); ++i) {
      result.x <<= 1;
      result.y <<= 1;
      char c = quadkey[i];
      if (c == '1' || c == '3') result.x |= 1;
      if (c == '2' || c == '3') result.y |= 1;
    }
    return result;
  }
  
  // Get parent tile (one zoom level up)
  TileKey GetParent() const {
    if (level == 0) return *this;
    return TileKey(level - 1, x / 2, y / 2);
  }
  
  // Get all 4 children tiles (one zoom level down)
  std::array<TileKey, 4> GetChildren() const {
    int childX = x * 2;
    int childY = y * 2;
    int childLevel = level + 1;
    return {{
      TileKey(childLevel, childX, childY),         // Top-left
      TileKey(childLevel, childX + 1, childY),     // Top-right
      TileKey(childLevel, childX, childY + 1),     // Bottom-left
      TileKey(childLevel, childX + 1, childY + 1)  // Bottom-right
    }};
  }
  
  // Get child index (0-3) within parent
  int GetChildIndex() const {
    return (x & 1) + ((y & 1) << 1);
  }
  
  // Get specific child by index (0=TL, 1=TR, 2=BL, 3=BR)
  TileKey GetChild(int index) const {
    int childX = x * 2 + (index & 1);
    int childY = y * 2 + ((index >> 1) & 1);
    return TileKey(level + 1, childX, childY);
  }
  
  // Neighbor navigation (wraps around X axis, clamps Y axis)
  TileKey GetNeighborNorth() const {
    if (y == 0) return *this;  // No neighbor above top row
    return TileKey(level, x, y - 1);
  }
  
  TileKey GetNeighborSouth() const {
    int maxY = (1 << level) - 1;
    if (y >= maxY) return *this;  // No neighbor below bottom row
    return TileKey(level, x, y + 1);
  }
  
  TileKey GetNeighborEast() const {
    int n = 1 << level;
    return TileKey(level, (x + 1) % n, y);  // Wrap around
  }
  
  TileKey GetNeighborWest() const {
    int n = 1 << level;
    return TileKey(level, (x - 1 + n) % n, y);  // Wrap around
  }
  
  // Check if this tile is an ancestor of another tile
  bool IsAncestorOf(const TileKey& other) const {
    if (level >= other.level) return false;
    int levelDiff = other.level - level;
    return (other.x >> levelDiff) == x && (other.y >> levelDiff) == y;
  }
  
  // Check if this tile is a descendant of another tile
  bool IsDescendantOf(const TileKey& other) const {
    return other.IsAncestorOf(*this);
  }
  
  // Get the ancestor at a specific level
  TileKey GetAncestorAtLevel(int targetLevel) const {
    if (targetLevel >= level) return *this;
    int levelDiff = level - targetLevel;
    return TileKey(targetLevel, x >> levelDiff, y >> levelDiff);
  }
  
  // Comparison operators
  bool operator==(const TileKey& other) const {
    return level == other.level && x == other.x && y == other.y;
  }
  
  bool operator!=(const TileKey& other) const {
    return !(*this == other);
  }
  
  bool operator<(const TileKey& other) const {
    if (level != other.level) return level < other.level;
    if (y != other.y) return y < other.y;
    return x < other.x;
  }
  
  // Hash function for use with std::unordered_map
  struct Hash {
    size_t operator()(const TileKey& key) const {
      // Phase 3 Fix: Use a robust 64-bit pack to avoid collisions at levels > 10.
      // level(5 bits) | y(26 bits) | x(26 bits) = 57 bits total.
      uint64_t h = static_cast<uint64_t>(key.level) & 0x1F;
      h = (h << 26) | (static_cast<uint64_t>(key.y) & 0x3FFFFFF);
      h = (h << 26) | (static_cast<uint64_t>(key.x) & 0x3FFFFFF);
      return static_cast<size_t>(h);
    }
  };
};

// Geographic bounds of a tile
struct TileBounds {
  double west = 0.0;   // Minimum longitude (degrees)
  double east = 0.0;   // Maximum longitude (degrees)
  double south = 0.0;  // Minimum latitude (degrees)
  double north = 0.0;  // Maximum latitude (degrees)
  
  // Create bounds from TileKey (Web Mercator projection)
  static TileBounds FromTileKey(const TileKey& key) {
    TileBounds bounds;
    int n = 1 << key.level;
    
    // Longitude: linear mapping
    bounds.west = static_cast<double>(key.x) / n * 360.0 - 180.0;
    bounds.east = static_cast<double>(key.x + 1) / n * 360.0 - 180.0;
    
    // Latitude: inverse Mercator projection
    double y1 = static_cast<double>(key.y) / n;
    double y2 = static_cast<double>(key.y + 1) / n;
    
    constexpr double PI = 3.14159265358979323846;
    bounds.north = 180.0 / PI * std::atan(std::sinh(PI * (1.0 - 2.0 * y1)));
    bounds.south = 180.0 / PI * std::atan(std::sinh(PI * (1.0 - 2.0 * y2)));
    
    return bounds;
  }
  
  // Check if a point is inside the bounds
  bool Contains(double lat, double lon) const {
    return lon >= west && lon <= east && lat >= south && lat <= north;
  }
  
  // Check if bounds intersect with another bounds
  bool Intersects(const TileBounds& other) const {
    return !(other.west > east || other.east < west ||
             other.south > north || other.north < south);
  }
  
  // Get center point
  void GetCenter(double& lat, double& lon) const {
    lon = (west + east) / 2.0;
    lat = (south + north) / 2.0;
  }
  
  // Get width and height in degrees
  double GetWidth() const { return east - west; }
  double GetHeight() const { return north - south; }
};

// Tile URL generation with template substitution
class TileUrlGenerator {
public:
  TileUrlGenerator(const std::string& url_pattern)
      : pattern_(url_pattern) {}
  
  // Replace template variables
  std::string GenerateUrl(const TileKey& tile) const {
    std::string url = pattern_;
    
    // Replace {z}, {level}
    ReplaceAll(url, "{z}", std::to_string(tile.level));
    ReplaceAll(url, "{level}", std::to_string(tile.level));
    
    // Replace {x}
    ReplaceAll(url, "{x}", std::to_string(tile.x));
    
    // Replace {y}
    ReplaceAll(url, "{y}", std::to_string(tile.y));
    
    // Replace {-y} (TMS inverted)
    int max_y = (1 << tile.level) - 1;
    ReplaceAll(url, "{-y}", std::to_string(max_y - tile.y));
    
    // Replace {quadkey}
    ReplaceAll(url, "{quadkey}", tile.ToQuadKey());
    
    // Replace {bbox}
    TileBounds bounds = TileBounds::FromTileKey(tile);
    char bbox[256];
    std::snprintf(bbox, sizeof(bbox), "%.6f,%.6f,%.6f,%.6f",
            bounds.west, bounds.south, bounds.east, bounds.north);
    ReplaceAll(url, "{bbox}", bbox);
    
    return url;
  }

private:
  std::string pattern_;
  
  void ReplaceAll(std::string& str, const std::string& from, 
                 const std::string& to) const {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.length(), to);
      pos += to.length();
    }
  }
};

namespace TileMath {

// Find tile containing a point at given level
inline TileKey LatLonToTileKey(double lat, double lon, int level) {
  int n = 1 << level;
  
  // X from longitude
  int x = static_cast<int>((lon + 180.0) / 360.0 * n);
  x = std::max(0, std::min(x, n - 1));
  
  // Y from latitude (Mercator projection)
  constexpr double PI = 3.14159265358979323846;
  double lat_rad = lat * PI / 180.0;
  double mercator_y = std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad));
  int y = static_cast<int>((1.0 - mercator_y / PI) / 2.0 * n);
  y = std::max(0, std::min(y, n - 1));
  
  return TileKey(level, x, y);
}

// Distance between two tiles (in tile units)
inline int TileDistance(const TileKey& a, const TileKey& b) {
  if (a.level != b.level) {
    return -1;  // Different levels, not comparable
  }
  
  int dx = std::abs(a.x - b.x);
  int dy = std::abs(a.y - b.y);
  return std::max(dx, dy);  // Chebyshev distance
}

// Check if tile contains geographic point
inline bool TileContainsPoint(const TileKey& tile, double lat, double lon) {
  TileBounds bounds = TileBounds::FromTileKey(tile);
  return bounds.Contains(lat, lon);
}

} // namespace TileMath