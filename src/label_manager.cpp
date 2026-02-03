#include "label_manager.h"
#include <imgui.h>
#include <iostream>
#include <cmath>

static glm::vec3 LabelLatLonToSphere(double latDeg, double lonDeg, double altMeters) {
    constexpr double GLOBE_RADIUS = 6378137.0 * 0.001; // Normalized
    constexpr double GLOBE_RADIUS_K = 0.001;
    double r = GLOBE_RADIUS + altMeters * GLOBE_RADIUS_K;
    double lat = glm::radians(latDeg);
    double lon = glm::radians(lonDeg);
    return glm::vec3(
        std::cos(lat) * std::cos(lon) * r,
        std::cos(lat) * std::sin(lon) * r,
        std::sin(lat) * r
    );
}

LabelManager::LabelManager() {}
LabelManager::~LabelManager() { Shutdown(); }

void LabelManager::Shutdown() {
    // Cleanup if needed
}

void LabelManager::Init() {
    // No-op for ImGui mode
}

int LabelManager::AddLabel(const std::string& text, double lat, double lon, double alt) {
    LabelInfo info;
    info.text = text;
    
    // Convert Geo to ECEF/Sphere (Simplified Sphere for now, matching LayerManager)
    double latRad = glm::radians(lat);
    double lonRad = glm::radians(lon);
    // Assuming Radius = GLOBE_RADIUS from globe_engine.h, but we don't include it.
    // Let's use 6378.137 as base or assume unit sphere * Scale?
    // LayerManager::GeoToSphere returns unit sphere.
    // But Render uses MVP which usually includes Scale.
    // Wait, LayerManager geometry is on Unit Sphere. Model Matrix scales it up?
    // Let's check GlobeEngine::Draw.
    
    // For now, use unit sphere coordinates, Render will likely position them.
    // But wait, Render receives MVP. 
    // If geometry is unit sphere, MVP handles scale.
    // So AddLabel should store Unit Sphere pos.
    
    // Scale by GLOBE_RADIUS (6378.137) to match engine units
    const float R = 6378.137f;
    
    info.worldPos.x = static_cast<float>(std::cos(latRad) * std::cos(lonRad)) * R;
    info.worldPos.y = static_cast<float>(std::cos(latRad) * std::sin(lonRad)) * R;
    info.worldPos.z = static_cast<float>(std::sin(latRad)) * R;
    
    // Rasterize
    if (!RasterizeTextToAtlas(text, info)) {
        return -1;
    }
    
    int id = nextLabelId_++;
    labels_[id] = info;
    return id;
}

void LabelManager::RemoveLabel(int id) {
    labels_.erase(id);
}

void LabelManager::ClearLabels() {
    labels_.clear();
    // Reset Atlas? Maybe not needed for simple implementation.
    // Ideally we should manage atlas space (allocate/free).
    // Current RasterizeTextToAtlas acts as a linear allocator.
    // Resetting currentX/Y is risky if we don't clear texture.
    // But simpler: just clear map. Atlas fills up until restart.
}

bool LabelManager::RasterizeTextToAtlas(const std::string& text, LabelInfo& info) {
    // Placeholder for Phase 8 optimization
    return true; 
}

void LabelManager::Render(const glm::mat4& mvp, const glm::vec3& cameraPos) {
    if (ImGui::GetCurrentContext() == nullptr) return;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float screenWidth = displaySize.x;
    float screenHeight = displaySize.y;
    
    // Horizon culling setup
    constexpr float GLOBE_RADIUS = 6378.137f; // Normalized radius
    float camDist = glm::length(cameraPos);
    float horizonAngle = 0.0f;
    bool checkHorizon = (camDist > GLOBE_RADIUS);
    if (checkHorizon) {
        horizonAngle = std::acos(GLOBE_RADIUS / camDist);
        if (std::isnan(horizonAngle)) horizonAngle = 0.0f;
    }
    glm::vec3 camDir = glm::normalize(cameraPos);
    
    for (auto& kv : labels_) {
        const LabelInfo& l = kv.second;
        
        // Horizon Culling check (occlusion)
        if (checkHorizon) {
            glm::vec3 labelDir = glm::normalize(l.worldPos);
            float labelAngle = std::acos(glm::clamp(glm::dot(camDir, labelDir), -1.0f, 1.0f));
            // Add slight tolerance for label height
            if (labelAngle > horizonAngle + 0.05f) continue;
        }
        
        // Project world to screen
        glm::vec4 clip = mvp * glm::vec4(l.worldPos, 1.0f);
        if (clip.w <= 0.0f) continue; // Behind camera
        
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z > 1.0f || ndc.z < -1.0f) continue; // Far/Near clipped
        
        float x = (ndc.x * 0.5f + 0.5f) * screenWidth;
        float y = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenHeight;
        
        // Draw text with shadow/outline for readability
        drawList->AddText(ImVec2(x+1, y+1), ImGui::GetColorU32(ImVec4(0,0,0,1)), l.text.c_str());
        drawList->AddText(ImVec2(x, y), ImGui::GetColorU32(ImVec4(l.color.r, l.color.g, l.color.b, l.color.a)), l.text.c_str());
    }
}

void LabelManager::CreateLabelShader() {
    // Unused
}
