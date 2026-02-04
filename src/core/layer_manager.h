#pragma once

#include "layer.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

namespace globe {

// Layer manager - handles layer CRUD and queries
class LayerManager {
public:
    LayerManager() = default;
    ~LayerManager() = default;
    
    // Layer CRUD
    std::string AddLayer(const std::string& name, LayerType type);
    std::string AddRasterLayer(const std::string& name, const std::string& tileUrlTemplate);
    bool DeleteLayer(const std::string& id);
    void DeleteAllLayers();
    Layer* GetLayer(const std::string& id);
    const Layer* GetLayer(const std::string& id) const;
    size_t GetLayerCount() const { return layers_.size(); }
    
    // Layer visibility/opacity
    void SetLayerVisible(const std::string& id, bool visible);
    bool GetLayerVisible(const std::string& id) const;
    void SetLayerOpacity(const std::string& id, float opacity);
    float GetLayerOpacity(const std::string& id) const;
    
    // Layer style
    void SetLayerStyle(const std::string& id, const LayerStyle& style);
    LayerStyle GetLayerStyle(const std::string& id) const;
    
    // Feature management
    void AddFeature(const std::string& layerId, Feature feature);
    void RemoveFeature(const std::string& layerId, const std::string& featureId);
    Feature* GetFeature(const std::string& layerId, const std::string& featureId);
    void ClearFeatures(const std::string& layerId);
    
    // Selection
    void SetSelectedIds(const std::string& layerId, const std::vector<std::string>& ids);
    std::vector<std::string> GetSelectedIds(const std::string& layerId) const;
    void ClearSelection(const std::string& layerId);
    
    // Query
    std::vector<Feature*> QueryByPoint(double lon, double lat, double toleranceDeg);
    std::vector<Feature*> QueryByBBox(double minLon, double minLat, double maxLon, double maxLat);
    
    // Draw order
    void SetZIndex(const std::string& id, int zIndex);
    std::vector<Layer*> GetLayersByZIndex();
    
    // Iteration
    template<typename Func>
    void ForEachLayer(Func&& func) {
        for (auto& [id, layer] : layers_) {
            func(*layer);
        }
    }
    
    // Mark layer dirty (needs GPU buffer rebuild)
    void MarkDirty(const std::string& id);
    void MarkAllDirty();

private:
    std::unordered_map<std::string, std::unique_ptr<Layer>> layers_;
    uint64_t nextId_ = 1;
    
    std::string GenerateId();
};

} // namespace globe
