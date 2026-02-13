// NetworkPanel stub for testing
// Provides minimal implementation without ImGui/GLFW dependencies

#include "../../src/debug/network_panel.h"
#include <iostream>

namespace globe {

std::string NetworkRequest::GetDisplayId() const {
    if (!id.empty()) return id;
    return "z" + std::to_string(key.level) + "/" + std::to_string(key.x) + "/" + std::to_string(key.y);
}

NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel instance;
    return instance;
}

void NetworkPanel::RecordStart(const TileKey& key, RequestType type, const std::string& url) {
    (void)key; (void)type; (void)url;
    // Stub: no-op for tests
}

void NetworkPanel::RecordStart(const std::string& id, RequestType type, const std::string& url) {
    (void)id; (void)type; (void)url;
    // Stub: no-op for tests
}

void NetworkPanel::RecordComplete(const TileKey& key, RequestType type, bool success,
                                  long httpStatus, size_t bytes, double durationMs,
                                  bool cacheHit, const std::string& error) {
    (void)key; (void)type; (void)success; (void)httpStatus; 
    (void)bytes; (void)durationMs; (void)cacheHit; (void)error;
    // Stub: no-op for tests
}

void NetworkPanel::RecordComplete(const std::string& id, RequestType type, bool success,
                                  long httpStatus, size_t bytes, double durationMs,
                                  bool cacheHit, const std::string& error) {
    (void)id; (void)type; (void)success; (void)httpStatus; 
    (void)bytes; (void)durationMs; (void)cacheHit; (void)error;
    // Stub: no-op for tests
}

void NetworkPanel::Render() {
    // Stub: no-op for tests
}

void NetworkPanel::Clear() {
    // Stub: no-op for tests
}

NetworkPanel::Stats NetworkPanel::GetStats() const {
    return Stats{};
}

NetworkRequest* NetworkPanel::FindPending(const TileKey& key, RequestType type) {
    (void)key; (void)type;
    return nullptr;
}

NetworkRequest* NetworkPanel::FindPending(const std::string& id, RequestType type) {
    (void)id; (void)type;
    return nullptr;
}

} // namespace globe
