#pragma once

#include "layer.h"
#include "icon_map.h"
#include "label_manager.h" // Include header
#include <vector>
#include <memory>
// ...

class LayerManager {
public:
  LayerManager();
  ~LayerManager();

  void SetIconMaps(std::unordered_map<std::string, earth::IconMap>* maps) { iconMaps_ = maps; }
  void SetLabelManager(LabelManager* lm) { labelManager_ = lm; }

  // Layer CRUD
  std::string AddLayer(const std::string& id, const std::string& name, LayerType type);
  bool DeleteLayer(const std::string& id);
  void DeleteAllLayers();
  Layer* GetLayer(const std::string& id);
  Layer* GetLayerByIndex(size_t index);
  size_t GetLayerCount() const;
  std::string GetNewLayerId();

  // Layer visibility/opacity
  void SetLayerVisible(const std::string& id, bool visible);
  bool GetLayerVisible(const std::string& id) const;
  void SetLayerOpacity(const std::string& id, float opacity);
  float GetLayerOpacity(const std::string& id) const;

  // Layer style
  void SetLayerStyle(const std::string& id, const LayerStyle& style);
  LayerStyle GetLayerStyle(const std::string& id) const;
  void StyleChanged(const std::string& id);

  // Layer data
  void SetLayerData(const std::string& id, std::vector<Feature>&& features);
  void AddFeature(const std::string& layerId, Feature&& feature);
  void RemoveFeature(const std::string& layerId, const std::string& featureId);
  void UpdateFeature(const std::string& layerId, const std::string& featureId, Feature&& feature);
  Feature* GetFeature(const std::string& layerId, const std::string& featureId);

  // Selection
  void SetSelectedList(const std::string& layerId, const std::vector<std::string>& ids);
  std::vector<std::string> GetSelectedList(const std::string& layerId) const;
  void ClearSelection(const std::string& layerId);

  // Draw order
  void SetDrawOrder(const std::string& id, int order);
  void BringToFront(const std::string& id);
  void SendToBack(const std::string& id);

  // Query
  std::vector<Feature*> QueryByPoint(double lon, double lat, double toleranceDeg);
  std::vector<Feature*> QueryByBBox(double minLon, double minLat, double maxLon, double maxLat);
  Feature* GetNearestFeature(double lon, double lat, double maxDistanceDeg);
  
  // Hit testing
  static bool PointInPolygon(double testLon, double testLat, const std::vector<double>& coords);
  static double PointToLineDistance(double pLon, double pLat, double l1Lon, double l1Lat, double l2Lon, double l2Lat);
  static double PointToPointDistance(double lon1, double lat1, double lon2, double lat2);

  // Render
  void BuildLayerBuffers(Layer& layer);
  void DestroyLayerBuffers(Layer& layer);
  void Render(const glm::mat4& mvp, int currentZoom, float time, float flashPeriod);
  void MarkAllDirty();
  
  // Get layers sorted by draw order for rendering
  std::vector<Layer*> GetLayersByDrawOrder();

private:
  std::vector<std::unique_ptr<Layer>> layers_;
  std::unordered_map<std::string, size_t> idToIndex_;
  std::unordered_map<std::string, earth::IconMap>* iconMaps_ = nullptr;
  LabelManager* labelManager_ = nullptr;
  uint64_t nextId_ = 1;
  
  // Shader program for layer rendering
  uint32_t program_ = 0;
  int32_t mvpLoc_ = -1;
  int32_t colorLoc_ = -1;
  int32_t pointSizeLoc_ = -1;
  int32_t texLoc_ = -1;
  int32_t useTexLoc_ = -1;
  int32_t timeLoc_ = -1;
  int32_t flashPeriodLoc_ = -1;
  int32_t flashEnabledLoc_ = -1;
  bool shadersInitialized_ = false;
  
  void InitShaders();
  void RebuildIndex();
  glm::vec3 GeoToSphere(double lon, double lat) const;
};
