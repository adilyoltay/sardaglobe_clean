#include "globe_api.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {

struct ObjectArrData {
  Value coords = Value::Array();
  Value coordsZ = Value::Array();
  Value attribs = Value::Array();

  bool Empty() const {
    return coords.Size() == 0 && attribs.Size() == 0;
  }

  Value ToValueArray() const {
    Value obj = Value::Object();
    obj.Set("coords", coords);
    obj.Set("coordsZ", coordsZ);
    obj.Set("attribs", attribs);
    Value arr = Value::Array();
    arr.Push(obj);
    return arr;
  }
};

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string ToUpper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

Value CloneProps(const Value& props) {
  Value out = Value::Object();
  if (!props.IsObject()) return out;
  for (const auto& entry : props.object) {
    if (entry.second) {
      out.Set(entry.first, *entry.second);
    }
  }
  return out;
}

void AppendCoords(const Value& coords, Value& coordsArr, Value& coordsZArr) {
  if (!coords.IsArray()) return;
  if (coords.Size() >= 2 && coords.At(0).IsNumber()) {
    double lon = coords.At(0).AsNumber();
    double lat = coords.At(1).AsNumber();
    coordsArr.Push(Value::Number(lon));
    coordsArr.Push(Value::Number(lat));
    double z = coords.At(2).IsNumber() ? coords.At(2).AsNumber() : 0.0;
    coordsZArr.Push(Value::Number(z));
    return;
  }
  for (size_t i = 0; i < coords.Size(); ++i) {
    AppendCoords(coords.At(i), coordsArr, coordsZArr);
  }
}

void AddFeature(ObjectArrData& bucket, const Value& coords, const Value& props) {
  AppendCoords(coords, bucket.coords, bucket.coordsZ);
  bucket.attribs.Push(CloneProps(props));
}

void ParseGeometry(const Value& geom, const Value& props,
                   ObjectArrData& points,
                   ObjectArrData& lines,
                   ObjectArrData& polys) {
  if (!geom.IsObject()) return;
  std::string type = ToLower(geom.Get("type").AsString());
  Value coords = geom.Get("coordinates");
  if (type == "point" || type == "multipoint") {
    AddFeature(points, coords, props);
  } else if (type == "linestring" || type == "multilinestring") {
    AddFeature(lines, coords, props);
  } else if (type == "polygon" || type == "multipolygon") {
    AddFeature(polys, coords, props);
  } else if (type == "geometrycollection") {
    Value geoms = geom.Get("geometries");
    if (geoms.IsArray()) {
      for (size_t i = 0; i < geoms.Size(); ++i) {
        ParseGeometry(geoms.At(i), props, points, lines, polys);
      }
    }
  }
}

void ParseGeoJSON(const Value& node,
                  ObjectArrData& points,
                  ObjectArrData& lines,
                  ObjectArrData& polys) {
  if (node.IsArray()) {
    for (size_t i = 0; i < node.Size(); ++i) {
      ParseGeoJSON(node.At(i), points, lines, polys);
    }
    return;
  }
  if (!node.IsObject()) return;

  std::string type = ToLower(node.Get("type").AsString());
  if (type == "featurecollection") {
    Value features = node.Get("features");
    if (features.IsArray()) {
      for (size_t i = 0; i < features.Size(); ++i) {
        ParseGeoJSON(features.At(i), points, lines, polys);
      }
    }
    return;
  }
  if (type == "feature") {
    Value geom = node.Get("geometry");
    Value props = node.Get("properties");
    ParseGeometry(geom, props, points, lines, polys);
    return;
  }
  if (type == "geometrycollection") {
    Value geoms = node.Get("geometries");
    if (geoms.IsArray()) {
      for (size_t i = 0; i < geoms.Size(); ++i) {
        ParseGeometry(geoms.At(i), Value::Object(), points, lines, polys);
      }
    }
    return;
  }

  // Treat as raw geometry object.
  if (!type.empty()) {
    ParseGeometry(node, Value::Object(), points, lines, polys);
  }
}

Value Vec4ToArray(const glm::vec4& v) {
  Value arr = Value::Array();
  arr.Push(Value::Number(static_cast<double>(v.r)));
  arr.Push(Value::Number(static_cast<double>(v.g)));
  arr.Push(Value::Number(static_cast<double>(v.b)));
  arr.Push(Value::Number(static_cast<double>(v.a)));
  return arr;
}

static glm::vec4 StringToColor(const std::string& str, float opacity) {
  if (str.empty()) return glm::vec4(1.0f, 1.0f, 1.0f, opacity);
  if (str[0] == '#') {
    std::string hex = str.substr(1);
    try {
      if (hex.length() == 3) {
        float r = std::stoi(hex.substr(0, 1), nullptr, 16) / 15.0f;
        float g = std::stoi(hex.substr(1, 1), nullptr, 16) / 15.0f;
        float b = std::stoi(hex.substr(2, 1), nullptr, 16) / 15.0f;
        return glm::vec4(r, g, b, opacity);
      } else if (hex.length() == 6) {
        float r = std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f;
        float g = std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f;
        float b = std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f;
        return glm::vec4(r, g, b, opacity);
      }
    } catch (...) {
      return glm::vec4(1.0f, 1.0f, 1.0f, opacity);
    }
  }
  return glm::vec4(1.0f, 1.0f, 1.0f, opacity);
}

static void ParseStyle(const Value& val, LayerStyle& style) {
  if (val.type != Value::Type::Object) return;
  
  if (!val.Get("active").IsNull()) style.active = val.Get("active").AsBool(true);
  if (!val.Get("opacity").IsNull()) style.opacity = static_cast<float>(val.Get("opacity").AsNumber(1.0));
  
  if (!val.Get("fillColor").IsNull()) style.fillColor = StringToColor(val.Get("fillColor").AsString(), style.opacity);
  if (!val.Get("filled").IsNull()) style.filled = val.Get("filled").AsBool(true);
  
  if (!val.Get("borderColor").IsNull()) style.borderColor = StringToColor(val.Get("borderColor").AsString(), style.opacity);
  if (!val.Get("border").IsNull()) style.border = val.Get("border").AsBool(true);
  if (!val.Get("strokeWidth").IsNull()) style.strokeWidth = static_cast<float>(val.Get("strokeWidth").AsNumber(1.0));
  
  // Point/Icon
  if (!val.Get("pointSize").IsNull()) style.pointSize = static_cast<float>(val.Get("pointSize").AsNumber(8.0));
  if (!val.Get("pointColor").IsNull()) style.pointColor = StringToColor(val.Get("pointColor").AsString(), style.opacity);
  
  if (!val.Get("iconType").IsNull()) style.iconType = static_cast<IconType>(static_cast<int>(val.Get("iconType").AsNumber(0)));
  
  Value icon = val.Get("icon");
  if (!icon.IsNull()) {
    if (!icon.Get("mapName").IsNull()) style.icon.mapName = icon.Get("mapName").AsString();
    if (!icon.Get("name").IsNull()) style.icon.name = icon.Get("name").AsString();
    if (!icon.Get("borderColor").IsNull()) style.icon.borderColor = StringToColor(icon.Get("borderColor").AsString(), 1.0f);
    if (!icon.Get("fillColor").IsNull()) style.icon.fillColor = StringToColor(icon.Get("fillColor").AsString(), 1.0f);
    if (!icon.Get("borderWidth").IsNull()) style.icon.borderWidth = static_cast<float>(icon.Get("borderWidth").AsNumber(0.0));
    if (!icon.Get("radius").IsNull()) style.icon.radius = static_cast<float>(icon.Get("radius").AsNumber(16.0));
    if (!icon.Get("sizeX").IsNull()) style.icon.sizeX = static_cast<float>(icon.Get("sizeX").AsNumber(32.0));
    if (!icon.Get("sizeY").IsNull()) style.icon.sizeY = static_cast<float>(icon.Get("sizeY").AsNumber(32.0));
    if (!icon.Get("rotDeg").IsNull()) style.icon.rotDeg = static_cast<float>(icon.Get("rotDeg").AsNumber(0.0));
  }
  
  Value labels = val.Get("labels");
  if (labels.type == Value::Type::Array && labels.Size() > 0) {
    Value label = labels.At(0);
    if (!label.Get("show").IsNull()) style.showLabel = label.Get("show").AsBool(false);
    if (!label.Get("field").IsNull()) style.labelField = label.Get("field").AsString();
    if (!label.Get("text").IsNull()) style.labelText = label.Get("text").AsString();
    if (!label.Get("size").IsNull()) style.labelSize = static_cast<float>(label.Get("size").AsNumber(20.0));
    if (!label.Get("textColor").IsNull()) style.labelColor = StringToColor(label.Get("textColor").AsString(), 1.0f);
    if (!label.Get("hollowColor").IsNull()) style.labelHaloColor = StringToColor(label.Get("hollowColor").AsString(), 1.0f);
    if (!label.Get("hollowWidth").IsNull()) style.labelHaloWidth = static_cast<float>(label.Get("hollowWidth").AsNumber(2.0));
    // Flash
    if (!label.Get("flash").IsNull()) style.flashLabels = label.Get("flash").AsBool(false);
  }
  
  // Flash
  if (!val.Get("flashIcon").IsNull()) style.flashIcon = val.Get("flashIcon").AsBool(false);
  
  if (!val.Get("startLod").IsNull()) style.minZoom = static_cast<int>(val.Get("startLod").AsNumber(2.0));
  if (!val.Get("endLod").IsNull()) style.maxZoom = static_cast<int>(val.Get("endLod").AsNumber(25.0));
  if (!val.Get("depthTest").IsNull()) style.depthTest = val.Get("depthTest").AsBool(false);
  if (!val.Get("cullFace").IsNull()) style.cullFace = val.Get("cullFace").AsBool(true);
}

static std::string ColorToHex(const glm::vec4& v) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x",
           static_cast<int>(v.r * 255.0f + 0.5f),
           static_cast<int>(v.g * 255.0f + 0.5f),
           static_cast<int>(v.b * 255.0f + 0.5f));
  return std::string(buf);
}

Value StyleToValue(const LayerStyle& style) {
  Value obj = Value::Object();
  obj.Set("active", Value::Bool(style.active));
  obj.Set("opacity", Value::Number(static_cast<double>(style.opacity)));
  obj.Set("fillColor", Value::String(ColorToHex(style.fillColor)));
  obj.Set("filled", Value::Bool(style.filled));
  obj.Set("borderColor", Value::String(ColorToHex(style.borderColor)));
  obj.Set("border", Value::Bool(style.border));
  obj.Set("strokeWidth", Value::Number(static_cast<double>(style.strokeWidth)));
  obj.Set("pointSize", Value::Number(static_cast<double>(style.pointSize)));
  obj.Set("pointColor", Value::String(ColorToHex(style.pointColor)));
  
  obj.Set("iconType", Value::Number(static_cast<double>(style.iconType)));
  Value icon = Value::Object();
  icon.Set("mapName", Value::String(style.icon.mapName));
  icon.Set("name", Value::String(style.icon.name));
  icon.Set("borderColor", Value::String(ColorToHex(style.icon.borderColor)));
  icon.Set("fillColor", Value::String(ColorToHex(style.icon.fillColor)));
  icon.Set("borderWidth", Value::Number(static_cast<double>(style.icon.borderWidth)));
  icon.Set("radius", Value::Number(static_cast<double>(style.icon.radius)));
  icon.Set("sizeX", Value::Number(static_cast<double>(style.icon.sizeX)));
  icon.Set("sizeY", Value::Number(static_cast<double>(style.icon.sizeY)));
  icon.Set("rotDeg", Value::Number(static_cast<double>(style.icon.rotDeg)));
  obj.Set("icon", icon);

  Value labels = Value::Array();
  Value label = Value::Object();
  label.Set("show", Value::Bool(style.showLabel));
  label.Set("field", Value::String(style.labelField));
  label.Set("text", Value::String(style.labelText));
  label.Set("size", Value::Number(static_cast<double>(style.labelSize)));
  label.Set("textColor", Value::String(ColorToHex(style.labelColor)));
  label.Set("hollowColor", Value::String(ColorToHex(style.labelHaloColor)));
  label.Set("hollowWidth", Value::Number(static_cast<double>(style.labelHaloWidth)));
  // Flash
  label.Set("flash", Value::Bool(style.flashLabels));
  
  labels.Push(label);
  obj.Set("labels", labels);
  
  // Flash
  obj.Set("flashIcon", Value::Bool(style.flashIcon));

  obj.Set("startLod", Value::Number(static_cast<double>(style.minZoom)));
  obj.Set("endLod", Value::Number(static_cast<double>(style.maxZoom)));
  obj.Set("depthTest", Value::Bool(style.depthTest));
  obj.Set("cullFace", Value::Bool(style.cullFace));
  
  return obj;
}

Value RasterConfigToValue(const RasterLayerConfig& cfg) {
  Value obj = Value::Object();
  obj.Set("id", Value::String(cfg.id));
  obj.Set("name", Value::String(cfg.name));
  obj.Set("url", Value::String(cfg.url));
  obj.Set("supportUrl", Value::String(cfg.supportUrl));
  Value supportObj = Value::Object();
  supportObj.Set("url", Value::String(cfg.supportUrl));
  supportObj.Set("transparentPixelSupport", Value::Bool(cfg.supportTransparentPixel));
  supportObj.Set("emptyContentSupport", Value::Bool(cfg.supportEmptyContent));
  supportObj.Set("outOfBBOXSupport", Value::Bool(cfg.supportOutOfBBOX));
  obj.Set("supportURL", supportObj);
  obj.Set("visible", Value::Bool(cfg.visible));
  obj.Set("opacity", Value::Number(static_cast<double>(cfg.opacity)));
  obj.Set("minZoom", Value::Number(static_cast<double>(cfg.minZoom)));
  obj.Set("maxZoom", Value::Number(static_cast<double>(cfg.maxZoom)));
  obj.Set("zIndex", Value::Number(static_cast<double>(cfg.zIndex)));
  const char* typeStr = "XYZ";
  switch (cfg.type) {
    case RasterLayerType::XYZ: typeStr = "XYZ"; break;
    case RasterLayerType::TMS: typeStr = "TMS"; break;
    case RasterLayerType::WMS: typeStr = "WMS"; break;
  }
  obj.Set("type", Value::String(typeStr));
  return obj;
}

Value FeatureToValue(const Feature& feature) {
  Value item = Value::Object();
  item.Set("id", Value::String(feature.id));
  const char* geomType = "Unknown";
  switch (feature.geometryType) {
    case GeometryType::Point: geomType = "Point"; break;
    case GeometryType::Line: geomType = "Line"; break;
    case GeometryType::Polygon: geomType = "Polygon"; break;
    case GeometryType::MultiPoint: geomType = "MultiPoint"; break;
    case GeometryType::MultiLine: geomType = "MultiLine"; break;
    case GeometryType::MultiPolygon: geomType = "MultiPolygon"; break;
  }
  item.Set("geometryType", Value::String(geomType));
  Value coords = Value::Array();
  for (double coord : feature.coordinates) {
    coords.Push(Value::Number(coord));
  }
  item.Set("coords", coords);
  Value props = Value::Object();
  for (const auto& entry : feature.properties) {
    const PropertyValue& prop = entry.second;
    switch (prop.type) {
      case PropertyValue::Type::Number:
        props.Set(entry.first, Value::Number(prop.numberVal));
        break;
      case PropertyValue::Type::String:
        props.Set(entry.first, Value::String(prop.stringVal));
        break;
      case PropertyValue::Type::Boolean:
        props.Set(entry.first, Value::Bool(prop.boolVal));
        break;
      case PropertyValue::Type::Null:
      default:
        props.Set(entry.first, Value::Null());
        break;
    }
  }
  item.Set("properties", props);
  return item;
}

constexpr double kMercatorRadius = 20037508.34;
constexpr double kEarthRadius = 6378137.0;
constexpr double kUtmScale = 0.9996;
constexpr double kGlobeZAbart = 1.0;
constexpr size_t kMaxDrawCommands = 2048;

double LonDegToMercator(double lon) {
  return lon * kMercatorRadius / 180.0;
}

double LatDegToMercator(double lat) {
  return std::log(std::tan((90.0 + lat) * M_PI / 360.0)) * kMercatorRadius / M_PI;
}

double MercatorToLonDeg(double x) {
  return x * 180.0 / kMercatorRadius;
}

double MercatorToLatDeg(double y) {
  return 180.0 / M_PI * (2.0 * std::atan(std::exp(y * M_PI / kMercatorRadius)) - M_PI / 2.0);
}

double RadiusFromHeight(const GlobeEngine& engine, double lonDeg, double latDeg, double height, bool isMSL) {
  double terrainHeight = 0.0;
  if (!isMSL) {
    double sampled = 0.0;
    // Use a high LOD (14) for accurate point sampling
    if (engine.SampleTerrainHeightMeters(lonDeg, latDeg, 14, sampled)) {
      terrainHeight = sampled;
    }
  }
  return static_cast<double>(GLOBE_RADIUS) +
         (terrainHeight + height) * kGlobeZAbart * static_cast<double>(GLOBE_RADIUS_K);
}

void GeoToCartesian(double lonDeg, double latDeg, double radius, double& outX, double& outY, double& outZ) {
  double lonRad = lonDeg * M_PI / 180.0;
  double latRad = latDeg * M_PI / 180.0;
  double cosLat = std::cos(latRad);
  outX = radius * cosLat * std::cos(lonRad);
  outY = radius * cosLat * std::sin(lonRad);
  outZ = radius * std::sin(latRad);
}

void AppendCartesianPoint(const GlobeEngine& engine,
                          Value& arr,
                          double lonDeg,
                          double latDeg,
                          double height,
                          bool isMSL) {
  double radius = RadiusFromHeight(engine, lonDeg, latDeg, height, isMSL);
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  GeoToCartesian(lonDeg, latDeg, radius, x, y, z);
  arr.Push(Value::Number(x));
  arr.Push(Value::Number(y));
  arr.Push(Value::Number(z));
}

bool IsSphereGeometry(const std::string& geometry) {
  return ToLower(geometry) == "sphere";
}

struct DegMinSec {
  int degree = 0;
  int minute = 0;
  double second = 0.0;
};

DegMinSec GeoToDegMinSec(double value) {
  DegMinSec dms;
  double absVal = std::abs(value);
  dms.degree = static_cast<int>(absVal);
  double minFloat = (absVal - dms.degree) * 60.0;
  dms.minute = static_cast<int>(minFloat);
  dms.second = (minFloat - dms.minute) * 60.0;
  return dms;
}

double DegMinSecToFloat(int deg, int minute, double second) {
  double sign = deg < 0 ? -1.0 : 1.0;
  double absDeg = std::abs(static_cast<double>(deg));
  return sign * (absDeg + minute / 60.0 + second / 3600.0);
}

bool ParseDmsComponent(const Value& obj, bool isLon, double& out, std::string& error) {
  if (!obj.IsObject()) {
    error = "Missing DMS object";
    return false;
  }
  int deg = static_cast<int>(obj.Get("deg").AsNumber(-1));
  int min = static_cast<int>(obj.Get("min").AsNumber(-1));
  double sec = obj.Get("sec").AsNumber(0.0);
  if (deg < 0 || min < 0) {
    error = "Wrong deg Parameter. Degree Value Must be Greater Than 0";
    return false;
  }
  std::string dir = ToUpper(obj.Get("direction").AsString());
  if (dir.empty()) {
    error = "Missing Prefix(direction) Parameter";
    return false;
  }
  int sign = 0;
  if (isLon) {
    if (dir == "E" || dir == "D") sign = 1;
    else if (dir == "W" || dir == "B") sign = -1;
  } else {
    if (dir == "N" || dir == "K") sign = 1;
    else if (dir == "S" || dir == "G") sign = -1;
  }
  if (sign == 0) {
    error = "Wrong Prefix(direction)";
    return false;
  }
  double val = DegMinSecToFloat(deg * sign, min, sec);
  out = val;
  return true;
}

struct UtmCoord {
  double x = 0.0;
  double y = 0.0;
  int zone = 0;
};

int GetUtmZoneFromLonLat(double lon, double lat) {
  int zone = static_cast<int>(std::floor((lon + 180.0) / 6.0)) + 1;
  if (zone < 1) zone = 1;
  if (zone > 60) zone = 60;
  if (lat < 0.0) zone = -zone;
  return zone;
}

UtmCoord GeoToUtm(double lon, double lat) {
  UtmCoord out;
  int zone = GetUtmZoneFromLonLat(lon, lat);
  int zoneAbs = std::abs(zone);

  double lonRad = lon * M_PI / 180.0;
  double latRad = lat * M_PI / 180.0;
  double lonOrigin = (zoneAbs - 1) * 6 - 180 + 3;
  double lonOriginRad = lonOrigin * M_PI / 180.0;

  double f = 1.0 / 298.257223563;
  double e2 = f * (2.0 - f);
  double ep2 = e2 / (1.0 - e2);

  double N = kEarthRadius / std::sqrt(1.0 - e2 * std::sin(latRad) * std::sin(latRad));
  double T = std::tan(latRad) * std::tan(latRad);
  double C = ep2 * std::cos(latRad) * std::cos(latRad);
  double A = std::cos(latRad) * (lonRad - lonOriginRad);

  double M = kEarthRadius *
             ((1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256) * latRad
              - (3 * e2 / 8 + 3 * e2 * e2 / 32 + 45 * e2 * e2 * e2 / 1024) * std::sin(2 * latRad)
              + (15 * e2 * e2 / 256 + 45 * e2 * e2 * e2 / 1024) * std::sin(4 * latRad)
              - (35 * e2 * e2 * e2 / 3072) * std::sin(6 * latRad));

  double easting = kUtmScale * N *
                   (A + (1 - T + C) * std::pow(A, 3) / 6 +
                    (5 - 18 * T + T * T + 72 * C - 58 * ep2) * std::pow(A, 5) / 120) + 500000.0;
  double northing = kUtmScale *
                    (M + N * std::tan(latRad) *
                     (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * std::pow(A, 4) / 24 +
                      (61 - 58 * T + T * T + 600 * C - 330 * ep2) * std::pow(A, 6) / 720));
  if (lat < 0.0) {
    northing += 10000000.0;
  }

  out.x = easting;
  out.y = northing;
  out.zone = zone;
  return out;
}

bool UtmToGeo(int zone, double easting, double northing, double& outLon, double& outLat) {
  int zoneAbs = std::abs(zone);
  bool south = zone < 0;
  double f = 1.0 / 298.257223563;
  double e2 = f * (2.0 - f);
  double ep2 = e2 / (1.0 - e2);

  double x = easting - 500000.0;
  double y = northing;
  if (south) {
    y -= 10000000.0;
  }

  double M = y / kUtmScale;
  double mu = M / (kEarthRadius * (1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256));
  double e1 = (1 - std::sqrt(1 - e2)) / (1 + std::sqrt(1 - e2));

  double phi1 = mu + (3 * e1 / 2 - 27 * std::pow(e1, 3) / 32) * std::sin(2 * mu)
                + (21 * e1 * e1 / 16 - 55 * std::pow(e1, 4) / 32) * std::sin(4 * mu)
                + (151 * std::pow(e1, 3) / 96) * std::sin(6 * mu)
                + (1097 * std::pow(e1, 4) / 512) * std::sin(8 * mu);

  double N1 = kEarthRadius / std::sqrt(1 - e2 * std::sin(phi1) * std::sin(phi1));
  double T1 = std::tan(phi1) * std::tan(phi1);
  double C1 = ep2 * std::cos(phi1) * std::cos(phi1);
  double R1 = kEarthRadius * (1 - e2) / std::pow(1 - e2 * std::sin(phi1) * std::sin(phi1), 1.5);
  double D = x / (N1 * kUtmScale);

  double lat = phi1 - (N1 * std::tan(phi1) / R1) *
                         (D * D / 2 - (5 + 3 * T1 + 10 * C1 - 4 * C1 * C1 - 9 * ep2) *
                                        std::pow(D, 4) / 24 +
                          (61 + 90 * T1 + 298 * C1 + 45 * T1 * T1 - 252 * ep2 - 3 * C1 * C1) *
                              std::pow(D, 6) / 720);

  double lonOrigin = (zoneAbs - 1) * 6 - 180 + 3;
  double lon = (D - (1 + 2 * T1 + C1) * std::pow(D, 3) / 6 +
                (5 - 2 * C1 + 28 * T1 - 3 * C1 * C1 + 8 * ep2 + 24 * T1 * T1) *
                    std::pow(D, 5) / 120) / std::cos(phi1);
  lon = lonOrigin * M_PI / 180.0 + lon;

  outLat = lat * 180.0 / M_PI;
  outLon = lon * 180.0 / M_PI;
  return true;
}

int LetterIndex(const std::string& letters, char letter) {
  auto pos = letters.find(letter);
  if (pos == std::string::npos) return -1;
  return static_cast<int>(pos);
}

char LatitudeBand(double lat) {
  static const std::string kBands = "CDEFGHJKLMNPQRSTUVWX";
  if (lat > 84.0) lat = 84.0;
  if (lat < -80.0) lat = -80.0;
  int index = static_cast<int>(std::floor((lat + 80.0) / 8.0));
  if (index < 0) index = 0;
  if (index > static_cast<int>(kBands.size()) - 1) index = static_cast<int>(kBands.size()) - 1;
  return kBands[static_cast<size_t>(index)];
}

double MinNorthingForBand(char band) {
  switch (band) {
    case 'C': return 1100000.0;
    case 'D': return 2000000.0;
    case 'E': return 2800000.0;
    case 'F': return 3700000.0;
    case 'G': return 4600000.0;
    case 'H': return 5500000.0;
    case 'J': return 6400000.0;
    case 'K': return 7300000.0;
    case 'L': return 8200000.0;
    case 'M': return 9100000.0;
    case 'N': return 0.0;
    case 'P': return 800000.0;
    case 'Q': return 1700000.0;
    case 'R': return 2600000.0;
    case 'S': return 3500000.0;
    case 'T': return 4400000.0;
    case 'U': return 5300000.0;
    case 'V': return 6200000.0;
    case 'W': return 7000000.0;
    case 'X': return 7900000.0;
    default: return 0.0;
  }
}

std::string FormatPaddedInt(int value, int width) {
  std::string out = std::to_string(value);
  if (static_cast<int>(out.size()) < width) {
    out.insert(out.begin(), width - static_cast<int>(out.size()), '0');
  }
  return out;
}

std::string GeoToMgrs(double lon, double lat) {
  UtmCoord utm = GeoToUtm(lon, lat);
  int zone = std::abs(utm.zone);
  char band = LatitudeBand(lat);

  static const std::string kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
  static const std::string kRowLetters = "ABCDEFGHJKLMNPQRSTUV";
  static const char kColOrigin[] = "AJSAJS";
  static const char kRowOrigin[] = "AFAFAF";

  int set = zone % 6;
  if (set == 0) set = 6;
  int colOrigin = LetterIndex(kLetters, kColOrigin[set - 1]);
  int rowOrigin = LetterIndex(kRowLetters, kRowOrigin[set - 1]);
  if (colOrigin < 0 || rowOrigin < 0) return std::string();

  int col = static_cast<int>(utm.x / 100000.0);
  if (col < 1) col = 1;
  int row = static_cast<int>(utm.y / 100000.0) % 20;

  int colIndex = (colOrigin + col - 1) % 24;
  int rowIndex = (rowOrigin + row) % 20;

  char colLetter = kLetters[static_cast<size_t>(colIndex)];
  char rowLetter = kRowLetters[static_cast<size_t>(rowIndex)];

  int easting = static_cast<int>(std::round(utm.x)) % 100000;
  int northing = static_cast<int>(std::round(utm.y)) % 100000;

  return std::to_string(zone) + band + colLetter + rowLetter +
         FormatPaddedInt(easting, 5) + FormatPaddedInt(northing, 5);
}

bool MgrsToGeo(const std::string& mgrs, double& outLon, double& outLat) {
  std::string s;
  s.reserve(mgrs.size());
  for (char c : mgrs) {
    if (c == ' ') continue;
    s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  if (s.size() < 5) return false;

  size_t idx = 0;
  int zone = 0;
  while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
    zone = zone * 10 + (s[idx] - '0');
    ++idx;
  }
  if (zone < 1 || zone > 60 || idx >= s.size()) return false;
  char band = s[idx++];
  if (idx + 1 >= s.size()) return false;
  char colLetter = s[idx++];
  char rowLetter = s[idx++];
  std::string digits = s.substr(idx);
  if (digits.size() % 2 != 0) return false;
  int precision = static_cast<int>(digits.size() / 2);

  int easting = 0;
  int northing = 0;
  if (precision > 0) {
    easting = std::stoi(digits.substr(0, precision));
    northing = std::stoi(digits.substr(precision, precision));
  }
  int scale = 1;
  for (int i = 0; i < 5 - precision; ++i) {
    scale *= 10;
  }
  easting *= scale;
  northing *= scale;

  static const std::string kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
  static const std::string kRowLetters = "ABCDEFGHJKLMNPQRSTUV";
  static const char kColOrigin[] = "AJSAJS";
  static const char kRowOrigin[] = "AFAFAF";

  int set = zone % 6;
  if (set == 0) set = 6;
  int colOrigin = LetterIndex(kLetters, kColOrigin[set - 1]);
  int rowOrigin = LetterIndex(kRowLetters, kRowOrigin[set - 1]);
  int colIndex = LetterIndex(kLetters, colLetter);
  int rowIndex = LetterIndex(kRowLetters, rowLetter);
  if (colOrigin < 0 || rowOrigin < 0 || colIndex < 0 || rowIndex < 0) return false;

  int col = colIndex - colOrigin;
  if (col < 0) col += 24;
  int row = rowIndex - rowOrigin;
  if (row < 0) row += 20;

  double easting100k = (col + 1) * 100000.0;
  double northing100k = row * 100000.0;
  double finalEasting = easting100k + easting;
  double finalNorthing = northing100k + northing;

  double minNorthing = MinNorthingForBand(band);
  while (finalNorthing < minNorthing) {
    finalNorthing += 2000000.0;
  }

  int zoneSigned = (band < 'N') ? -zone : zone;
  return UtmToGeo(zoneSigned, finalEasting, finalNorthing, outLon, outLat);
}

} // namespace

#include "value.h"
#include "json_parser.h"

namespace {

bool ParseJsonString(const std::string& input, Value& out) {
  JsonParser parser(input);
  return parser.Parse(out);
}

}  // namespace

bool GlobeApi::Init(const GlobeConfig& config) {
  valid_ = engine_.Init(config);
  return valid_;
}

void GlobeApi::Run() {
  if (!valid_) return;
  engine_.Run();
}

bool GlobeApi::RunLodTest() {
  if (!valid_) return false;
  return engine_.RunLodTest();
}

bool GlobeApi::RunDemTest() {
  if (!valid_) return false;
  return engine_.RunDemTest();
}

bool GlobeApi::Run2DClampTest() {
  if (!valid_) return false;
  return engine_.Run2DClampTest();
}

bool GlobeApi::RunParityTest() {
  if (!valid_) return false;
  return engine_.RunParityTest();
}

void GlobeApi::Shutdown() {
  engine_.Shutdown();
  valid_ = false;
}

Value GlobeApi::api_GlobeIsValid() {
  return Value::Bool(valid_ && engine_.IsValid());
}

Value GlobeApi::api_GetCurrentLOD() {
  // P1: JS parity - return FDRAWED_MAX_LEVEL (max drawn tile LOD), not calculated zoom
  return Value::Number(static_cast<double>(engine_.GetMaxDrawnZoom()));
}

Value GlobeApi::api_GetCurrentLODWithDecimal() {
  return Value::Number(engine_.GetDrawnMaxLevelExact());
}

Value GlobeApi::api_SetNavigationDist(const Value& a0) {
  // P0: JS parity - use SetNavigationDist with Sa table
  double distMeters = a0.AsNumber();
  engine_.SetNavigationDist(distMeters);
  return Value::Null();
}

Value GlobeApi::api_SetNavigationLOD(const Value& a0) {
  // P0: JS parity - use SetNavigationLOD with Sa table
  int lod = static_cast<int>(a0.AsNumber());
  engine_.SetNavigationLOD(lod);
  return Value::Null();
}

Value GlobeApi::api_SetMinNavigationLOD(const Value& a0) {
  // P0: JS parity - use SetMinNavigationLOD with Sa table
  engine_.SetMinNavigationLOD(static_cast<int>(a0.AsNumber()));
  return Value::Null();
}

Value GlobeApi::api_SetMaxNavigationLOD(const Value& a0) {
  // P0: JS parity - use SetMaxNavigationLOD with Sa table
  engine_.SetMaxNavigationLOD(static_cast<int>(a0.AsNumber()));
  return Value::Null();
}

Value GlobeApi::api_SetScreenWidth(const Value& a0, const Value& a1) {
  double widthMeters = a0.AsNumber();
  bool lock = a1.AsBool(false);
  engine_.SetScreenWidthMeters(widthMeters, lock);
  return Value::Null();
}

Value GlobeApi::api_CancelScreenWidthAndMinMaxLOD() {
  // P0: JS parity - reset nav limits and stop movement
  engine_.ResetNavigationLimits();
  engine_.CancelAnimation();
  return Value::Null();
}

Value GlobeApi::api_SetMeshCacheSize(const Value& a0) {
  double value = a0.AsNumber();
  if (value < 1.0) value = 1.0;
  engine_.SetMeshCacheSize(static_cast<size_t>(value));
  return Value::Null();
}

Value GlobeApi::api_ReTryAtMeshTimeout(const Value& a0, const Value& a1, const Value& a2) {
  (void)a2;
  bool retry = a0.AsBool(true);
  bool continueDiv = a1.AsBool(false);
  engine_.SetMeshRetryOptions(retry, continueDiv);
  return Value::Null();
}

Value GlobeApi::api_SetTiltAngle(const Value& a0) {
  engine_.SetPitch(static_cast<float>(a0.AsNumber()));
  return Value::Null();
}

Value GlobeApi::api_SetNorthAngle(const Value& a0) {
  engine_.SetNorthAngle(a0.AsNumber());
  return Value::Null();
}

Value GlobeApi::api_Set2DMode(const Value& a0) {
  engine_.Set2DMode(a0.AsBool());
  return Value::Null();
}

Value GlobeApi::api_ZoomToLOD(const Value& a0) {
  // P1: JS parity - api_ZoomToLOD with GLOBE_MIN_CELL_LEVEL=19 limit
  // JS: webglobe_beautified.js lines 21465-21472
  static int prevZoomLod = -1;
  
  // P2: Base step on DRAWN LOD (FDRAWED_MAX_LEVEL) not camera altitude zoom
  int currentLod = engine_.GetMaxDrawnZoom();
  int targetLod = currentLod;
  
  if (a0.IsString()) {
    std::string cmd = a0.AsString();
    if (cmd == "zoomin") {
      targetLod = currentLod + 1;
      // JS: skip same LOD as previous (sticky zoom prevention)
      if (prevZoomLod == targetLod) targetLod++;
      // JS: clamp to GLOBE_MIN_CELL_LEVEL (19), not 22
      if (targetLod > GLOBE_MIN_CELL_LEVEL) targetLod = GLOBE_MIN_CELL_LEVEL;
    } else if (cmd == "zoomout") {
      targetLod = currentLod - 1;
      // JS: skip same LOD as previous
      if (prevZoomLod == targetLod) targetLod--;
      // JS: clamp to GLOBE_DEFAULT_MIN_LOD (2)
      if (targetLod < GLOBE_DEFAULT_MIN_LOD) targetLod = GLOBE_DEFAULT_MIN_LOD;
    }
  } else if (a0.IsNumber()) {
    int lod = static_cast<int>(a0.AsNumber());
    // JS: clamp to 2-19 range for navigation
    if (lod > GLOBE_DEFAULT_MIN_LOD && lod < GLOBE_MIN_CELL_LEVEL) {
      targetLod = lod;
    } else if (lod <= GLOBE_DEFAULT_MIN_LOD) {
      targetLod = GLOBE_DEFAULT_MIN_LOD;
    } else {
      targetLod = GLOBE_MIN_CELL_LEVEL;
    }
  }
  
  prevZoomLod = targetLod;
  engine_.ZoomToLOD(targetLod);
  return Value::Null();
}

Value GlobeApi::api_ZoomToPaperScale(const Value& a0) {
  double scale = a0.AsNumber();
  if (scale <= 0.0) return Value::Null();

  // Resolution at LOD L: Res = (C / 256) / 2^L  (meters/pixel)
  // At 96 DPI, 1 pixel = 0.000264583 meters
  const double C = 40075016.686;
  double res = 0.000264583 * scale;
  double lod = std::log2((C / 256.0) / res);

  engine_.ZoomToAltitude(engine_.AltitudeMetersFromLod(lod));
  return Value::Null();
}

Value GlobeApi::api_FlyToPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  double distMeters = a2.AsNumber();
  double north = a3.AsNumber(0.0);
  double tilt = a4.AsNumber(0.0);
  engine_.FlyToPoint(lat, lon, distMeters, north, tilt, 1.0);
  return Value::Null();
}

Value GlobeApi::api_FlyToPointDirect(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  double distMeters = a2.AsNumber();
  double north = a3.AsNumber(0.0);
  double tilt = a4.AsNumber(0.0);
  engine_.SetDirectPos(lon, lat, distMeters, north, tilt);
  return Value::Null();
}

Value GlobeApi::api_FlyToRegion(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  double minLon = a0.AsNumber();
  double minLat = a1.AsNumber();
  double maxLon = a2.AsNumber();
  double maxLat = a3.AsNumber();
  double duration = a4.AsNumber(1.0);
  engine_.FlyToRegion(minLat, minLon, maxLat, maxLon, duration);
  return Value::Null();
}

Value GlobeApi::api_FlyToRegionDirect(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  (void)a4;
  double minLon = a0.AsNumber();
  double minLat = a1.AsNumber();
  double maxLon = a2.AsNumber();
  double maxLat = a3.AsNumber();
  double centerLat = (minLat + maxLat) / 2.0;
  double centerLon = (minLon + maxLon) / 2.0;
  engine_.SetCenterLatLon(centerLat, centerLon);
  return Value::Null();
}

// Getter APIs
Value GlobeApi::api_GetCameraDist() {
  return Value::Number(engine_.GetCameraDistMeters());
}

Value GlobeApi::api_ScrW() {
  return Value::Number(static_cast<double>(engine_.GetScreenWidth()));
}

Value GlobeApi::api_ScrH() {
  return Value::Number(static_cast<double>(engine_.GetScreenHeight()));
}

Value GlobeApi::api_FPS() {
  return Value::Number(engine_.GetFPS());
}

Value GlobeApi::api_Altitude() {
  return Value::Number(engine_.GetAltitude());
}

Value GlobeApi::api_CamZ() {
  return Value::Number(engine_.GetCamZ());
}

Value GlobeApi::api_OrbitDistance() {
  return Value::Number(engine_.GetDistance());
}

Value GlobeApi::api_NorthAngleDeg() {
  double angle = engine_.GetNorthAngle();
  angle = std::fmod(angle, 360.0);
  if (angle < 0.0) angle += 360.0;
  return Value::Number(angle);
}

Value GlobeApi::api_GetScreenCenterAsDegree() {
  double lat = engine_.GetCenterLat();
  double lon = engine_.GetCenterLon();
  Value result = Value::Object();
  result.Set("lat", Value::Number(lat));
  result.Set("lon", Value::Number(lon));
  result.Set("long", Value::Number(lon));
  result.Set("lng", Value::Number(lon));
  result.Set("lng", Value::Number(lon));
  return result;
}

Value GlobeApi::api_GetCurrentLookInfo() {
  Value result = Value::Object();
  double lat = engine_.GetCenterLat();
  result.Set("lat", Value::Number(lat));
  double lon = engine_.GetCenterLon();
  result.Set("lon", Value::Number(lon));
  result.Set("long", Value::Number(lon));
  result.Set("alt", Value::Number(engine_.GetAltitude()));
  result.Set("pitch", Value::Number(static_cast<double>(engine_.GetPitch())));
  result.Set("yaw", Value::Number(static_cast<double>(engine_.GetYaw())));
  result.Set("roll", Value::Number(0.0));
  result.Set("x", Value::Number(lon));
  result.Set("y", Value::Number(lat));
  result.Set("z", Value::Number(engine_.GetAltitude()));
  // JS-style keys for parity
  result.Set("CenterLong", Value::Number(lon));
  result.Set("CenterLat", Value::Number(lat));
  double dist = engine_.GetCameraDistMeters();
  result.Set("Distance", Value::Number(dist));
  result.Set("Tilt", Value::Number(static_cast<double>(engine_.GetPitch())));
  double north = engine_.GetNorthAngle();
  if (north > 180.0) {
    double mod360 = std::fmod(north, 360.0);
    if (std::abs(mod360) < 1e-12) {
      north = 0.0;
    } else {
      double mod180 = std::fmod(north, 180.0);
      north = mod180 - 180.0;
    }
  } else if (north < -180.0) {
    double mod360 = std::fmod(north, 360.0);
    if (std::abs(mod360) < 1e-12) {
      north = 0.0;
    } else {
      double mod180 = std::fmod(north, 180.0);
      north = 180.0 + mod180;
    }
  }
  result.Set("NorthAng", Value::Number(north));
  result.Set("OrbitLong", Value::Number(lon));
  result.Set("OrbitLat", Value::Number(lat));
  result.Set("OrbitDist", Value::Number(dist));
  return result;
}

Value GlobeApi::api_IsScreenMoving() {
  return Value::Bool(engine_.IsScreenMoving());
}

Value GlobeApi::api_GetCurrentScale() {
  // Calculate approximate scale based on zoom level
  int zoom = engine_.GetCurrentZoom();
  double scale = 559082264.0 / (1 << zoom); // meters per pixel at equator
  return Value::Number(scale);
}

Value GlobeApi::api_GetCurrentMinLOD() {
  return Value::Number(static_cast<double>(engine_.GetMinZoom()));
}

Value GlobeApi::api_GetCurrentWorldLimit() {
  // Return world bounds - simplified
  return Value::Null();
}

Value GlobeApi::api_GetCurrentWorldWH() {
  // Return world width/height in current view
  return Value::Null();
}

Value GlobeApi::api_GetDirectPosNatural() {
  Value out = Value::Object();
  glm::dvec3 ea = engine_.GetEulerAngles();
  
  Value eaObj = Value::Object();
  eaObj.Set("x", Value::Number(ea.x));
  eaObj.Set("y", Value::Number(ea.y));
  eaObj.Set("z", Value::Number(ea.z));
  
  out.Set("ea", eaObj);
  out.Set("dist", Value::Number(engine_.GetCameraDistMeters() * GLOBE_RADIUS_K));
  out.Set("tilt", Value::Number(engine_.GetTiltAngle()));
  
  return out;
}

Value GlobeApi::api_GlobeFree() {
  engine_.Shutdown();
  return Value::Null();
}

// Camera APIs
Value GlobeApi::api_SetCameraPos(const Value& a0, const Value& a1, const Value& a2, 
                                  const Value& a3, const Value& a4, const Value& a5) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  double distMeters = a2.AsNumber();
  double north = a3.AsNumber();
  double tilt = a4.AsNumber();
  (void)a5; // roll - not implemented
  engine_.SetDirectPos(lon, lat, distMeters, north, tilt);
  return Value::Null();
}

Value GlobeApi::api_SetDirectPosNatural(const Value& a0) {
  if (!a0.IsObject()) {
    return Value::Null();
  }

  constexpr double kRadToDeg = 180.0 / M_PI;
  double lon = engine_.GetCenterLon();
  double lat = engine_.GetCenterLat();
  double distMeters = engine_.GetCameraDistMeters();
  double north = engine_.GetNorthAngle();
  double tilt = static_cast<double>(engine_.GetPitch());

  Value ea = a0.Get("ea");
  if (ea.IsObject()) {
    double eaX = ea.Get("x").AsNumber();
    double eaY = ea.Get("y").AsNumber();
    double eaZ = ea.Get("z").AsNumber();
    north = eaX * kRadToDeg;
    lat = -eaY * kRadToDeg;
    lon = eaZ * kRadToDeg;
    // JS parity: dist is normalized (FDist)
    double distNorm = a0.Get("dist").AsNumber(distMeters * GLOBE_RADIUS_K);
    distMeters = distNorm / GLOBE_RADIUS_K;
    tilt = a0.Get("tilt").AsNumber(tilt);
  } else {
    lon = a0.Get("longitude").AsNumber(lon);
    lat = a0.Get("latitude").AsNumber(lat);
    distMeters = a0.Get("distance").AsNumber(distMeters);
    north = a0.Get("northAngle").AsNumber(north);
    tilt = a0.Get("tiltAngle").AsNumber(tilt);
  }

  if (engine_.GetLockNorth()) {
    north = 0.0;
  }

  engine_.SetDirectPos(lon, lat, distMeters, north, tilt);
  return Value::Null();
}

Value GlobeApi::api_LeaveCamera(const Value& a0) {
  (void)a0;
  engine_.CancelAnimation();
  return Value::Null();
}

Value GlobeApi::api_SetLockNorth(const Value& a0) {
  bool lock = a0.AsBool();
  engine_.SetLockNorth(lock);
  return Value::Null();
}

Value GlobeApi::api_SetContinuousRotation(const Value& a0) {
  (void)a0;
  // Not implemented - would enable continuous globe rotation
  return Value::Null();
}

Value GlobeApi::api_SetMinNavigationDist(const Value& a0) {
  engine_.SetMinNavigationDist(a0.AsNumber());
  return Value::Null();
}

Value GlobeApi::api_SetMaxNavigationDist(const Value& a0) {
  engine_.SetMaxNavigationDist(a0.AsNumber());
  return Value::Null();
}

Value GlobeApi::api_GetNavigationSpeed() {
  return Value::Number(engine_.GetNavigationSpeed());
}

Value GlobeApi::api_SetNavigationSpeed(const Value& a0) {
  double speed = a0.AsNumber();
  engine_.SetNavigationSpeed(speed);
  return Value::Null();
}

Value GlobeApi::api_SetMouseWheelMode(const Value& a0) {
  bool zoomToCursor = a0.AsBool();
  engine_.SetMouseWheelMode(zoomToCursor);
  return Value::Null();
}

Value GlobeApi::api_SetMouseWheelDirection(const Value& a0) {
  bool reverse = a0.AsBool();
  engine_.SetMouseWheelDirection(reverse);
  return Value::Null();
}

Value GlobeApi::api_SetLang(const Value& a0) {
  engine_.SetLang(a0.AsString());
  return Value::Null();
}

Value GlobeApi::api_SetArrowKeysNavSpeed(const Value& a0) {
  double speed = a0.AsNumber();
  engine_.SetArrowKeysNavSpeed(speed);
  return Value::Null();
}

Value GlobeApi::api_GetFlashPeriod() {
  return Value::Number(static_cast<double>(engine_.GetFlashPeriod()));
}

Value GlobeApi::api_SetFlashPeriod(const Value& a0) {
  double val = a0.AsNumber(800.0);
  if (val < 50.0) val = 50.0;
  engine_.SetFlashPeriod(static_cast<int>(val));
  return Value::Null();
}

// Layer APIs
Value GlobeApi::api_AddLayer(const Value& a0, const Value& a1) {
  (void)a1; // style - will be used later
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  std::string newId = lm->AddLayer(id, id, LayerType::ObjectArray);
  return Value::String(newId);
}

Value GlobeApi::api_AddObjectBuffer(const Value& a0) {
  if (!a0.IsObject()) return Value::Null();
  std::string id = a0.Get("id").AsString();
  std::string name = a0.Get("name").AsString();
  engine_.GetLayerManager()->AddLayer(id, name, LayerType::ObjectArray);
  return Value::Null();
}

Value GlobeApi::api_CreateObjectBuffer(const Value& a0, const Value& a1) {
  std::string name = a0.AsString();
  std::string id = a1.AsString();
  if (id.empty()) id = std::to_string(nextObjectBufferId_++);
  
  Value obj = Value::Object();
  obj.Set("id", Value::String(id));
  obj.Set("name", Value::String(name));
  return obj;
}

Value GlobeApi::api_DeleteObjectBufferById(const Value& a0) {
  std::string id = a0.AsString();
  if (id.empty() && a0.IsNumber()) id = std::to_string(static_cast<int>(a0.AsNumber()));
  engine_.GetLayerManager()->DeleteLayer(id);
  return Value::Null();
}

Value GlobeApi::api_DeleteAllObjectBuffers() {
    std::vector<std::string> toDelete;
    size_t count = engine_.GetLayerManager()->GetLayerCount();
    for (size_t i = 0; i < count; ++i) {
        Layer* l = engine_.GetLayerManager()->GetLayerByIndex(i);
        if (l && l->type == LayerType::ObjectArray) {
            toDelete.push_back(l->id);
        }
    }
    for (const auto& id : toDelete) {
        engine_.GetLayerManager()->DeleteLayer(id);
    }
    return Value::Null();
}

Value GlobeApi::api_DeleteObjectBufferByIndex(const Value& a0) {
    int targetIndex = static_cast<int>(a0.AsNumber());
    int currentObjIndex = 0;
    std::string idToDelete;
    
    size_t count = engine_.GetLayerManager()->GetLayerCount();
    for (size_t i = 0; i < count; ++i) {
        Layer* l = engine_.GetLayerManager()->GetLayerByIndex(i);
        if (l && l->type == LayerType::ObjectArray) {
            if (currentObjIndex == targetIndex) {
                idToDelete = l->id;
                break;
            }
            currentObjIndex++;
        }
    }
    
    if (!idToDelete.empty()) {
        engine_.GetLayerManager()->DeleteLayer(idToDelete);
    }
    return Value::Null();
}

Value GlobeApi::api_FindObjectBufferById(const Value& a0) {
    std::string id = a0.AsString();
    if (id.empty() && a0.IsNumber()) id = std::to_string(static_cast<int>(a0.AsNumber()));
    Layer* l = engine_.GetLayerManager()->GetLayer(id);
    if (l) return Value::String(l->id);
    return Value::Null();
}

Value GlobeApi::api_GetObjectBuffer(const Value& a0) {
    return api_GetLayer(a0);
}

Value GlobeApi::api_ObjectBufferCount() {
    size_t count = 0;
    size_t total = engine_.GetLayerManager()->GetLayerCount();
    for (size_t i = 0; i < total; ++i) {
        Layer* l = engine_.GetLayerManager()->GetLayerByIndex(i);
        if (l && l->type == LayerType::ObjectArray) count++;
    }
    return Value::Number(static_cast<double>(count));
}

Value GlobeApi::api_DeleteLayer(const Value& a0) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  bool result = lm->DeleteLayer(id);
  return Value::Bool(result);
}

Value GlobeApi::api_DeleteLayers() {
  LayerManager* lm = engine_.GetLayerManager();
  lm->DeleteAllLayers();
  return Value::Null();
}

Value GlobeApi::api_GetLayer(const Value& a0) {
  int index = static_cast<int>(a0.AsNumber());
  LayerManager* lm = engine_.GetLayerManager();
  Layer* layer = lm->GetLayerByIndex(static_cast<size_t>(index));
  if (layer) {
    return Value::String(layer->id);
  }
  return Value::Null();
}

Value GlobeApi::api_GetLayerById(const Value& a0) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  Layer* layer = lm->GetLayer(id);
  if (layer) {
    return Value::String(layer->id);
  }
  return Value::Null();
}

Value GlobeApi::api_LayerCount() {
  LayerManager* lm = engine_.GetLayerManager();
  return Value::Number(static_cast<double>(lm->GetLayerCount()));
}

Value GlobeApi::api_GetNewLayerId() {
  return Value::String("layer_" + std::to_string(engine_.GetLayerManager()->GetLayerCount() + 1));
}

Value GlobeApi::api_GetNewObjectBufferId() {
  return Value::Number(nextObjectBufferId_++);
}

static Value LayerToValue(const Layer& layer) {
  Value obj = Value::Object();
  obj.Set("id", Value::String(layer.id));
  obj.Set("name", Value::String(layer.name));
  obj.Set("type", Value::Number(static_cast<double>(layer.type)));
  obj.Set("visible", Value::Bool(layer.visible));
  obj.Set("opacity", Value::Number(static_cast<double>(layer.opacity)));
  return obj;
}

Value GlobeApi::api_GetTotalLayersAsJSON() {
  Value out = Value::Object();
  
  Value rasters = Value::Array();
  for (const auto& id : engine_.GetRasterLayerIds()) {
    RasterLayerConfig cfg;
    if (engine_.GetRasterLayerConfigById(id, cfg)) {
      rasters.Push(RasterConfigToValue(cfg));
    }
  }
  out.Set("rasters", rasters);
  
  Value vectors = Value::Array();
  LayerManager* lm = engine_.GetLayerManager();
  for (size_t i = 0; i < lm->GetLayerCount(); ++i) {
    Layer* l = lm->GetLayerByIndex(i);
    if (l) {
      vectors.Push(LayerToValue(*l));
    }
  }
  out.Set("vectors", vectors);
  
  return out;
}

Value GlobeApi::api_GetZClient(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  double height = 0.0;
  if (engine_.SampleTerrainHeightMeters(lon, lat, -1, height)) {
    return Value::Number(height);
  }
  return Value::Number(0.0);
}

Value GlobeApi::api_SetLayerOn(const Value& a0, const Value& a1) {
  std::string id = a0.AsString();
  bool visible = a1.AsBool(true);
  LayerManager* lm = engine_.GetLayerManager();
  lm->SetLayerVisible(id, visible);
  return Value::Null();
}

Value GlobeApi::api_GetLayerOn(const Value& a0) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  return Value::Bool(lm->GetLayerVisible(id));
}

Value GlobeApi::api_SetLayerOpacity(const Value& a0, const Value& a1) {
  std::string id = a0.AsString();
  float opacity = static_cast<float>(a1.AsNumber(1.0));
  LayerManager* lm = engine_.GetLayerManager();
  lm->SetLayerOpacity(id, opacity);
  return Value::Null();
}

Value GlobeApi::api_GetLayerStyle(const Value& a0) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  Layer* layer = lm->GetLayer(id);
  if (layer) {
    Value style = StyleToValue(layer->style);
    style.Set("visible", Value::Bool(layer->visible));
    style.Set("opacity", Value::Number(static_cast<double>(layer->opacity)));
    style.Set("id", Value::String(layer->id));
    style.Set("name", Value::String(layer->name));
    return style;
  }
  return Value::Null();
}

Value GlobeApi::api_SetLayerStyle(const Value& a0, const Value& a1) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  Layer* layer = lm->GetLayer(id);
  if (layer) {
    // Parse style
    ParseStyle(a1, layer->style);
    
    // Also handle visible/opacity if they are in the style object
    if (!a1.Get("visible").IsNull()) layer->visible = a1.Get("visible").AsBool(true);
    if (!a1.Get("opacity").IsNull()) layer->opacity = static_cast<float>(a1.Get("opacity").AsNumber(1.0));
    
    // Mark dirty
    lm->StyleChanged(id);
  }
  return Value::Null();
}

Value GlobeApi::api_LayerStyleChanged(const Value& a0) {
  std::string id = a0.AsString();
  LayerManager* lm = engine_.GetLayerManager();
  lm->StyleChanged(id);
  return Value::Null();
}

Value GlobeApi::api_QueryByScreen(const Value& a0, const Value& a1) {
  int screenX = static_cast<int>(a0.AsNumber());
  int screenY = static_cast<int>(a1.AsNumber());
  Value result = Value::Array();
  auto features = engine_.QueryFeaturesAtScreen(screenX, screenY, 5.0);
  for (const auto* feature : features) {
    if (!feature) {
      continue;
    }
    result.Push(FeatureToValue(*feature));
  }
  return result;
}

Value GlobeApi::api_QueryByBBox(const Value& a0, const Value& a1, const Value& a2) {
  (void)a1;
  (void)a2;
  double minLon = 0.0;
  double minLat = 0.0;
  double maxLon = 0.0;
  double maxLat = 0.0;
  bool parsed = false;

  if (a0.IsArray() && a0.Size() >= 4) {
    minLon = a0.At(0).AsNumber();
    minLat = a0.At(1).AsNumber();
    maxLon = a0.At(2).AsNumber();
    maxLat = a0.At(3).AsNumber();
    parsed = true;
  } else if (a0.IsObject()) {
    Value ll = a0.Get("ll");
    Value ur = a0.Get("ur");
    if (ll.IsObject() && ur.IsObject()) {
      minLon = ll.Get("x").AsNumber();
      minLat = ll.Get("y").AsNumber();
      maxLon = ur.Get("x").AsNumber();
      maxLat = ur.Get("y").AsNumber();
      parsed = true;
    }
  }

  if (!parsed) {
    return Value::Null();
  }

  LayerManager* lm = engine_.GetLayerManager();
  auto features = lm->QueryByBBox(minLon, minLat, maxLon, maxLat);
  Value result = Value::Array();
  for (const auto* feature : features) {
    if (!feature) continue;
    result.Push(FeatureToValue(*feature));
  }
  return result;
}

Value GlobeApi::api_GetGeoFromScreenPoint(const Value& a0, const Value& a1) {
  int screenX = static_cast<int>(a0.AsNumber());
  int screenY = static_cast<int>(a1.AsNumber());
  double lat, lon;
  if (engine_.ScreenToGeo(screenX, screenY, lat, lon)) {
    Value result = Value::Object();
    result.Set("lat", Value::Number(lat));
    result.Set("lon", Value::Number(lon));
    result.Set("long", Value::Number(lon));
    result.Set("lng", Value::Number(lon));
    return result;
  }
  return Value::Null();
}

Value GlobeApi::api_GetScreenPointFromGeo(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  int screenX, screenY;
  if (engine_.GeoToScreen(lat, lon, screenX, screenY)) {
    Value result = Value::Object();
    result.Set("x", Value::Number(static_cast<double>(screenX)));
    result.Set("y", Value::Number(static_cast<double>(screenY)));
    return result;
  }
  return Value::Null();
}

Value GlobeApi::api_CanPickPoint(const Value& a0, const Value& a1, const Value& a2, 
                                  const Value& a3, const Value& a4, const Value& a5) {
  (void)a2; (void)a3; (void)a4; (void)a5;
  int screenX = static_cast<int>(a0.AsNumber());
  int screenY = static_cast<int>(a1.AsNumber());
  auto features = engine_.QueryFeaturesAtScreen(screenX, screenY, 5.0);
  return Value::Bool(!features.empty());
}

Value GlobeApi::api_GetDefaultStyle() {
  LayerStyle style;
  return StyleToValue(style);
}

Value GlobeApi::api_GetDefaultCompositeLayerStyle() {
  Value obj = api_GetDefaultLayerStyle();
  obj.Set("MVTXYZName", Value::Null());
  obj.Set("objectType", Value::Null());
  return obj;
}

Value GlobeApi::api_GetDefaultLayerStyle() {
  LayerStyle style;
  Value obj = StyleToValue(style);
  obj.Set("visible", Value::Bool(true));
  return obj;
}

Value GlobeApi::api_GetDefaultClusterStyle(const Value& a0) {
  bool inner = a0.AsBool(false);
  if (inner) {
    Value obj = Value::Object();
    obj.Set("active", Value::Bool(true));
    obj.Set("iconType", Value::Number(2.0)); // CIRCLE
    obj.Set("opacity", Value::Number(1.0));
    obj.Set("zMode", Value::Number(0.0)); // Z_GROUND_PERVERTEX
    
    Value icon = Value::Object();
    icon.Set("borderColor", Value::String("#0000ff"));
    icon.Set("fillColor", Value::String("#ffffff"));
    icon.Set("borderWidth", Value::Number(2.0));
    icon.Set("radius", Value::Number(16.0));
    obj.Set("icon", icon);

    Value labels = Value::Array();
    Value label = Value::Object();
    label.Set("text", Value::String("Calc(NUMBER,objcount)"));
    label.Set("vAlignment", Value::Number(0.0)); // CENTER
    label.Set("hAlignment", Value::Number(0.0)); // CENTER
    labels.Push(label);
    obj.Set("labels", labels);

    return obj;
  } else {
    Value obj = Value::Object();
    Value rule = Value::Array();
    rule.Push(Value::Bool(true));
    obj.Set("rule", rule);

    Value cluster = Value::Object();
    cluster.Set("maxLod", Value::Number(19.0));
    cluster.Set("radius", Value::Number(32.0));
    cluster.Set("style", api_GetDefaultClusterStyle(Value::Bool(true)));

    obj.Set("cluster", cluster);
    return obj;
  }
}

Value GlobeApi::api_GeoJSONToObjectArrData(const Value& a0) {
  Value root = a0;
  if (a0.IsString()) {
    Value parsed;
    if (!ParseJsonString(a0.AsString(), parsed)) {
      return Value::Null();
    }
    root = parsed;
  }
  ObjectArrData points;
  ObjectArrData lines;
  ObjectArrData polys;
  ParseGeoJSON(root, points, lines, polys);
  Value result = Value::Object();
  if (points.Empty()) {
    result.Set("pointData", Value::Null());
  } else {
    result.Set("pointData", points.ToValueArray());
  }
  if (lines.Empty()) {
    result.Set("lineData", Value::Null());
  } else {
    result.Set("lineData", lines.ToValueArray());
  }
  if (polys.Empty()) {
    result.Set("polygonData", Value::Null());
  } else {
    result.Set("polygonData", polys.ToValueArray());
  }
  return result;
}

Value GlobeApi::api_ObjectCreator(const Value& a0, const Value& a1, const Value& a2) {
  std::string type;
  if (a0.IsString()) {
    type = ToUpper(a0.AsString());
  } else if (a0.IsNumber()) {
    int typeId = static_cast<int>(a0.AsNumber());
    switch (typeId) {
      case 0: type = "POINT"; break;
      case 1: type = "LINE"; break;
      case 2: type = "POLYGON"; break;
      case 3: type = "SHAPE"; break;
      case 4: type = "ARCAREA"; break;
      default: type = "POINT"; break;
    }
  } else {
    type = "POINT";
  }

  bool isNew = a1.AsBool(false);
  bool emptyCoords = a2.AsBool(false);

  double centerLon = engine_.GetCenterLon();
  double centerLat = engine_.GetCenterLat();

  Value obj = Value::Object();
  obj.Set("startLod", Value::Number(static_cast<double>(engine_.GetMinZoom())));
  obj.Set("endLod", Value::Number(static_cast<double>(engine_.GetMaxZoom())));
  if (isNew) {
    obj.Set("isNew", Value::Bool(true));
  }

  Value coords = Value::Array();
  Value coordsZ = Value::Array();
  auto pushPoint = [&](double lon, double lat, double z = 0.0) {
    coords.Push(Value::Number(lon));
    coords.Push(Value::Number(lat));
    coordsZ.Push(Value::Number(z));
  };

  if (type == "POINT") {
    if (!emptyCoords) {
      pushPoint(centerLon, centerLat);
    }
  } else if (type == "LINE") {
    if (!isNew && !emptyCoords) {
      pushPoint(centerLon - 2.0, centerLat - 2.0);
      pushPoint(centerLon + 2.0, centerLat + 2.0);
    }
  } else if (type == "POLYGON") {
    if (!isNew && !emptyCoords) {
      pushPoint(centerLon - 2.0, centerLat - 2.0);
      pushPoint(centerLon, centerLat + 2.0);
      pushPoint(centerLon + 2.0, centerLat);
    }
    obj.Set("heights", Value::Array());
    obj.Set("solid3D", Value::Bool(false));
    obj.Set("fixedHeights", Value::Bool(false));
  } else if (type == "SHAPE") {
    if (!isNew && !emptyCoords) {
      pushPoint(centerLon, centerLat);
    }
    obj.Set("heights", Value::Array());
    obj.Set("shapeType", Value::String("ELLIPSE"));
    obj.Set("radius1", Value::Number(20000.0));
    obj.Set("radius2", Value::Number(20000.0));
    obj.Set("rotDeg", Value::Number(0.0));
    obj.Set("fixedTop", Value::Bool(false));
    obj.Set("proportional", Value::Bool(false));
    obj.Set("isEditCenter", Value::Bool(false));
  } else if (type == "ARCAREA") {
    if (!isNew && !emptyCoords) {
      pushPoint(centerLon, centerLat);
    }
    obj.Set("heights", Value::Array());
    obj.Set("radius1", Value::Number(40000.0));
    obj.Set("radius2", Value::Number(20000.0));
    obj.Set("startAng", Value::Number(0.0));
    obj.Set("endAng", Value::Number(90.0));
    obj.Set("stepAng", Value::Number(5.0));
    obj.Set("solid3D", Value::Bool(false));
    obj.Set("fixedTop", Value::Bool(false));
    obj.Set("isEditCenter", Value::Bool(false));
  }

  obj.Set("coords", coords);
  obj.Set("coordsZ", coordsZ);
  obj.Set("attribs", Value::Array());
  return obj;
}

Value GlobeApi::api_SetMouseEvents(const Value& a0, const Value& a1) {
  std::string events = a0.AsString();
  if (events.empty()) {
    return Value::Null();
  }
  size_t start = 0;
  while (start < events.size()) {
    size_t end = events.find(' ', start);
    if (end == std::string::npos) end = events.size();
    if (end > start) {
      std::string key = events.substr(start, end - start);
      key = ToLower(key);
      mouseEvents_[key] = a1;
    }
    start = end + 1;
  }
  return Value::Null();
}

Value GlobeApi::api_GetMouseEvent(const Value& a0) {
  std::string key = ToLower(a0.AsString());
  auto it = mouseEvents_.find(key);
  if (it == mouseEvents_.end()) {
    return Value::Null();
  }
  return it->second;
}

Value GlobeApi::api_ClearMouseEvents() {
  mouseEvents_.clear();
  return Value::Null();
}

Value GlobeApi::api_SetZoomWheelInDist(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  (void)a2;
  if (a3.IsNumber() && a4.IsNumber()) {
    int screenX = static_cast<int>(std::lround(a3.AsNumber()));
    int screenY = static_cast<int>(std::lround(a4.AsNumber()));
    engine_.UpdateCursorPosFromScreen(screenX, screenY);
  }
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  engine_.StartWheelZoomInDist(lon, lat);
  return Value::Null();
}

Value GlobeApi::api_SetZoomWheelOutDist(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4) {
  (void)a2;
  if (a3.IsNumber() && a4.IsNumber()) {
    int screenX = static_cast<int>(std::lround(a3.AsNumber()));
    int screenY = static_cast<int>(std::lround(a4.AsNumber()));
    engine_.UpdateCursorPosFromScreen(screenX, screenY);
  }
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  engine_.StartWheelZoomOutDist(lon, lat);
  return Value::Null();
}

Value GlobeApi::api_SetCameraCallBack(const Value& a0) {
  callbacks_["camera"] = a0;
  callbacks_["cameracallback"] = a0;
  callbacks_["camera_callback"] = a0;
  return Value::Null();
}

Value GlobeApi::api_SetStatusBarCallBack(const Value& a0) {
  callbacks_["statusbar"] = a0;
  callbacks_["statusbarcallback"] = a0;
  callbacks_["status_bar"] = a0;
  return Value::Null();
}

Value GlobeApi::api_SetUndoBuffersChangedEvent(const Value& a0) {
  callbacks_["undo_buffers_changed"] = a0;
  callbacks_["undobufferschangedevent"] = a0;
  callbacks_["undobufferschanged"] = a0;
  return Value::Null();
}

Value GlobeApi::api_EditCallbackChanged() {
  return Value::Null();
}

Value GlobeApi::api_EditCallbackCreator() {
  Value obj = Value::Object();
  obj.Set("can_rotate", Value::Bool(true));
  obj.Set("can_move", Value::Bool(true));
  obj.Set("can_moveVertex", Value::Bool(true));
  obj.Set("can_scale", Value::Bool(true));
  obj.Set("can_scaleX", Value::Bool(true));
  obj.Set("can_scaleY", Value::Bool(true));
  obj.Set("show_editwidget", Value::Bool(true));
  obj.Set("show_walker", Value::Bool(true));
  obj.Set("point_square", Value::Bool(false));
  obj.Set("draw_shadow", Value::Bool(true));

  Value canChange = Value::Object();
  canChange.Set("vertex", Value::Bool(true));
  canChange.Set("height", Value::Bool(true));
  canChange.Set("Z", Value::Bool(true));
  obj.Set("can_change", canChange);

  obj.Set("next_height", Value::Number(0.0));

  Value vertexColor = Value::Object();
  vertexColor.Set("mainH", Value::String("#12CFE3"));
  vertexColor.Set("mainN", Value::String("#000000"));
  vertexColor.Set("sub", Value::String("#FF0000"));
  vertexColor.Set("Z", Value::String("#E789DE"));
  vertexColor.Set("height", Value::String("#ffffff"));
  obj.Set("vertex_color", vertexColor);

  Value vertexSize = Value::Object();
  vertexSize.Set("main", Value::Number(7.0));
  vertexSize.Set("sub", Value::Number(6.0));
  vertexSize.Set("Z", Value::Number(7.0));
  vertexSize.Set("height", Value::Number(7.0));
  obj.Set("vertex_size", vertexSize);

  obj.Set("iconsURL", Value::Null());
  obj.Set("onHeightClick", Value::Null());
  obj.Set("onVertexClick", Value::Null());
  obj.Set("onFinish", Value::Null());
  obj.Set("onChanged", Value::Null());
  obj.Set("onMoveChanged", Value::Null());
  obj.Set("onEndEdit", Value::Null());

  callbacks_["editcallback"] = obj;
  callbacks_["edit_callback"] = obj;
  return obj;
}

Value GlobeApi::api_GetEditCallback() {
  auto it = callbacks_.find("editcallback");
  if (it == callbacks_.end()) {
    it = callbacks_.find("edit_callback");
  }
  if (it == callbacks_.end()) {
    return Value::Null();
  }
  return it->second;
}

Value GlobeApi::api_DispatchEvent(const Value& a0, const Value& a1) {
  std::string name = ToLower(a0.AsString());
  if (name.empty()) {
    return Value::Bool(false);
  }
  Value evt = Value::Object();
  evt.Set("name", Value::String(name));
  evt.Set("payload", a1);
  callbacks_["event:" + name] = evt;
  return Value::Bool(true);
}

Value GlobeApi::api_TriggerCallback(const Value& a0, const Value& a1) {
  std::string name = ToLower(a0.AsString());
  if (name.empty()) {
    return Value::Null();
  }
  auto it = callbacks_.find(name);
  if (it == callbacks_.end()) {
    it = callbacks_.find("event:" + name);
  }
  if (it == callbacks_.end()) {
    return Value::Null();
  }
  Value out = Value::Object();
  out.Set("name", Value::String(name));
  out.Set("callback", it->second);
  out.Set("payload", a1);
  return out;
}

Value GlobeApi::api_GetMercatorPoint(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  Value obj = Value::Object();
  obj.Set("long", Value::Number(LonDegToMercator(lon)));
  obj.Set("lat", Value::Number(LatDegToMercator(lat)));
  return obj;
}

Value GlobeApi::api_GetMercator2DPoint(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  Value arr = Value::Array();
  arr.Push(Value::Number(LonDegToMercator(lon)));
  arr.Push(Value::Number(LatDegToMercator(lat)));
  return arr;
}

Value GlobeApi::api_GetMercator3DPoint(const Value& a0, const Value& a1, const Value& a2) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  double z = a2.AsNumber();
  Value arr = Value::Array();
  arr.Push(Value::Number(LonDegToMercator(lon)));
  arr.Push(Value::Number(LatDegToMercator(lat)));
  arr.Push(Value::Number(z));
  return arr;
}

Value GlobeApi::api_GetMercator3DPoints(const Value& a0) {
  if (!a0.IsArray()) {
    return Value::Array();
  }
  Value out = Value::Array();
  for (size_t i = 0; i < a0.Size(); ++i) {
    Value item = a0.At(i);
    if (!item.IsObject()) continue;
    double lon = item.Get("long").IsNumber() ? item.Get("long").AsNumber() :
                 (item.Get("lon").IsNumber() ? item.Get("lon").AsNumber() : item.Get("lng").AsNumber());
    double lat = item.Get("lat").AsNumber();
    double z = item.Get("z").AsNumber();
    out.Push(Value::Number(LonDegToMercator(lon)));
    out.Push(Value::Number(LatDegToMercator(lat)));
    out.Push(Value::Number(z));
  }
  return out;
}

Value GlobeApi::api_GetMercator3DPointsByGeoArr(const Value& a0, const Value& a1) {
  if (!a0.IsArray() || !a1.IsArray()) {
    return Value::Array();
  }
  Value out = Value::Array();
  size_t count = a1.Size();
  for (size_t i = 0; i < count; ++i) {
    size_t idx = i * 2;
    if (idx + 1 >= a0.Size()) break;
    double lon = a0.At(idx).AsNumber();
    double lat = a0.At(idx + 1).AsNumber();
    double z = a1.At(i).AsNumber();
    out.Push(Value::Number(LonDegToMercator(lon)));
    out.Push(Value::Number(LatDegToMercator(lat)));
    out.Push(Value::Number(z));
  }
  return out;
}

Value GlobeApi::api_GetMercator3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1) {
  if (!a0.IsArray()) {
    return Value::Array();
  }
  double z = a1.AsNumber();
  Value out = Value::Array();
  for (size_t i = 0; i + 1 < a0.Size(); i += 2) {
    double lon = a0.At(i).AsNumber();
    double lat = a0.At(i + 1).AsNumber();
    out.Push(Value::Number(LonDegToMercator(lon)));
    out.Push(Value::Number(LatDegToMercator(lat)));
    out.Push(Value::Number(z));
  }
  return out;
}

Value GlobeApi::api_Get3DPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  if (!IsSphereGeometry(currentGeometry_)) {
    return api_GetMercator3DPoint(a0, a1, a2);
  }
  bool isMSL = a3.AsBool(true);
  Value arr = Value::Array();
  AppendCartesianPoint(engine_, arr, a0.AsNumber(), a1.AsNumber(), a2.AsNumber(), isMSL);
  return arr;
}

Value GlobeApi::api_Get3DPoints(const Value& a0, const Value& a1) {
  if (!IsSphereGeometry(currentGeometry_)) {
    return api_GetMercator3DPoints(a0);
  }
  return api_GetCartesian3DPoints(a0, a1);
}

Value GlobeApi::api_Get3DPointsByGeoArr(const Value& a0, const Value& a1, const Value& a2) {
  if (!IsSphereGeometry(currentGeometry_)) {
    return api_GetMercator3DPointsByGeoArr(a0, a1);
  }
  return api_GetCartesian3DPointsByGeoArr(a0, a1, a2);
}

Value GlobeApi::api_Get3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1, const Value& a2) {
  if (!IsSphereGeometry(currentGeometry_)) {
    return api_GetMercator3DPointsByGeoArr_SameZ(a0, a1);
  }
  return api_GetCartesian3DPointsByGeoArr_SameZ(a0, a1, a2);
}

Value GlobeApi::api_GetCartesian3DPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  bool isMSL = a3.AsBool(true);
  Value arr = Value::Array();
  AppendCartesianPoint(engine_, arr, a0.AsNumber(), a1.AsNumber(), a2.AsNumber(), isMSL);
  return arr;
}

Value GlobeApi::api_GetCartesian3DPoints(const Value& a0, const Value& a1) {
  if (!a0.IsArray()) {
    return Value::Array();
  }
  bool isMSL = a1.AsBool(true);
  Value out = Value::Array();
  for (size_t i = 0; i < a0.Size(); ++i) {
    Value item = a0.At(i);
    if (!item.IsObject()) continue;
    double lon = item.Get("long").IsNumber() ? item.Get("long").AsNumber() :
                 (item.Get("lon").IsNumber() ? item.Get("lon").AsNumber() : item.Get("lng").AsNumber());
    double lat = item.Get("lat").AsNumber();
    double z = item.Get("z").AsNumber();
    AppendCartesianPoint(engine_, out, lon, lat, z, isMSL);
  }
  return out;
}

Value GlobeApi::api_GetCartesian3DPointsByGeoArr(const Value& a0, const Value& a1, const Value& a2) {
  if (!a0.IsArray() || !a1.IsArray()) {
    return Value::Array();
  }
  bool isMSL = a2.AsBool(true);
  Value out = Value::Array();
  size_t count = a1.Size();
  for (size_t i = 0; i < count; ++i) {
    size_t idx = i * 2;
    if (idx + 1 >= a0.Size()) break;
    double lon = a0.At(idx).AsNumber();
    double lat = a0.At(idx + 1).AsNumber();
    double z = a1.At(i).AsNumber();
    AppendCartesianPoint(engine_, out, lon, lat, z, isMSL);
  }
  return out;
}

Value GlobeApi::api_GetCartesian3DPointsByGeoArr_SameZ(const Value& a0, const Value& a1, const Value& a2) {
  if (!a0.IsArray()) {
    return Value::Array();
  }
  bool isMSL = a2.AsBool(true);
  double z = a1.AsNumber();
  Value out = Value::Array();
  for (size_t i = 0; i + 1 < a0.Size(); i += 2) {
    double lon = a0.At(i).AsNumber();
    double lat = a0.At(i + 1).AsNumber();
    AppendCartesianPoint(engine_, out, lon, lat, z, isMSL);
  }
  return out;
}

Value GlobeApi::api_GeoToDMS(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  DegMinSec lonDms = GeoToDegMinSec(lon);
  DegMinSec latDms = GeoToDegMinSec(lat);

  Value result = Value::Object();
  Value lonObj = Value::Object();
  lonObj.Set("deg", Value::Number(lonDms.degree));
  lonObj.Set("min", Value::Number(lonDms.minute));
  lonObj.Set("sec", Value::Number(lonDms.second));
  lonObj.Set("direction", Value::String(lon >= 0 ? "E" : "W"));
  Value latObj = Value::Object();
  latObj.Set("deg", Value::Number(latDms.degree));
  latObj.Set("min", Value::Number(latDms.minute));
  latObj.Set("sec", Value::Number(latDms.second));
  latObj.Set("direction", Value::String(lat >= 0 ? "N" : "S"));
  result.Set("long", lonObj);
  result.Set("lat", latObj);
  return result;
}

Value GlobeApi::api_DMSToGeo(const Value& a0) {
  if (!a0.IsObject()) {
    return Value::String("Missing DMS object");
  }
  Value lonObj = a0.Get("long");
  Value latObj = a0.Get("lat");
  double lon = 0.0;
  double lat = 0.0;
  std::string err;
  if (!ParseDmsComponent(lonObj, true, lon, err)) {
    return Value::String(err);
  }
  if (!ParseDmsComponent(latObj, false, lat, err)) {
    return Value::String(err);
  }
  Value result = Value::Object();
  result.Set("long", Value::Number(lon));
  result.Set("lat", Value::Number(lat));
  return result;
}

Value GlobeApi::api_GetUTMZone(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  return Value::Number(static_cast<double>(GetUtmZoneFromLonLat(lon, lat)));
}

Value GlobeApi::api_GeoToUTM(const Value& a0, const Value& a1, const Value& a2) {
  (void)a0; // datum ignored, WGS84 assumed
  double lon = a1.AsNumber();
  double lat = a2.AsNumber();
  UtmCoord utm = GeoToUtm(lon, lat);
  Value result = Value::Object();
  result.Set("x", Value::Number(utm.x));
  result.Set("y", Value::Number(utm.y));
  result.Set("zone", Value::Number(static_cast<double>(utm.zone)));
  return result;
}

Value GlobeApi::api_UTMToGeo(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  (void)a0; // datum ignored, WGS84 assumed
  int zone = static_cast<int>(a1.AsNumber());
  double easting = a2.AsNumber();
  double northing = a3.AsNumber();
  double lon = 0.0;
  double lat = 0.0;
  if (!UtmToGeo(zone, easting, northing, lon, lat)) {
    return Value::Null();
  }
  Value result = Value::Object();
  result.Set("long", Value::Number(lon));
  result.Set("lat", Value::Number(lat));
  return result;
}

Value GlobeApi::api_GeoToMGRS(const Value& a0) {
  double lon = 0.0;
  double lat = 0.0;
  if (a0.IsArray() && a0.Size() >= 2) {
    lon = a0.At(0).AsNumber();
    lat = a0.At(1).AsNumber();
  } else if (a0.IsObject()) {
    Value lonVal = a0.Get("long");
    if (!lonVal.IsNumber()) lonVal = a0.Get("lon");
    if (!lonVal.IsNumber()) lonVal = a0.Get("lng");
    lon = lonVal.AsNumber();
    lat = a0.Get("lat").AsNumber();
  } else {
    return Value::Null();
  }
  std::string mgrs = GeoToMgrs(lon, lat);
  if (mgrs.empty()) return Value::Null();
  return Value::String(mgrs);
}

Value GlobeApi::api_MGRSToGeo(const Value& a0) {
  std::string mgrs = a0.AsString();
  double lon = 0.0;
  double lat = 0.0;
  if (!MgrsToGeo(mgrs, lon, lat)) {
    return Value::Null();
  }
  Value result = Value::Object();
  result.Set("long", Value::Number(lon));
  result.Set("lat", Value::Number(lat));
  return result;
}

Value GlobeApi::api_GeoToGeoRef(const Value& a0, const Value& a1) {
  double lon = a0.AsNumber();
  double lat = a1.AsNumber();
  if (lon < -180.0 || lon > 180.0 || lat < -90.0 || lat > 90.0) {
    return Value::String("");
  }
  static const std::string kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
  DegMinSec lonDms = GeoToDegMinSec(lon);
  DegMinSec latDms = GeoToDegMinSec(lat);
  int lon15 = static_cast<int>(std::floor((lon + 180.0) / 15.0));
  int lat15 = static_cast<int>(std::floor((lat + 90.0) / 15.0));
  int lon1 = static_cast<int>(std::floor(std::fmod(lon + 180.0, 15.0)));
  int lat1 = static_cast<int>(std::floor(std::fmod(lat + 90.0, 15.0)));

  double lonSecRounded = std::round(lonDms.second * 1e4) / 1e4;
  double latSecRounded = std::round(latDms.second * 1e4) / 1e4;
  int lonSecHundred = static_cast<int>(std::floor((lonSecRounded / 60.0) * 100.0));
  int latSecHundred = static_cast<int>(std::floor((latSecRounded / 60.0) * 100.0));

  std::string result;
  result.push_back(kLetters[static_cast<size_t>(lon15)]);
  result.push_back(kLetters[static_cast<size_t>(lat15)]);
  result.push_back(kLetters[static_cast<size_t>(lon1)]);
  result.push_back(kLetters[static_cast<size_t>(lat1)]);
  result += FormatPaddedInt(lonDms.minute, 2);
  result += FormatPaddedInt(lonSecHundred, 2);
  result += FormatPaddedInt(latDms.minute, 2);
  result += FormatPaddedInt(latSecHundred, 2);
  return Value::String(result);
}

Value GlobeApi::api_GeoRefToGeo(const Value& a0) {
  std::string s = ToUpper(a0.AsString());
  s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' '; }), s.end());
  if (s.size() != 8 && s.size() != 12) {
    return Value::Null();
  }
  static const std::string kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
  int lon15 = LetterIndex(kLetters, s[0]);
  int lat15 = LetterIndex(kLetters, s[1]);
  int lon1 = LetterIndex(kLetters, s[2]);
  int lat1 = LetterIndex(kLetters, s[3]);
  if (lon15 < 0 || lat15 < 0 || lon1 < 0 || lat1 < 0) {
    return Value::Null();
  }
  int lonDeg = 15 * lon15 + lon1 - 180;
  int latDeg = 15 * lat15 + lat1 - 90;

  int lonMin = std::stoi(s.substr(4, 2));
  int latMin = 0;
  double lonSec = 0.0;
  double latSec = 0.0;
  if (s.size() == 8) {
    latMin = std::stoi(s.substr(6, 2));
  } else {
    int lonSecHundred = std::stoi(s.substr(6, 2));
    latMin = std::stoi(s.substr(8, 2));
    int latSecHundred = std::stoi(s.substr(10, 2));
    lonSec = (lonSecHundred / 100.0) * 60.0;
    latSec = (latSecHundred / 100.0) * 60.0;
  }

  double lon = lonDeg >= 0 ? lonDeg + lonMin / 60.0 + lonSec / 3600.0
                           : lonDeg - lonMin / 60.0 - lonSec / 3600.0;
  double lat = latDeg >= 0 ? latDeg + latMin / 60.0 + latSec / 3600.0
                           : latDeg - latMin / 60.0 - latSec / 3600.0;

  Value result = Value::Object();
  result.Set("long", Value::Number(lon));
  result.Set("lat", Value::Number(lat));
  return result;
}

// P0 blockers
Value GlobeApi::api_GlobeVersion() {
  Value result = Value::Object();
  result.Set("version", Value::String("1.0.0"));
  result.Set("major", Value::Number(1));
  result.Set("minor", Value::Number(0));
  result.Set("patch", Value::Number(0));
  result.Set("build", Value::String("native-cpp"));
  return result;
}

Value GlobeApi::api_GetCurrentGeometry() {
  return Value::String(currentGeometry_);
}

Value GlobeApi::api_SetGeometry(const Value& a0) {
  std::string geom = a0.AsString("Sphere");
  // Supported geometries: "Sphere", "Flat", "Mercator"
  if (geom == "Sphere" || geom == "Flat" || geom == "Mercator") {
    currentGeometry_ = geom;
    engine_.Set2DMode(geom != "Sphere");
    // JS parity: reset screen position history on geometry switch
    engine_.ResetPositionHistory();
    return Value::Bool(true);
  }
  return Value::Bool(false);
}

Value GlobeApi::api_GetMousePos() {
  double cursorX = 0.0;
  double cursorY = 0.0;
  if (engine_.GetCursorPos(cursorX, cursorY)) {
    mouseX_ = static_cast<int>(cursorX);
    mouseY_ = static_cast<int>(cursorY);
  }
  Value result = Value::Object();
  result.Set("x", Value::Number(static_cast<double>(mouseX_)));
  result.Set("y", Value::Number(static_cast<double>(mouseY_)));
  return result;
}

Value GlobeApi::api_GetMouseDeg() {
  double cursorX = 0.0;
  double cursorY = 0.0;
  if (engine_.GetCursorPos(cursorX, cursorY)) {
    mouseX_ = static_cast<int>(cursorX);
    mouseY_ = static_cast<int>(cursorY);
  }
  double lat, lon;
  if (engine_.ScreenToGeo(mouseX_, mouseY_, lat, lon)) {
    Value result = Value::Object();
    result.Set("lat", Value::Number(lat));
    result.Set("lon", Value::Number(lon));
    result.Set("long", Value::Number(lon));
    result.Set("lng", Value::Number(lon));
    return result;
  }
  return Value::Null();
}

Value GlobeApi::api_GetGL() {
  // Return GL context info - simplified for native implementation
  Value result = Value::Object();
  result.Set("valid", Value::Bool(valid_));
  result.Set("type", Value::String("OpenGL"));
  return result;
}

// Raster support (P1)
Value GlobeApi::api_AddCustomFont(const Value& a0, const Value& a1) {
  // a0: customName, a1: customFontParams
  (void)a0; (void)a1;
  return Value::Null();
}

Value GlobeApi::api_AddIconMap(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  // a0: name, a1: imageUrl, a2: jsonUrl, a3: callback
  std::string name = a0.AsString();
  std::string imageUrl = a1.AsString();
  std::string jsonUrl = a2.AsString();
  (void)a3; // callback not yet supported via Value (requires JS binding)

  engine_.AddIconMap(name, imageUrl, jsonUrl, [name](bool success) {
      // NOTE: We cannot invoke JS callback 'a3' here because we don't hold a V8/JS reference.
      // This lambda runs on the engine's worker thread (or main thread after dispatch).
      if (success) {
          printf("IconMap '%s' loaded successfully.\n", name.c_str());
      } else {
          printf("Failed to load IconMap '%s'.\n", name.c_str());
      }
  });
  return Value::Null();
}

Value GlobeApi::api_AddImageOverlay(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) {
  int id = static_cast<int>(a0.AsNumber());
  std::string url = a1.AsString();
  
  double minLon = 0, minLat = 0, maxLon = 0, maxLat = 0;
  if (!a2.IsNull()) {
      Value ll = a2.Get("ll");
      Value ur = a2.Get("ur");
      if (!ll.IsNull()) {
          minLon = ll.Get("x").AsNumber();
          minLat = ll.Get("y").AsNumber();
      }
      if (!ur.IsNull()) {
          maxLon = ur.Get("x").AsNumber();
          maxLat = ur.Get("y").AsNumber();
      }
  }
  
  float opacity = static_cast<float>(a4.AsNumber(1.0));
  float rotation = static_cast<float>(a5.AsNumber(0.0));
  
  engine_.AddImageOverlay(id, url, minLon, minLat, maxLon, maxLat, opacity, rotation);
  
  if (!a3.IsNull()) {
      glm::vec4 color = StringToColor(a3.AsString(), 1.0f);
      engine_.SetImageOverlayColor(id, color, opacity);
  }
  
  return Value::Null();
}

Value GlobeApi::api_SetImageOverlayColor(const Value& a0, const Value& a1, const Value& a2) {
  int id = static_cast<int>(a0.AsNumber());
  glm::vec4 color = StringToColor(a1.AsString(), 1.0f);
  float opacity = static_cast<float>(a2.AsNumber(1.0));
  engine_.SetImageOverlayColor(id, color, opacity);
  return Value::Null();
}

Value GlobeApi::api_ChangeImageOverlayURL(const Value& a0, const Value& a1) {
  int id = static_cast<int>(a0.AsNumber());
  std::string url = a1.AsString();
  engine_.ChangeImageOverlayURL(id, url);
  return Value::Null();
}

Value GlobeApi::api_DeleteImageOverlay(const Value& a0, const Value& a1) {
  (void)a1;
  if (a0.IsNull()) {
      engine_.DeleteAllImageOverlays();
  } else {
      int id = static_cast<int>(a0.AsNumber());
      engine_.DeleteImageOverlay(id);
  }
  return Value::Null();
}

Value GlobeApi::api_LineOfSight(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) {
    // a6, a7, a8: color, width, callback? Ignored for parity MVP.
    (void)a6; (void)a7; (void)a8;
    double lat1 = a0.AsNumber();
    double lon1 = a1.AsNumber();
    double alt1 = a2.AsNumber();
    double lat2 = a3.AsNumber();
    double lon2 = a4.AsNumber();
    double alt2 = a5.AsNumber();
    
    auto res = engine_.CheckLineOfSight(lat1, lon1, alt1, lat2, lon2, alt2);
    
    Value ret = Value::Object();
    ret.Set("visible", Value::Bool(res.visible));
    if (!res.visible) {
        Value hit = Value::Object();
        hit.Set("lat", Value::Number(res.hitLat));
        hit.Set("lon", Value::Number(res.hitLon));
        hit.Set("alt", Value::Number(res.hitAlt));
        ret.Set("hit", hit);
    }
    return ret;
}

Value GlobeApi::api_FindProfile(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
    std::vector<double> path;
    int lod = 10;
    
    if (a0.IsArray()) {
        size_t len = a0.Size();
        for(size_t i=0; i<len; ++i) path.push_back(a0.At(i).AsNumber());
        lod = static_cast<int>(a1.AsNumber(10.0));
    } else if (a0.IsNumber() && a1.IsNumber() && a2.IsNumber() && a3.IsNumber()) {
        // Legacy 4-arg: lat1, lon1, lat2, lon2
        path.push_back(a1.AsNumber()); // Lon1
        path.push_back(a0.AsNumber()); // Lat1
        path.push_back(a3.AsNumber()); // Lon2
        path.push_back(a2.AsNumber()); // Lat2
    }
    
    double stepMeters = 40075016.0 / (256.0 * std::pow(2.0, lod));
    if (stepMeters < 1.0) stepMeters = 1.0;
    
    Value arr = Value::Array();
    double totalDist = 0.0;
    
    for (size_t i = 0; i + 3 < path.size(); i += 2) {
        double lon1 = path[i];
        double lat1 = path[i+1];
        double lon2 = path[i+2];
        double lat2 = path[i+3];
        
        double distDeg = std::sqrt(std::pow(lat2-lat1, 2) + std::pow(lon2-lon1, 2));
        double distMeters = distDeg * 111000.0;
        int samples = std::max(2, static_cast<int>(distMeters / stepMeters));
        if (samples > 1000) samples = 1000;
        
        auto segment = engine_.GetElevationProfile(lat1, lon1, lat2, lon2, samples, lod);
        
        for (const auto& p : segment) {
            Value item = Value::Array();
            item.Push(Value::Number(totalDist + p.dist));
            item.Push(Value::Number(p.lon));
            item.Push(Value::Number(p.lat));
            item.Push(Value::Number(p.height));
            arr.Push(item);
        }
        if (!segment.empty()) {
            totalDist += segment.back().dist;
        }
    }
    return arr;
}

Value GlobeApi::api_AddRaster(const Value& a0, const Value& a1) {
  std::string id = a0.AsString();
  std::string url = a1.AsString();
  
  RasterLayerConfig config;
  config.id = id;
  config.name = id;
  config.url = url;
  config.type = RasterLayerType::XYZ;
  config.visible = true;
  config.opacity = 1.0f;
  
  engine_.AddRasterLayer(config);
  return Value::String(id);
}

Value GlobeApi::api_SetRasterService(const Value& a0, const Value& a1, const Value& a2) {
  std::string id = a0.AsString();
  std::string url = a1.AsString();
  std::string supportUrl;
  bool supportTransparent = false;
  bool supportEmptyContent = false;
  bool supportOutOfBBOX = false;
  if (a2.IsObject()) {
    Value urlVal = a2.Get("url");
    if (urlVal.IsString()) {
      supportUrl = urlVal.AsString();
    }
    supportTransparent = a2.Get("transparentPixelSupport").AsBool(false);
    supportEmptyContent = a2.Get("emptyContentSupport").AsBool(false);
    supportOutOfBBOX = a2.Get("outOfBBOXSupport").AsBool(false);
  } else {
    supportUrl = a2.AsString();
  }
  
  RasterLayerConfig config;
  config.id = id;
  config.name = id;
  config.url = url;
  config.supportUrl = supportUrl;
  config.supportTransparentPixel = supportTransparent;
  config.supportEmptyContent = supportEmptyContent;
  config.supportOutOfBBOX = supportOutOfBBOX;
  config.type = RasterLayerType::XYZ;
  
  engine_.AddRasterLayer(config);
  return Value::String(id);
}

Value GlobeApi::api_DeleteRaster(const Value& a0) {
  std::string id;
  if (a0.IsString()) {
    id = a0.AsString();
  } else {
    int index = static_cast<int>(a0.AsNumber(-1));
    RasterLayerConfig cfg;
    if (index >= 0 && engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
      id = cfg.id;
    }
  }
  if (id.empty()) {
    return Value::Bool(false);
  }
  engine_.RemoveRasterLayer(id);
  return Value::Bool(true);
}

Value GlobeApi::api_GetRaster(const Value& a0) {
  int index = static_cast<int>(a0.AsNumber(-1));
  if (index < 0) {
    return Value::Null();
  }
  RasterLayerConfig cfg;
  if (!engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    return Value::Null();
  }
  return RasterConfigToValue(cfg);
}

Value GlobeApi::api_GetRasterById(const Value& a0) {
  std::string id = a0.AsString();
  RasterLayerConfig cfg;
  if (!engine_.GetRasterLayerConfigById(id, cfg)) {
    return Value::Null();
  }
  return RasterConfigToValue(cfg);
}

Value GlobeApi::api_SetRasterONOFF(const Value& a0, const Value& a1) {
  bool visible = a1.AsBool(true);
  if (a0.IsString()) {
    engine_.SetRasterLayerVisibility(a0.AsString(), visible);
    return Value::Null();
  }
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index >= 0 && engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    engine_.SetRasterLayerVisibility(cfg.id, visible);
  }
  return Value::Null();
}

Value GlobeApi::api_GetRasterONOFF(const Value& a0) {
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index < 0 || !engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    return Value::Null();
  }
  return Value::Bool(cfg.visible);
}

Value GlobeApi::api_GetRasterONOFFById(const Value& a0) {
  RasterLayerConfig cfg;
  if (!engine_.GetRasterLayerConfigById(a0.AsString(), cfg)) {
    return Value::Null();
  }
  return Value::Bool(cfg.visible);
}

Value GlobeApi::api_SetRasterOpacity(const Value& a0, const Value& a1) {
  float opacity = static_cast<float>(a1.AsNumber(1.0));
  if (a0.IsString()) {
    engine_.SetRasterLayerOpacity(a0.AsString(), opacity);
    return Value::Null();
  }
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index >= 0 && engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    engine_.SetRasterLayerOpacity(cfg.id, opacity);
  }
  return Value::Null();
}

Value GlobeApi::api_GetRasterOpacity(const Value& a0) {
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index < 0 || !engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    return Value::Null();
  }
  return Value::Number(static_cast<double>(cfg.opacity));
}

Value GlobeApi::api_SetRasterZIndex(const Value& a0, const Value& a1) {
  int zIndex = static_cast<int>(a1.AsNumber(0.0));
  if (a0.IsString()) {
    engine_.SetRasterLayerZIndex(a0.AsString(), zIndex);
    return Value::Null();
  }
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index >= 0 && engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    engine_.SetRasterLayerZIndex(cfg.id, zIndex);
  }
  return Value::Null();
}

Value GlobeApi::api_GetRasterZIndex(const Value& a0) {
  int index = static_cast<int>(a0.AsNumber(-1));
  RasterLayerConfig cfg;
  if (index < 0 || !engine_.GetRasterLayerConfigByIndex(static_cast<size_t>(index), cfg)) {
    return Value::Null();
  }
  return Value::Number(static_cast<double>(cfg.zIndex));
}

Value GlobeApi::api_GetNewRasterId() {
  std::string id;
  bool unique = false;
  while (!unique) {
    id = "raster_" + std::to_string(nextRasterId_++);
    unique = true;
    for (const auto& existing : engine_.GetRasterLayerIds()) {
      if (existing == id) {
        unique = false;
        break;
      }
    }
  }
  return Value::String(id);
}

Value GlobeApi::api_RasterCount() {
  return Value::Number(static_cast<double>(engine_.GetRasterLayerIds().size()));
}

Value GlobeApi::api_AddWMSOverlay(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6) {
  // a0: id, a1: WMSUrl, a2: color, a3: opacity, a4: imgSize, a5: beforeObject, a6: WMSCallback
  std::string id = a0.AsString();
  std::string url = a1.AsString();
  std::string colorStr = a2.AsString();
  float opacity = static_cast<float>(a3.AsNumber(1.0));
  int imgSize = static_cast<int>(a4.AsNumber(256));

  RasterLayerConfig config;
  config.id = id;
  config.name = id;
  config.url = url;
  config.type = RasterLayerType::WMS;
  config.opacity = opacity;
  config.color = StringToColor(colorStr, 1.0f);
  config.tileWidth = imgSize;
  config.tileHeight = imgSize;

  engine_.AddRasterLayer(config);
  return Value::String(id);
}

Value GlobeApi::api_SetWMSOverlayColor(const Value& a0, const Value& a1, const Value& a2) {
  // a0: id, a1: color, a2: opacity
  std::string id = a0.AsString();
  std::string colorStr = a1.AsString();
  float opacity = static_cast<float>(a2.AsNumber(1.0));

  RasterLayerConfig cfg;
  if (engine_.GetRasterLayerConfigById(id, cfg)) {
    cfg.color = StringToColor(colorStr, 1.0f);
    cfg.opacity = opacity;
    engine_.AddRasterLayer(cfg);
  }
  return Value::Null();
}

Value GlobeApi::api_DeleteWMSOverlay(const Value& a0) {
  return api_DeleteRaster(a0);
}

void GlobeApi::RecordDrawCommand(const std::string& name, std::vector<Value> args) {
  while (drawCommands_.size() >= kMaxDrawCommands) {
    drawCommands_.erase(drawCommands_.begin());
  }
  DrawCommand cmd;
  cmd.name = name;
  cmd.args = std::move(args);
  drawCommands_.push_back(std::move(cmd));
}

Value GlobeApi::api_GetDrawCommands() {
  Value out = Value::Array();
  for (const auto& cmd : drawCommands_) {
    Value item = Value::Object();
    item.Set("name", Value::String(cmd.name));
    Value args = Value::Array();
    for (const auto& arg : cmd.args) {
      args.Push(arg);
    }
    item.Set("args", args);
    out.Push(item);
  }
  return out;
}

Value GlobeApi::api_ClearDrawCommands() {
  drawCommands_.clear();
  return Value::Null();
}

Value GlobeApi::api_Draw3dDashedLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) {
  RecordDrawCommand("api_Draw3dDashedLine", {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10});
  return Value::Null();
}

Value GlobeApi::api_Draw3dDashedLineLoop(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) {
  RecordDrawCommand("api_Draw3dDashedLineLoop", {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10});
  return Value::Null();
}

Value GlobeApi::api_Draw3dDashedLineLoopCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_Draw3dDashedLineLoopCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_Draw3dDashedLineStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9, const Value& a10) {
  RecordDrawCommand("api_Draw3dDashedLineStrip", {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10});
  return Value::Null();
}

Value GlobeApi::api_Draw3dDashedLineStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_Draw3dDashedLineStripCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) {
  RecordDrawCommand("api_Draw3dLine", {a0, a1, a2, a3, a4, a5, a6, a7, a8});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLineCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_Draw3dLineCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLineLoop(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) {
  RecordDrawCommand("api_Draw3dLineLoop", {a0, a1, a2, a3, a4, a5, a6, a7, a8});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLineLoopCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_Draw3dLineLoopCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLineStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) {
  RecordDrawCommand("api_Draw3dLineStrip", {a0, a1, a2, a3, a4, a5, a6, a7, a8});
  return Value::Null();
}

Value GlobeApi::api_Draw3dLineStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_Draw3dLineStripCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_Draw3dPolygon(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9) {
  RecordDrawCommand("api_Draw3dPolygon", {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9});
  return Value::Null();
}

Value GlobeApi::api_Draw3dPolygonCurModelProjection(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  RecordDrawCommand("api_Draw3dPolygonCurModelProjection", {a0, a1, a2, a3});
  return Value::Null();
}

Value GlobeApi::api_Draw3dPolygonStrip(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8, const Value& a9) {
  RecordDrawCommand("api_Draw3dPolygonStrip", {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9});
  return Value::Null();
}

Value GlobeApi::api_Draw3dPolygonStripCurModelProjection(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  RecordDrawCommand("api_Draw3dPolygonStripCurModelProjection", {a0, a1, a2, a3});
  return Value::Null();
}

Value GlobeApi::api_DrawBaseGlobeColor(const Value& a0) {
  RecordDrawCommand("api_DrawBaseGlobeColor", {a0});
  return Value::Null();
}

Value GlobeApi::api_DrawCircle(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7, const Value& a8) {
  RecordDrawCommand("api_DrawCircle", {a0, a1, a2, a3, a4, a5, a6, a7, a8});
  return Value::Null();
}

Value GlobeApi::api_DrawCircleCurModelProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_DrawCircleCurModelProjection", {a0, a1, a2});
  return Value::Null();
}

Value GlobeApi::api_DrawContextText(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) {
  RecordDrawCommand("api_DrawContextText", {a0, a1, a2, a3, a4, a5, a6, a7});
  return Value::Null();
}

Value GlobeApi::api_DrawContextTextInScreen(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  RecordDrawCommand("api_DrawContextTextInScreen", {a0, a1, a2, a3});
  return Value::Null();
}

Value GlobeApi::api_DrawContextTextMultiLine(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6, const Value& a7) {
  RecordDrawCommand("api_DrawContextTextMultiLine", {a0, a1, a2, a3, a4, a5, a6, a7});
  return Value::Null();
}

Value GlobeApi::api_DrawIcon(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4, const Value& a5, const Value& a6) {
  RecordDrawCommand("api_DrawIcon", {a0, a1, a2, a3, a4, a5, a6});
  return Value::Null();
}

Value GlobeApi::api_DrawIconCurProjection(const Value& a0, const Value& a1, const Value& a2) {
  RecordDrawCommand("api_DrawIconCurProjection", {a0, a1, a2});
  return Value::Null();
}

// ============================================================================
// EDIT APIs (Phase 3)
// ============================================================================

Value GlobeApi::api_StartEditObj(const Value& a0, const Value& a1, const Value& a2, const Value& a3) {
  if (editState_.active) {
    return Value::Bool(false);  // Already editing
  }
  
  editState_.active = true;
  editState_.objectId = a0.AsString();
  editState_.objectData = a1;
  editState_.undoStack.clear();
  editState_.redoStack.clear();
  
  // Save initial state
  editState_.undoStack.push_back(a1);
  
  // Trigger callback if registered
  auto it = callbacks_.find("undobufferschanged");
  if (it != callbacks_.end()) {
    // Notify that undo buffers changed
  }
  
  return Value::Bool(true);
}

Value GlobeApi::api_StopEditObj(const Value& a0, const Value& a1) {
  if (!editState_.active) {
    return Value::Bool(false);
  }
  
  bool save = a1.IsBool() ? a1.AsBool() : true;
  
  if (!save) {
    // Restore original state if not saving
    if (!editState_.undoStack.empty()) {
      editState_.objectData = editState_.undoStack.front();
    }
  }
  
  editState_.active = false;
  editState_.undoStack.clear();
  editState_.redoStack.clear();
  
  return Value::Bool(true);
}

Value GlobeApi::api_CanUndoEdit() {
  return Value::Bool(editState_.active && editState_.undoStack.size() > 1);
}

Value GlobeApi::api_CanRedoEdit() {
  return Value::Bool(editState_.active && !editState_.redoStack.empty());
}

Value GlobeApi::api_UndoEdit() {
  if (!editState_.active || editState_.undoStack.size() <= 1) {
    return Value::Bool(false);
  }
  
  // Move current state to redo stack
  editState_.redoStack.push_back(editState_.undoStack.back());
  editState_.undoStack.pop_back();
  
  // Restore previous state
  editState_.objectData = editState_.undoStack.back();
  
  return Value::Bool(true);
}

Value GlobeApi::api_RedoEdit() {
  if (!editState_.active || editState_.redoStack.empty()) {
    return Value::Bool(false);
  }
  
  // Move redo state to undo stack
  editState_.undoStack.push_back(editState_.redoStack.back());
  editState_.redoStack.pop_back();
  
  // Apply restored state
  editState_.objectData = editState_.undoStack.back();
  
  return Value::Bool(true);
}

// ============================================================================
// PLUGIN APIs (Phase 3)
// ============================================================================

Value GlobeApi::api_RegisterPlugin(const Value& a0, const Value& a1) {
  std::string pluginId = a0.AsString();
  if (pluginId.empty()) {
    return Value::Bool(false);
  }
  
  plugins_[pluginId] = a1;
  return Value::Bool(true);
}

Value GlobeApi::api_UnRegisterPlugin(const Value& a0) {
  std::string pluginId = a0.AsString();
  auto it = plugins_.find(pluginId);
  if (it == plugins_.end()) {
    return Value::Bool(false);
  }
  
  plugins_.erase(it);
  return Value::Bool(true);
}

Value GlobeApi::api_GetPlugin(const Value& a0) {
  std::string pluginId = a0.AsString();
  auto it = plugins_.find(pluginId);
  if (it == plugins_.end()) {
    return Value::Null();
  }
  return it->second;
}

Value GlobeApi::api_GetAllPluginsId() {
  Value arr = Value::Array();
  for (const auto& kv : plugins_) {
    arr.Push(Value::String(kv.first));
  }
  return arr;
}

// ============================================================================
// NAVIGATION HISTORY APIs (Phase 4C)
// ============================================================================

Value GlobeApi::api_GoToPreviousPosition() {
  return Value::Bool(engine_.GoToPreviousPosition());
}

Value GlobeApi::api_GoToNextPosition() {
  return Value::Bool(engine_.GoToNextPosition());
}

Value GlobeApi::api_IsPreviousPositionAvailable() {
  return Value::Bool(engine_.IsPreviousPositionAvailable());
}

Value GlobeApi::api_IsNextPositionAvailable() {
  return Value::Bool(engine_.IsNextPositionAvailable());
}

Value GlobeApi::api_TurnToNorth(const Value& a0) {
  // a0: degree (optional)
  double angle = a0.IsNumber() ? a0.AsNumber() : 0.0;
  engine_.TurnToNorthAngle(angle, 0.5);
  return Value::Null();
}

Value GlobeApi::api_TurnToNorthDirect(const Value& a0) {
  // a0: degree (optional)
  double angle = a0.IsNumber() ? a0.AsNumber() : 0.0;
  engine_.SetNorthAngle(angle);
  return Value::Null();
}
