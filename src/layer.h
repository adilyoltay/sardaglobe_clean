#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include <glm/glm.hpp>

enum class GeometryType {
  Point,
  Line,
  Polygon,
  MultiPoint,
  MultiLine,
  MultiPolygon
};

enum class LayerType {
  ObjectArray,    // CS_OBJECT_ARRAY - local data
  MVT_XYZ,        // Vector tile layer
  WFS             // OGC WFS layer
};

enum class IconType {
  NOICON = 0,
  MAP = 1,
  CIRCLE = 2
};

struct IconParams {
  std::string mapName;
  std::string name;
  glm::vec4 borderColor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
  glm::vec4 fillColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  float borderWidth = 2.0f;
  float radius = 16.0f;
  float sizeX = 32.0f;
  float sizeY = 32.0f;
  float rotDeg = 0.0f;
};

struct LayerStyle {
  // Fill
  glm::vec4 fillColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f); // JS: #ffff00
  bool filled = true;
  
  // Stroke
  glm::vec4 borderColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // JS: #000000
  bool border = true;
  float strokeWidth = 2.0f;
  
  // Point/Icon
  IconType iconType = IconType::NOICON;
  IconParams icon;
  
  // Backward compatibility
  float pointSize = 8.0f;
  glm::vec4 pointColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  
  // Label
  bool showLabel = false;
  std::string labelField;
  std::string labelText; // Static text override
  float labelSize = 20.0f;
  glm::vec4 labelColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  glm::vec4 labelHaloColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  float labelHaloWidth = 2.0f;
  
  // Flash
  bool flashIcon = false;
  bool flashLabels = false;
  
  // Visibility
  int minZoom = 2; // GLOBE_DEFAULT_MIN_LOD
  int maxZoom = 25;
  float opacity = 1.0f;
  bool active = true;
  bool depthTest = false;
  bool cullFace = true;
};

struct PropertyValue {
  enum class Type { Null, Number, String, Boolean };
  Type type = Type::Null;
  double numberVal = 0.0;
  std::string stringVal;
  bool boolVal = false;
  
  static PropertyValue Null() { return PropertyValue(); }
  static PropertyValue Number(double v) { PropertyValue p; p.type = Type::Number; p.numberVal = v; return p; }
  static PropertyValue String(const std::string& v) { PropertyValue p; p.type = Type::String; p.stringVal = v; return p; }
  static PropertyValue Boolean(bool v) { PropertyValue p; p.type = Type::Boolean; p.boolVal = v; return p; }
  
  double AsNumber(double fallback = 0.0) const { return type == Type::Number ? numberVal : fallback; }
  std::string AsString(const std::string& fallback = "") const { return type == Type::String ? stringVal : fallback; }
  bool AsBool(bool fallback = false) const { return type == Type::Boolean ? boolVal : fallback; }
};

struct Feature {
  std::string id;
  GeometryType geometryType = GeometryType::Point;
  
  // Coordinates: flat array [lon, lat, lon, lat, ...]
  std::vector<double> coordinates;
  
  // For multi-geometries: indices where each part starts
  std::vector<size_t> partIndices;
  
  // Properties
  std::unordered_map<std::string, PropertyValue> properties;
  
  // Render state
  bool selected = false;
  bool visible = true;
  
  // Bounding box (computed)
  double minLon = 180.0, maxLon = -180.0;
  double minLat = 90.0, maxLat = -90.0;
  
  void ComputeBounds() {
    minLon = 180.0; maxLon = -180.0;
    minLat = 90.0; maxLat = -90.0;
    for (size_t i = 0; i + 1 < coordinates.size(); i += 2) {
      double lon = coordinates[i];
      double lat = coordinates[i + 1];
      if (lon < minLon) minLon = lon;
      if (lon > maxLon) maxLon = lon;
      if (lat < minLat) minLat = lat;
      if (lat > maxLat) maxLat = lat;
    }
  }
};

struct Layer {
  std::string id;
  std::string name;
  LayerType type = LayerType::ObjectArray;
  
  // Visibility
  bool visible = true;
  float opacity = 1.0f;
  
  // Style
  LayerStyle style;
  LayerStyle selectedStyle;
  
  // Data
  std::vector<Feature> features;
  
  // Selection
  std::vector<std::string> selectedIds;
  
  // Draw order (higher = on top)
  int drawOrder = 0;
  
  // Render buffers (GPU resources)
  uint32_t pointVao = 0;
  uint32_t pointVbo = 0;
  uint32_t pointUvVbo = 0;
  size_t pointCount = 0;
  
  uint32_t lineVao = 0;
  uint32_t lineVbo = 0;
  size_t lineVertexCount = 0;
  
  uint32_t fillVao = 0;
  uint32_t fillVbo = 0;
  size_t fillVertexCount = 0;
  
  // Labels
  std::vector<int> labelIds;
  
  bool dirty = true;  // Needs rebuild
  
  // Bounding box of all features
  double minLon = 180.0, maxLon = -180.0;
  double minLat = 90.0, maxLat = -90.0;
  
  void ComputeBounds() {
    minLon = 180.0; maxLon = -180.0;
    minLat = 90.0; maxLat = -90.0;
    for (const auto& f : features) {
      if (f.minLon < minLon) minLon = f.minLon;
      if (f.maxLon > maxLon) maxLon = f.maxLon;
      if (f.minLat < minLat) minLat = f.minLat;
      if (f.maxLat > maxLat) maxLat = f.maxLat;
    }
  }
};
