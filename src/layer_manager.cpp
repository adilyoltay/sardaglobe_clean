#include "layer_manager.h"

#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

namespace {

const char* kLayerVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aUV; // minU, minV, widthU, heightU
uniform mat4 uMVP;
uniform float uPointSize;
out vec4 vUV;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  gl_PointSize = uPointSize;
  vUV = aUV;
}
)";

const char* kLayerFragmentShader = R"(
#version 330 core
uniform vec4 uColor;
uniform sampler2D uTex;
uniform bool uUseTexture;
uniform float uTime;        // Time in seconds
uniform float uFlashPeriod; // Period in milliseconds
uniform bool uFlashEnabled; // Gating flag
in vec4 vUV;
out vec4 FragColor;
void main() {
  vec4 finalColor = uColor;
  
  // Flash logic (simplified: pulse alpha if period > 0)
  // uFlashPeriod is in ms, uTime is in seconds.
  // Flash frequency = 1 / (period * 0.001)
  if (uFlashEnabled && uFlashPeriod > 0.0) {
    float periodSec = uFlashPeriod * 0.001;
    float t = mod(uTime, periodSec) / periodSec;
    // Simple pulse: 0->1->0
    float pulse = 0.5 + 0.5 * sin(t * 6.28318);
    // Or on/off blink like standard military symbols
    // float pulse = (t < 0.5) ? 1.0 : 0.0;
    
    // Applying to alpha or color? Let's pulse alpha slightly for now or visibility?
    // JS parity usually means "blink" (visible/invisible or color toggle).
    // Let's assume transparency blink.
    finalColor.a *= pulse;
  }

  if (uUseTexture) {
    // Flip Y for gl_PointCoord if needed (OpenGL origin is bottom-left, texture usually top-left)
    // Assuming standard gl_PointCoord (0,0 top-left to 1,1 bottom-right? No, 0,0 is usually top-left or bottom-left depending on driver?)
    // Standard OpenGL: gl_PointCoord (0,0) is top-left? No, usually (0,1) is top-left in UV space but PointCoord varies.
    // Let's assume standard mapping: vUV.xy + gl_PointCoord * vUV.zw
    vec2 coord = vUV.xy + gl_PointCoord * vUV.zw;
    vec4 texColor = texture(uTex, coord);
    FragColor = texColor * finalColor;
  } else {
    FragColor = finalColor;
  }
}
)";

GLuint CompileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint CreateProgram(const char* vs, const char* fs) {
  GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
  GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
  GLuint program = glCreateProgram();
  glAttachShader(program, v);
  glAttachShader(program, f);
  glLinkProgram(program);
  glDeleteShader(v);
  glDeleteShader(f);
  return program;
}

}  // namespace

LayerManager::LayerManager() = default;

LayerManager::~LayerManager() {
  DeleteAllLayers();
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
}

void LayerManager::InitShaders() {
  if (shadersInitialized_) return;
  program_ = CreateProgram(kLayerVertexShader, kLayerFragmentShader);
  mvpLoc_ = glGetUniformLocation(program_, "uMVP");
  colorLoc_ = glGetUniformLocation(program_, "uColor");
  pointSizeLoc_ = glGetUniformLocation(program_, "uPointSize");
  texLoc_ = glGetUniformLocation(program_, "uTex");
  useTexLoc_ = glGetUniformLocation(program_, "uUseTexture");
  timeLoc_ = glGetUniformLocation(program_, "uTime");
  flashPeriodLoc_ = glGetUniformLocation(program_, "uFlashPeriod");
  flashEnabledLoc_ = glGetUniformLocation(program_, "uFlashEnabled");
  shadersInitialized_ = true;
}

std::string LayerManager::AddLayer(const std::string& id, const std::string& name, LayerType type) {
  std::string layerId = id.empty() ? GetNewLayerId() : id;
  
  // Check if layer already exists
  if (idToIndex_.find(layerId) != idToIndex_.end()) {
    return "";
  }
  
  auto layer = std::make_unique<Layer>();
  layer->id = layerId;
  layer->name = name.empty() ? layerId : name;
  layer->type = type;
  layer->drawOrder = static_cast<int>(layers_.size());
  
  layers_.push_back(std::move(layer));
  RebuildIndex();
  
  return layerId;
}

bool LayerManager::DeleteLayer(const std::string& id) {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return false;
  }
  
  size_t index = it->second;
  DestroyLayerBuffers(*layers_[index]);
  layers_.erase(layers_.begin() + static_cast<long>(index));
  RebuildIndex();
  
  return true;
}

void LayerManager::DeleteAllLayers() {
  for (auto& layer : layers_) {
    DestroyLayerBuffers(*layer);
  }
  layers_.clear();
  idToIndex_.clear();
}

Layer* LayerManager::GetLayer(const std::string& id) {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return nullptr;
  }
  return layers_[it->second].get();
}

Layer* LayerManager::GetLayerByIndex(size_t index) {
  if (index >= layers_.size()) {
    return nullptr;
  }
  return layers_[index].get();
}

size_t LayerManager::GetLayerCount() const {
  return layers_.size();
}

std::string LayerManager::GetNewLayerId() {
  return "$Layer$" + std::to_string(nextId_++);
}

void LayerManager::SetLayerVisible(const std::string& id, bool visible) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->visible = visible;
  }
}

bool LayerManager::GetLayerVisible(const std::string& id) const {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return false;
  }
  return layers_[it->second]->visible;
}

void LayerManager::SetLayerOpacity(const std::string& id, float opacity) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->opacity = std::clamp(opacity, 0.0f, 1.0f);
  }
}

float LayerManager::GetLayerOpacity(const std::string& id) const {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return 1.0f;
  }
  return layers_[it->second]->opacity;
}

void LayerManager::SetLayerStyle(const std::string& id, const LayerStyle& style) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->style = style;
    layer->dirty = true;
  }
}

LayerStyle LayerManager::GetLayerStyle(const std::string& id) const {
  auto it = idToIndex_.find(id);
  if (it == idToIndex_.end()) {
    return LayerStyle();
  }
  return layers_[it->second]->style;
}

void LayerManager::StyleChanged(const std::string& id) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->dirty = true;
  }
}

void LayerManager::SetLayerData(const std::string& id, std::vector<Feature>&& features) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->features = std::move(features);
    for (auto& f : layer->features) {
      f.ComputeBounds();
    }
    layer->ComputeBounds();
    layer->dirty = true;
  }
}

void LayerManager::AddFeature(const std::string& layerId, Feature&& feature) {
  Layer* layer = GetLayer(layerId);
  if (layer) {
    feature.ComputeBounds();
    layer->features.push_back(std::move(feature));
    layer->ComputeBounds();
    layer->dirty = true;
  }
}

void LayerManager::RemoveFeature(const std::string& layerId, const std::string& featureId) {
  Layer* layer = GetLayer(layerId);
  if (layer) {
    auto it = std::remove_if(layer->features.begin(), layer->features.end(),
                             [&](const Feature& f) { return f.id == featureId; });
    if (it != layer->features.end()) {
      layer->features.erase(it, layer->features.end());
      layer->ComputeBounds();
      layer->dirty = true;
    }
  }
}

void LayerManager::UpdateFeature(const std::string& layerId, const std::string& featureId, Feature&& feature) {
  Layer* layer = GetLayer(layerId);
  if (layer) {
    for (auto& f : layer->features) {
      if (f.id == featureId) {
        f = std::move(feature);
        f.ComputeBounds();
        layer->ComputeBounds();
        layer->dirty = true;
        break;
      }
    }
  }
}

Feature* LayerManager::GetFeature(const std::string& layerId, const std::string& featureId) {
  Layer* layer = GetLayer(layerId);
  if (layer) {
    for (auto& f : layer->features) {
      if (f.id == featureId) {
        return &f;
      }
    }
  }
  return nullptr;
}

void LayerManager::SetSelectedList(const std::string& layerId, const std::vector<std::string>& ids) {
  Layer* layer = GetLayer(layerId);
  if (layer) {
    // Clear previous selection
    for (auto& f : layer->features) {
      f.selected = false;
    }
    layer->selectedIds = ids;
    
    // Set new selection
    for (const auto& id : ids) {
      for (auto& f : layer->features) {
        if (f.id == id) {
          f.selected = true;
          break;
        }
      }
    }
  }
}

std::vector<std::string> LayerManager::GetSelectedList(const std::string& layerId) const {
  auto it = idToIndex_.find(layerId);
  if (it == idToIndex_.end()) {
    return {};
  }
  return layers_[it->second]->selectedIds;
}

void LayerManager::ClearSelection(const std::string& layerId) {
  SetSelectedList(layerId, {});
}

void LayerManager::SetDrawOrder(const std::string& id, int order) {
  Layer* layer = GetLayer(id);
  if (layer) {
    layer->drawOrder = order;
  }
}

void LayerManager::BringToFront(const std::string& id) {
  int maxOrder = 0;
  for (const auto& l : layers_) {
    if (l->drawOrder > maxOrder) {
      maxOrder = l->drawOrder;
    }
  }
  SetDrawOrder(id, maxOrder + 1);
}

void LayerManager::SendToBack(const std::string& id) {
  int minOrder = 0;
  for (const auto& l : layers_) {
    if (l->drawOrder < minOrder) {
      minOrder = l->drawOrder;
    }
  }
  SetDrawOrder(id, minOrder - 1);
}

std::vector<Feature*> LayerManager::QueryByPoint(double lon, double lat, double toleranceDeg) {
  std::vector<Feature*> results;
  double minLon = lon - toleranceDeg;
  double maxLon = lon + toleranceDeg;
  double minLat = lat - toleranceDeg;
  double maxLat = lat + toleranceDeg;
  
  for (auto& layer : layers_) {
    if (!layer->visible) continue;
    
    for (auto& f : layer->features) {
      if (!f.visible) continue;
      
      // Quick AABB check
      if (f.maxLon < minLon || f.minLon > maxLon ||
          f.maxLat < minLat || f.minLat > maxLat) {
        continue;
      }
      
      // For points, check distance
      if (f.geometryType == GeometryType::Point && f.coordinates.size() >= 2) {
        double dx = f.coordinates[0] - lon;
        double dy = f.coordinates[1] - lat;
        if (dx * dx + dy * dy <= toleranceDeg * toleranceDeg) {
          results.push_back(&f);
        }
      } else {
        // For other geometries, AABB overlap is enough for now
        results.push_back(&f);
      }
    }
  }
  
  return results;
}

std::vector<Feature*> LayerManager::QueryByBBox(double minLon, double minLat, double maxLon, double maxLat) {
  std::vector<Feature*> results;
  
  for (auto& layer : layers_) {
    if (!layer->visible) continue;
    
    for (auto& f : layer->features) {
      if (!f.visible) continue;
      
      // AABB intersection
      if (f.maxLon >= minLon && f.minLon <= maxLon &&
          f.maxLat >= minLat && f.minLat <= maxLat) {
        results.push_back(&f);
      }
    }
  }
  
  return results;
}

Feature* LayerManager::GetNearestFeature(double lon, double lat, double maxDistanceDeg) {
  Feature* nearest = nullptr;
  double nearestDist = maxDistanceDeg;
  
  for (auto& layer : layers_) {
    if (!layer->visible) continue;
    
    for (auto& f : layer->features) {
      if (!f.visible) continue;
      
      double dist = maxDistanceDeg + 1.0;
      
      if (f.geometryType == GeometryType::Point && f.coordinates.size() >= 2) {
        dist = PointToPointDistance(lon, lat, f.coordinates[0], f.coordinates[1]);
      } else if (f.geometryType == GeometryType::Line || f.geometryType == GeometryType::MultiLine) {
        for (size_t i = 0; i + 3 < f.coordinates.size(); i += 2) {
          double d = PointToLineDistance(lon, lat, 
                                         f.coordinates[i], f.coordinates[i + 1],
                                         f.coordinates[i + 2], f.coordinates[i + 3]);
          if (d < dist) dist = d;
        }
      } else if (f.geometryType == GeometryType::Polygon || f.geometryType == GeometryType::MultiPolygon) {
        if (PointInPolygon(lon, lat, f.coordinates)) {
          dist = 0.0;
        } else {
          // Check distance to polygon edges
          for (size_t i = 0; i + 3 < f.coordinates.size(); i += 2) {
            double d = PointToLineDistance(lon, lat,
                                           f.coordinates[i], f.coordinates[i + 1],
                                           f.coordinates[i + 2], f.coordinates[i + 3]);
            if (d < dist) dist = d;
          }
        }
      }
      
      if (dist < nearestDist) {
        nearestDist = dist;
        nearest = &f;
      }
    }
  }
  
  return nearest;
}

bool LayerManager::PointInPolygon(double testLon, double testLat, const std::vector<double>& coords) {
  if (coords.size() < 6) return false; // Need at least 3 points
  
  bool inside = false;
  size_t n = coords.size() / 2;
  
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    double xi = coords[i * 2];
    double yi = coords[i * 2 + 1];
    double xj = coords[j * 2];
    double yj = coords[j * 2 + 1];
    
    if (((yi > testLat) != (yj > testLat)) &&
        (testLon < (xj - xi) * (testLat - yi) / (yj - yi) + xi)) {
      inside = !inside;
    }
  }
  
  return inside;
}

double LayerManager::PointToLineDistance(double pLon, double pLat, 
                                          double l1Lon, double l1Lat, 
                                          double l2Lon, double l2Lat) {
  double dx = l2Lon - l1Lon;
  double dy = l2Lat - l1Lat;
  double lengthSq = dx * dx + dy * dy;
  
  if (lengthSq < 1e-12) {
    return PointToPointDistance(pLon, pLat, l1Lon, l1Lat);
  }
  
  double t = std::max(0.0, std::min(1.0, 
    ((pLon - l1Lon) * dx + (pLat - l1Lat) * dy) / lengthSq));
  
  double projLon = l1Lon + t * dx;
  double projLat = l1Lat + t * dy;
  
  return PointToPointDistance(pLon, pLat, projLon, projLat);
}

double LayerManager::PointToPointDistance(double lon1, double lat1, double lon2, double lat2) {
  double dx = lon2 - lon1;
  double dy = lat2 - lat1;
  return std::sqrt(dx * dx + dy * dy);
}

glm::vec3 LayerManager::GeoToSphere(double lon, double lat) const {
  const float R = 6378.137f; // GLOBE_RADIUS
  double latRad = glm::radians(lat);
  double lonRad = glm::radians(lon);
  glm::vec3 p;
  p.x = static_cast<float>(std::cos(latRad) * std::cos(lonRad)) * R;
  p.y = static_cast<float>(std::cos(latRad) * std::sin(lonRad)) * R;
  p.z = static_cast<float>(std::sin(latRad)) * R;
  return p;
}

void LayerManager::BuildLayerBuffers(Layer& layer) {
  // Destroy existing buffers
  DestroyLayerBuffers(layer);
  
  std::vector<glm::vec3> pointVerts;
  std::vector<glm::vec4> pointUvs;
  std::vector<glm::vec3> lineVerts;
  std::vector<glm::vec3> fillVerts;
  
  earth::IconMap* iconMap = nullptr;
  if (iconMaps_ && layer.style.iconType == IconType::MAP) {
    auto it = iconMaps_->find(layer.style.icon.mapName);
    if (it != iconMaps_->end()) {
      iconMap = &it->second;
    }
  }

  for (const auto& f : layer.features) {
    if (!f.visible) continue;
    
    if (f.geometryType == GeometryType::Point || f.geometryType == GeometryType::MultiPoint) {
      glm::vec4 uv(0.0f, 0.0f, 1.0f, 1.0f); // Default: full texture
      if (iconMap) {
        auto it = iconMap->icons.find(layer.style.icon.name);
        if (it != iconMap->icons.end()) {
          const auto& info = it->second;
          // Texture is flipped vertically by stbi, so (0,0) is bottom-left.
          // JSON coordinates are top-left (y=0 is top).
          // Map JSON top-left to UV top-left (V=1.0).
          uv.x = static_cast<float>(info.x) / iconMap->width;
          uv.y = 1.0f - static_cast<float>(info.y) / iconMap->height; // Top V
          uv.z = static_cast<float>(info.width) / iconMap->width;
          uv.w = -static_cast<float>(info.height) / iconMap->height; // Negative height to go down
        }
      }

      for (size_t i = 0; i + 1 < f.coordinates.size(); i += 2) {
        pointVerts.push_back(GeoToSphere(f.coordinates[i], f.coordinates[i + 1]));
        pointUvs.push_back(uv);
      }
    } else if (f.geometryType == GeometryType::Line || f.geometryType == GeometryType::MultiLine) {
      for (size_t i = 0; i + 3 < f.coordinates.size(); i += 2) {
        glm::vec3 p0 = GeoToSphere(f.coordinates[i], f.coordinates[i + 1]);
        glm::vec3 p1 = GeoToSphere(f.coordinates[i + 2], f.coordinates[i + 3]);
        lineVerts.push_back(p0);
        lineVerts.push_back(p1);
      }
    } else if (f.geometryType == GeometryType::Polygon || f.geometryType == GeometryType::MultiPolygon) {
      // Draw polygon outline as lines
      for (size_t i = 0; i + 3 < f.coordinates.size(); i += 2) {
        glm::vec3 p0 = GeoToSphere(f.coordinates[i], f.coordinates[i + 1]);
        glm::vec3 p1 = GeoToSphere(f.coordinates[i + 2], f.coordinates[i + 3]);
        lineVerts.push_back(p0);
        lineVerts.push_back(p1);
      }
      // Close the polygon
      if (f.coordinates.size() >= 4) {
        glm::vec3 pLast = GeoToSphere(f.coordinates[f.coordinates.size() - 2], f.coordinates[f.coordinates.size() - 1]);
        glm::vec3 pFirst = GeoToSphere(f.coordinates[0], f.coordinates[1]);
        lineVerts.push_back(pLast);
        lineVerts.push_back(pFirst);
      }
      // TODO: Triangulate for fill
    }
  }
  
  // Create point buffer
  if (!pointVerts.empty()) {
    glGenVertexArrays(1, &layer.pointVao);
    glGenBuffers(1, &layer.pointVbo);
    glGenBuffers(1, &layer.pointUvVbo);
    
    glBindVertexArray(layer.pointVao);
    
    glBindBuffer(GL_ARRAY_BUFFER, layer.pointVbo);
    glBufferData(GL_ARRAY_BUFFER, pointVerts.size() * sizeof(glm::vec3), pointVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    
    glBindBuffer(GL_ARRAY_BUFFER, layer.pointUvVbo);
    glBufferData(GL_ARRAY_BUFFER, pointUvs.size() * sizeof(glm::vec4), pointUvs.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    
    glBindVertexArray(0);
    layer.pointCount = pointVerts.size();
  }
  
  // Create line buffer
  if (!lineVerts.empty()) {
    glGenVertexArrays(1, &layer.lineVao);
    glGenBuffers(1, &layer.lineVbo);
    glBindVertexArray(layer.lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, layer.lineVbo);
    glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(glm::vec3), lineVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    layer.lineVertexCount = lineVerts.size();
  }
  
  // Create fill buffer
  if (!fillVerts.empty()) {
    glGenVertexArrays(1, &layer.fillVao);
    glGenBuffers(1, &layer.fillVbo);
    glBindVertexArray(layer.fillVao);
    glBindBuffer(GL_ARRAY_BUFFER, layer.fillVbo);
    glBufferData(GL_ARRAY_BUFFER, fillVerts.size() * sizeof(glm::vec3), fillVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    layer.fillVertexCount = fillVerts.size();
  }
  
  // Generate Labels
  if (layer.style.showLabel && labelManager_) {
    for (const auto& f : layer.features) {
      if (!f.visible) continue;
      
      std::string text;
      // Try field first
      if (!layer.style.labelField.empty()) {
          auto it = f.properties.find(layer.style.labelField);
          if (it != f.properties.end()) {
             text = it->second.AsString();
          }
      }
      // Fallback to static text
      if (text.empty() && !layer.style.labelText.empty()) {
          text = layer.style.labelText;
      }
      
      if (text.empty()) continue;
      
      // For Points
      if (f.geometryType == GeometryType::Point && !f.coordinates.empty()) {
          double lon = f.coordinates[0];
          double lat = f.coordinates[1];
          // Use 0 altitude for now, render handles projection?
          int id = labelManager_->AddLabel(text, lat, lon, 0.0);
          if (id != -1) {
             layer.labelIds.push_back(id);
          }
      }
      // TODO: Support MultiPoint, Line, Polygon labels
    }
  }
  
  layer.dirty = false;
}

void LayerManager::DestroyLayerBuffers(Layer& layer) {
  if (layer.pointVbo) { glDeleteBuffers(1, &layer.pointVbo); layer.pointVbo = 0; }
  if (layer.pointUvVbo) { glDeleteBuffers(1, &layer.pointUvVbo); layer.pointUvVbo = 0; }
  if (layer.pointVao) { glDeleteVertexArrays(1, &layer.pointVao); layer.pointVao = 0; }
  if (layer.lineVbo) { glDeleteBuffers(1, &layer.lineVbo); layer.lineVbo = 0; }
  if (layer.lineVao) { glDeleteVertexArrays(1, &layer.lineVao); layer.lineVao = 0; }
  if (layer.fillVbo) { glDeleteBuffers(1, &layer.fillVbo); layer.fillVbo = 0; }
  if (layer.fillVao) { glDeleteVertexArrays(1, &layer.fillVao); layer.fillVao = 0; }
  layer.pointCount = 0;
  layer.lineVertexCount = 0;
  layer.fillVertexCount = 0;
  
  // Clear labels
  if (labelManager_) {
    for (int id : layer.labelIds) {
      labelManager_->RemoveLabel(id);
    }
    layer.labelIds.clear();
  }
}

std::vector<Layer*> LayerManager::GetLayersByDrawOrder() {
  std::vector<Layer*> sorted;
  sorted.reserve(layers_.size());
  for (auto& l : layers_) {
    sorted.push_back(l.get());
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const Layer* a, const Layer* b) { return a->drawOrder < b->drawOrder; });
  return sorted;
}

void LayerManager::Render(const glm::mat4& mvp, int currentZoom, float time, float flashPeriod) {
  InitShaders();
  
  glUseProgram(program_);
  glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(mvp));
  glUniform1f(timeLoc_, time);
  glUniform1f(flashPeriodLoc_, flashPeriod);
  
  auto sortedLayers = GetLayersByDrawOrder();
  
  for (Layer* layer : sortedLayers) {
    if (!layer->visible) continue;
    if (currentZoom < layer->style.minZoom || currentZoom > layer->style.maxZoom) continue;
    
    // Rebuild buffers if dirty
    if (layer->dirty) {
      BuildLayerBuffers(*layer);
    }
    
    float alpha = layer->opacity * layer->style.opacity;
    
    // Draw fills
    glUniform1i(flashEnabledLoc_, 0); // Disable flash for fills/lines by default
    if (layer->fillVertexCount > 0) {
      glm::vec4 color = layer->style.fillColor;
      color.a *= alpha;
      glUniform4fv(colorLoc_, 1, glm::value_ptr(color));
      glBindVertexArray(layer->fillVao);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(layer->fillVertexCount));
    }
    
    // Draw lines
    if (layer->lineVertexCount > 0) {
      glm::vec4 color = layer->style.borderColor;
      color.a *= alpha;
      glUniform4fv(colorLoc_, 1, glm::value_ptr(color));
      glLineWidth(layer->style.strokeWidth);
      glBindVertexArray(layer->lineVao);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(layer->lineVertexCount));
    }
    
    // Draw points
    if (layer->pointCount > 0) {
      glm::vec4 color = layer->style.pointColor;
      color.a *= alpha;
      glUniform4fv(colorLoc_, 1, glm::value_ptr(color));
      glUniform1f(pointSizeLoc_, layer->style.pointSize);
      
      // Set flash enabled for points/icons
      glUniform1i(flashEnabledLoc_, layer->style.flashIcon ? 1 : 0);
      
      bool useTex = false;
      if (iconMaps_ && layer->style.iconType == IconType::MAP) {
        auto it = iconMaps_->find(layer->style.icon.mapName);
        if (it != iconMaps_->end() && it->second.loaded) {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, it->second.textureId);
          glUniform1i(texLoc_, 0);
          useTex = true;
        }
      }
      glUniform1i(useTexLoc_, useTex ? 1 : 0);

      glBindVertexArray(layer->pointVao);
      glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(layer->pointCount));
      
      if (useTex) {
        glBindTexture(GL_TEXTURE_2D, 0);
      }
    }
  }
  
  glBindVertexArray(0);
}

void LayerManager::MarkAllDirty() {
  for (auto& layer : layers_) {
    layer->dirty = true;
  }
}

void LayerManager::RebuildIndex() {
  idToIndex_.clear();
  for (size_t i = 0; i < layers_.size(); ++i) {
    idToIndex_[layers_[i]->id] = i;
  }
}
