#include "tile_manager.h"
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

// ============================================================================
// TILE MANAGER IMPLEMENTATION
// ============================================================================

void TileManager::CreateTileIfNeeded(std::unordered_map<std::string, Tile>& tiles,
                                      const TileKey& key,
                                      GLuint loadingTexture) {
    std::string keyStr = key.ToString();
    auto it = tiles.find(keyStr);
    if (it == tiles.end()) {
        Tile tile(key.level, key.x, key.y);
        tile.texture = loadingTexture;
        tile.ownsTexture = false;
        tile.fade = 0.0f;
        tiles.emplace(keyStr, std::move(tile));
    }
}

float TileManager::ComputeDownloadPriority(const Tile& tile,
                                            const glm::vec3* cameraPos,
                                            const glm::mat4* mvp,
                                            int currentZoom,
                                            bool isLeaf) {
    float score = 0.0f;
    if (!cameraPos) return score;
    
    // 1. Distance scoring (closer = higher priority) - max 40 points
    float distance = glm::length(tile.center - *cameraPos);
    constexpr float GLOBE_RADIUS = 6378.137f;
    float maxDist = 3.0f * GLOBE_RADIUS;
    score += std::max(0.0f, 1.0f - distance / maxDist) * 40.0f;
    
    // 2. Viewport Overlap scoring (center of screen = higher priority) - max 30 points
    if (mvp) {
        glm::vec4 clipPos = (*mvp) * glm::vec4(tile.center, 1.0f);
        if (clipPos.w > 0.001f) {
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
            float distFromCenter = std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
            score += std::max(0.0f, 1.0f - distFromCenter) * 30.0f;
        }
    }
    
    // 3. Level proximity - max 20 points
    int levelDiff = std::abs(tile.z - currentZoom);
    score += std::max(0.0f, 5.0f - static_cast<float>(levelDiff)) * 4.0f;
    
    // 4. Leaf tile bonus - max 10 points
    if (isLeaf) score += 10.0f;
    
    return score;
}

void TileManager::PinTile(const std::string& key) {
    std::lock_guard<std::mutex> lock(pinMutex_);
    pinnedTiles_.insert(key);
}

void TileManager::UnpinTile(const std::string& key) {
    std::lock_guard<std::mutex> lock(pinMutex_);
    pinnedTiles_.erase(key);
}

bool TileManager::IsTilePinned(const std::string& key) const {
    std::lock_guard<std::mutex> lock(pinMutex_);
    return pinnedTiles_.count(key) > 0;
}

TileManager::Stats TileManager::GetStats(const std::unordered_map<std::string, Tile>& tiles) const {
    Stats stats;
    stats.totalTiles = tiles.size();
    
    for (const auto& kv : tiles) {
        const Tile& tile = kv.second;
        
        switch (tile.loadState) {
            case TileLoadState::READY:
                stats.readyTiles++;
                break;
            case TileLoadState::FETCHING:
            case TileLoadState::DECODING:
            case TileLoadState::UPLOADING:
            case TileLoadState::SCHEDULED:
                stats.loadingTiles++;
                break;
            case TileLoadState::FAILED:
                stats.failedTiles++;
                break;
            default:
                break;
        }
        
        stats.cachedBytes += tile.estimatedBytes;
        
        if (IsTilePinned(kv.first)) {
            stats.pinnedTiles++;
        }
    }
    
    return stats;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

std::unordered_set<std::string> ComputeRequiredAncestors(
    const std::vector<std::pair<TileKey, std::string>>& orderedRequired) {
    
    std::unordered_set<std::string> ancestors;
    for (const auto& item : orderedRequired) {
        TileKey k = item.first;
        while (k.level > 0) {
            k = k.GetParent();
            ancestors.insert(k.ToString());
        }
    }
    return ancestors;
}

// ============================================================================
// PREFETCH SYSTEM
// ============================================================================

std::vector<TileKey> TileManager::ComputePrefetchTiles(
    const std::vector<std::string>& visibleLeaves,
    const glm::vec3& cameraVelocity,
    const PrefetchConfig& config) {
    
    if (!config.enabled || visibleLeaves.empty()) {
        return {};
    }
    
    std::vector<TileKey> prefetchTiles;
    std::unordered_set<std::string> seen;
    
    // Add visible leaves to seen set
    for (const auto& key : visibleLeaves) {
        seen.insert(key);
    }
    
    float cameraSpeed = glm::length(cameraVelocity);
    bool hasMovement = cameraSpeed > config.minCameraSpeed;
    
    // For each visible leaf, add neighbor tiles
    for (const auto& keyStr : visibleLeaves) {
        TileKey key = TileKey::FromString(keyStr);
        
        // Get immediate neighbors (ring 1)
        std::array<TileKey, 4> neighbors = {
            key.GetNeighborNorth(),
            key.GetNeighborSouth(),
            key.GetNeighborEast(),
            key.GetNeighborWest()
        };
        
        for (const auto& neighbor : neighbors) {
            std::string neighborStr = neighbor.ToString();
            if (seen.find(neighborStr) == seen.end()) {
                seen.insert(neighborStr);
                prefetchTiles.push_back(neighbor);
                
                if (prefetchTiles.size() >= static_cast<size_t>(config.maxPrefetchPerFrame)) {
                    return prefetchTiles;
                }
            }
        }
        
        // If camera is moving, prioritize tiles in movement direction
        if (hasMovement && config.neighborRings > 1) {
            // Add diagonal neighbors for ring 2
            TileKey ne = key.GetNeighborNorth().GetNeighborEast();
            TileKey nw = key.GetNeighborNorth().GetNeighborWest();
            TileKey se = key.GetNeighborSouth().GetNeighborEast();
            TileKey sw = key.GetNeighborSouth().GetNeighborWest();
            
            std::array<TileKey, 4> diagonals = {ne, nw, se, sw};
            for (const auto& diag : diagonals) {
                std::string diagStr = diag.ToString();
                if (seen.find(diagStr) == seen.end()) {
                    seen.insert(diagStr);
                    prefetchTiles.push_back(diag);
                    
                    if (prefetchTiles.size() >= static_cast<size_t>(config.maxPrefetchPerFrame)) {
                        return prefetchTiles;
                    }
                }
            }
        }
    }
    
    return prefetchTiles;
}

size_t EstimateTileBytes(const Tile& tile) {
    size_t bytes = sizeof(Tile);
    
    // Texture estimate: 256x256 RGBA = 256KB typical
    if (tile.ownsTexture && tile.texture != 0) {
        bytes += 256 * 256 * 4;
    }
    
    // Mesh estimate
    if (tile.mesh.vao != 0) {
        bytes += tile.mesh.indexCount * sizeof(unsigned int);
        bytes += tile.mesh.indexCount * 3 * sizeof(float) * 2; // positions + normals estimate
    }
    
    // Decoded data (temporary)
    bytes += tile.decodedData.size();
    
    return bytes;
}
