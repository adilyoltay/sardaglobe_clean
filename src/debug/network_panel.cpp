#include "network_panel.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace globe {

NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel instance;
    return instance;
}

void NetworkPanel::RecordStart(const TileKey& key, RequestType type, const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        appStartTime_ = glfwGetTime() * 1000.0;
        initialized_ = true;
    }
    
    NetworkRequest req;
    req.key = key;
    req.type = type;
    req.url = url;
    req.startTimeMs = glfwGetTime() * 1000.0 - appStartTime_;
    req.complete = false;
    
    requests_.push_back(req);
    
    // Trim old requests
    while (requests_.size() > MAX_REQUESTS) {
        requests_.pop_front();
    }
}

void NetworkPanel::RecordComplete(const TileKey& key, RequestType type, bool success,
                                   long httpStatus, size_t bytes, double durationMs,
                                   bool cacheHit, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the pending request
    NetworkRequest* req = FindPending(key, type);
    if (req) {
        req->success = success;
        req->httpStatus = httpStatus;
        req->bytesReceived = bytes;
        req->durationMs = durationMs;
        req->cacheHit = cacheHit;
        req->error = error;
        req->complete = true;
    }
}

NetworkRequest* NetworkPanel::FindPending(const TileKey& key, RequestType type) {
    // Search from end (most recent) to find pending request
    for (auto it = requests_.rbegin(); it != requests_.rend(); ++it) {
        if (!it->complete && it->key == key && it->type == type) {
            return &(*it);
        }
    }
    return nullptr;
}

void NetworkPanel::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
}

NetworkPanel::Stats NetworkPanel::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    double totalDuration = 0.0;
    int completedCount = 0;
    
    for (const auto& req : requests_) {
        if (!req.complete) continue;
        ++stats.totalRequests;
        if (req.success) ++stats.successCount;
        else ++stats.failCount;
        if (req.cacheHit) ++stats.cacheHits;
        stats.totalBytes += req.bytesReceived;
        totalDuration += req.durationMs;
        ++completedCount;
    }
    
    if (completedCount > 0) {
        stats.avgDurationMs = totalDuration / completedCount;
    }
    return stats;
}

void NetworkPanel::Render() {
    if (!panelOpen) return;
    
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Network Panel", &panelOpen)) {
        ImGui::End();
        return;
    }
    
    // Toolbar
    if (ImGui::Button("Clear")) {
        Clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Raster", &showRaster);
    ImGui::SameLine();
    ImGui::Checkbox("DEM", &showDem);
    ImGui::SameLine();
    ImGui::Checkbox("Failed Only", &showOnlyFailed);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoscroll);
    
    // Stats bar
    Stats stats = GetStats();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        "Total: %d | OK: %d | Fail: %d | Cache: %d | Avg: %.1fms | Size: %.1f KB",
        stats.totalRequests, stats.successCount, stats.failCount, stats.cacheHits,
        stats.avgDurationMs, stats.totalBytes / 1024.0);
    
    ImGui::Separator();
    
    // Table
    const ImGuiTableFlags tableFlags = 
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter;
    
    if (ImGui::BeginTable("NetworkTable", 7, tableFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Tile", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Cache", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& req : requests_) {
            // Filtering
            if (req.type == RequestType::RasterTile && !showRaster) continue;
            if (req.type == RequestType::DemMesh && !showDem) continue;
            if (showOnlyFailed && (req.success || !req.complete)) continue;
            
            ImGui::TableNextRow();
            
            // Type column
            ImGui::TableNextColumn();
            if (req.type == RequestType::RasterTile) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Raster");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "DEM");
            }
            
            // Status column
            ImGui::TableNextColumn();
            if (!req.complete) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "...");
            } else if (req.success) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%ld", req.httpStatus);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%ld", req.httpStatus);
            }
            
            // Tile column
            ImGui::TableNextColumn();
            ImGui::Text("z%d/%d/%d", req.key.level, req.key.x, req.key.y);
            
            // Time column
            ImGui::TableNextColumn();
            if (req.complete) {
                if (req.durationMs < 100) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.0fms", req.durationMs);
                } else if (req.durationMs < 500) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%.0fms", req.durationMs);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.0fms", req.durationMs);
                }
            } else {
                ImGui::Text("-");
            }
            
            // Size column
            ImGui::TableNextColumn();
            if (req.complete && req.bytesReceived > 0) {
                if (req.bytesReceived < 1024) {
                    ImGui::Text("%zuB", req.bytesReceived);
                } else {
                    ImGui::Text("%.1fKB", req.bytesReceived / 1024.0);
                }
            } else {
                ImGui::Text("-");
            }
            
            // Cache column
            ImGui::TableNextColumn();
            if (req.cacheHit) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "HIT");
            } else if (req.complete) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // URL column
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(req.url.c_str());
        }
        
        // Auto-scroll
        if (autoscroll && !requests_.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndTable();
    }
    
    ImGui::End();
}

} // namespace globe
