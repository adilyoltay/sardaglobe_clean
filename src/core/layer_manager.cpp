#include "layer_manager.h"
#include <algorithm>
#include <sstream>

namespace globe {

std::string LayerManager::GenerateId() {
    std::stringstream ss;
    ss << "layer_" << nextId_++;
    return ss.str();
}

std::string LayerManager::AddLayer(const std::string& name, LayerType type) {
    auto layer = std::make_unique<Layer>();
    layer->id = GenerateId();
    layer->name = name;
    layer->type = type;
    
    std::string id = layer->id;
    layers_[id] = std::move(layer);
    return id;
}

std::string LayerManager::AddRasterLayer(const std::string& name, const std::string& tileUrlTemplate) {
    auto layer = std::make_unique<Layer>();
    layer->id = GenerateId();
    layer->name = name;
    layer->type = LayerType::Raster;
    layer->tileUrlTemplate = tileUrlTemplate;
    
    std::string id = layer->id;
    layers_[id] = std::move(layer);
    return id;
}

bool LayerManager::DeleteLayer(const std::string& id) {
    return layers_.erase(id) > 0;
}

void LayerManager::DeleteAllLayers() {
    layers_.clear();
}

Layer* LayerManager::GetLayer(const std::string& id) {
    auto it = layers_.find(id);
    return it != layers_.end() ? it->second.get() : nullptr;
}

const Layer* LayerManager::GetLayer(const std::string& id) const {
    auto it = layers_.find(id);
    return it != layers_.end() ? it->second.get() : nullptr;
}

void LayerManager::SetLayerVisible(const std::string& id, bool visible) {
    if (Layer* layer = GetLayer(id)) {
        layer->visible = visible;
    }
}

bool LayerManager::GetLayerVisible(const std::string& id) const {
    if (const Layer* layer = GetLayer(id)) {
        return layer->visible;
    }
    return false;
}

void LayerManager::SetLayerOpacity(const std::string& id, float opacity) {
    if (Layer* layer = GetLayer(id)) {
        layer->opacity = std::clamp(opacity, 0.0f, 1.0f);
    }
}

float LayerManager::GetLayerOpacity(const std::string& id) const {
    if (const Layer* layer = GetLayer(id)) {
        return layer->opacity;
    }
    return 1.0f;
}

void LayerManager::SetLayerStyle(const std::string& id, const LayerStyle& style) {
    if (Layer* layer = GetLayer(id)) {
        layer->style = style;
        layer->dirty = true;
    }
}

LayerStyle LayerManager::GetLayerStyle(const std::string& id) const {
    if (const Layer* layer = GetLayer(id)) {
        return layer->style;
    }
    return LayerStyle{};
}

void LayerManager::AddFeature(const std::string& layerId, Feature feature) {
    if (Layer* layer = GetLayer(layerId)) {
        feature.ComputeBounds();
        layer->features.push_back(std::move(feature));
        layer->ComputeBounds();
        layer->dirty = true;
    }
}

void LayerManager::RemoveFeature(const std::string& layerId, const std::string& featureId) {
    if (Layer* layer = GetLayer(layerId)) {
        auto& features = layer->features;
        features.erase(
            std::remove_if(features.begin(), features.end(),
                [&featureId](const Feature& f) { return f.id == featureId; }),
            features.end()
        );
        layer->ComputeBounds();
        layer->dirty = true;
    }
}

Feature* LayerManager::GetFeature(const std::string& layerId, const std::string& featureId) {
    if (Layer* layer = GetLayer(layerId)) {
        for (auto& feature : layer->features) {
            if (feature.id == featureId) {
                return &feature;
            }
        }
    }
    return nullptr;
}

void LayerManager::ClearFeatures(const std::string& layerId) {
    if (Layer* layer = GetLayer(layerId)) {
        layer->features.clear();
        layer->dirty = true;
    }
}

void LayerManager::SetSelectedIds(const std::string& layerId, const std::vector<std::string>& ids) {
    if (Layer* layer = GetLayer(layerId)) {
        layer->selectedIds = ids;
        // Update feature selection state
        for (auto& feature : layer->features) {
            feature.selected = std::find(ids.begin(), ids.end(), feature.id) != ids.end();
        }
        layer->dirty = true;
    }
}

std::vector<std::string> LayerManager::GetSelectedIds(const std::string& layerId) const {
    if (const Layer* layer = GetLayer(layerId)) {
        return layer->selectedIds;
    }
    return {};
}

void LayerManager::ClearSelection(const std::string& layerId) {
    SetSelectedIds(layerId, {});
}

std::vector<Feature*> LayerManager::QueryByPoint(double lon, double lat, double toleranceDeg) {
    std::vector<Feature*> results;
    
    for (auto& [id, layer] : layers_) {
        if (!layer->visible) continue;
        
        for (auto& feature : layer->features) {
            if (!feature.visible) continue;
            
            // Quick bbox check
            if (lon < feature.minLon - toleranceDeg || lon > feature.maxLon + toleranceDeg ||
                lat < feature.minLat - toleranceDeg || lat > feature.maxLat + toleranceDeg) {
                continue;
            }
            
            // Point geometry: distance check
            if (feature.geometryType == GeometryType::Point && feature.coordinates.size() >= 2) {
                double dx = feature.coordinates[0] - lon;
                double dy = feature.coordinates[1] - lat;
                if (dx * dx + dy * dy <= toleranceDeg * toleranceDeg) {
                    results.push_back(&feature);
                }
            }
            // TODO: Line/Polygon hit testing
        }
    }
    
    return results;
}

std::vector<Feature*> LayerManager::QueryByBBox(double minLon, double minLat, double maxLon, double maxLat) {
    std::vector<Feature*> results;
    
    for (auto& [id, layer] : layers_) {
        if (!layer->visible) continue;
        
        for (auto& feature : layer->features) {
            if (!feature.visible) continue;
            
            // BBox intersection check
            if (feature.maxLon >= minLon && feature.minLon <= maxLon &&
                feature.maxLat >= minLat && feature.minLat <= maxLat) {
                results.push_back(&feature);
            }
        }
    }
    
    return results;
}

void LayerManager::SetZIndex(const std::string& id, int zIndex) {
    if (Layer* layer = GetLayer(id)) {
        layer->zIndex = zIndex;
    }
}

std::vector<Layer*> LayerManager::GetLayersByZIndex() {
    std::vector<Layer*> result;
    result.reserve(layers_.size());
    
    for (auto& [id, layer] : layers_) {
        result.push_back(layer.get());
    }
    
    std::sort(result.begin(), result.end(),
        [](const Layer* a, const Layer* b) { return a->zIndex < b->zIndex; });
    
    return result;
}

void LayerManager::MarkDirty(const std::string& id) {
    if (Layer* layer = GetLayer(id)) {
        layer->dirty = true;
    }
}

void LayerManager::MarkAllDirty() {
    for (auto& [id, layer] : layers_) {
        layer->dirty = true;
    }
}

} // namespace globe
