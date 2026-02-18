// P1-6: Predictive view prefetcher implementation

#include "predictive_prefetcher.h"
#include "../math/tile_math.h"
#include <algorithm>
#include <cmath>

namespace globe {

PredictivePrefetcher::PredictivePrefetcher(const PredictivePrefetchSettings& settings)
    : settings_(settings) {
}

std::vector<TileKey> PredictivePrefetcher::GenerateCandidates(
    const glm::vec3& cameraPos,
    const glm::vec3& cameraVelocity,
    const glm::vec3& cameraAcceleration,
    const std::vector<TileKey>& currentLeaves,
    const std::unordered_set<TileKey>& alreadyRequired,
    const std::unordered_set<TileKey>& alreadyPrefetched,
    const Frustum& frustum,
    int minZoom,
    int maxZoom
) {
    std::vector<TileKey> candidates;
    
    if (!settings_.enabled) {
        return candidates;
    }
    
    float speedKmPerSec = glm::length(cameraVelocity);
    if (!std::isfinite(speedKmPerSec) || speedKmPerSec < settings_.velocityThresholdKmPerSec) {
        return candidates;  // Ignore tiny/noisy velocity
    }
    
    glm::vec3 velocityDir = glm::normalize(cameraVelocity);
    
    // Calculate lookahead time based on speed (and optionally acceleration)
    float speedContribution = speedKmPerSec * settings_.lookaheadSpeedScale;
    float predictionSeconds = std::clamp(
        settings_.lookaheadMinSec + speedContribution,
        settings_.lookaheadMinSec,
        settings_.lookaheadMaxSec
    );
    
    // P1-6: Acceleration can increase lookahead slightly
    float accelMagnitude = glm::length(cameraAcceleration);
    if (std::isfinite(accelMagnitude) && accelMagnitude > 0.001f) {
        // If accelerating in velocity direction, increase lookahead
        glm::vec3 accelDir = glm::normalize(cameraAcceleration);
        float accelAlignment = glm::dot(velocityDir, accelDir);
        if (accelAlignment > 0.0f) {
            predictionSeconds = std::min(predictionSeconds * 1.2f, settings_.lookaheadMaxSec);
        }
    }
    
    glm::vec3 predictedCameraPos = cameraPos + cameraVelocity * predictionSeconds;
    
    // Build seen set
    std::unordered_set<TileKey> seen;
    seen.reserve(alreadyRequired.size() + alreadyPrefetched.size() + 32);
    for (const auto& key : alreadyRequired) {
        seen.insert(key);
    }
    for (const auto& key : alreadyPrefetched) {
        seen.insert(key);
    }
    
    // Clean up expired cooldowns
    CleanupCooldown();
    
    int added = 0;
    
    for (const TileKey& leaf : currentLeaves) {
        if (added >= settings_.maxCandidates) break;
        
        // Expand in motion direction
        glm::vec3 leafCenter = TileCenterWorld(leaf);
        glm::vec3 toLeaf = leafCenter - cameraPos;
        float distToLeaf = glm::length(toLeaf);
        
        if (!std::isfinite(distToLeaf)) continue;
        
        glm::vec3 toLeafDir = glm::normalize(toLeaf);
        float directional = glm::dot(velocityDir, toLeafDir);
        
        // Check if leaf is generally in front of camera (directional) or getting closer
        glm::vec3 predictedToLeaf = leafCenter - predictedCameraPos;
        float predictedDist = glm::length(predictedToLeaf);
        bool gettingCloser = predictedDist < distToLeaf;
        
        if (directional < settings_.directionalDotMin && !gettingCloser) {
            stats_.filteredByDirection++;
            continue;
        }
        
        // Try to add the leaf itself
        auto tryAdd = [&](const TileKey& candidate) {
            if (added >= settings_.maxCandidates) return;
            if (!candidate.IsValid()) return;
            if (seen.find(candidate) != seen.end()) return;
            if (candidate.level < minZoom || candidate.level > maxZoom) return;
            
            // P1-6: TTL cooldown check
            if (IsInCooldown(candidate)) {
                stats_.filteredByCooldown++;
                return;
            }
            
            glm::vec3 center = TileCenterWorld(candidate);
            float radius = TileBoundingRadius(candidate);
            
            // Conservative frustum guard
            if (!frustum.IsSphereVisible(center, radius * settings_.frustumRadiusScale)) {
                stats_.filteredByFrustum++;
                return;
            }
            
            candidates.push_back(candidate);
            seen.insert(candidate);
            ++added;
            stats_.generated++;
        };
        
        tryAdd(leaf);
        
        // Add neighbors in motion direction
        if (added < settings_.maxCandidates) {
            auto neighbors = leaf.Neighbors();
            for (const auto& neighbor : neighbors) {
                tryAdd(neighbor);
            }
        }
        
        // Add children for zoom-in anticipation
        if (added < settings_.maxCandidates && leaf.level < maxZoom - 1) {
            auto children = leaf.Children();
            for (const auto& child : children) {
                tryAdd(child);
            }
        }
    }
    
    stats_.dispatched += static_cast<int>(candidates.size());
    return candidates;
}

void PredictivePrefetcher::MarkDispatched(const TileKey& key) {
    std::lock_guard<std::mutex> lock(cooldownMutex_);
    cooldownMap_[key] = std::chrono::steady_clock::now();
}

bool PredictivePrefetcher::IsInCooldown(const TileKey& key) const {
    std::lock_guard<std::mutex> lock(cooldownMutex_);
    auto it = cooldownMap_.find(key);
    if (it == cooldownMap_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
    
    if (elapsed >= settings_.ttlMs) {
        // Expired, but we'll clean it up later
        return false;
    }
    
    return true;
}

void PredictivePrefetcher::CleanupCooldown() const {
    std::lock_guard<std::mutex> lock(cooldownMutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = cooldownMap_.begin(); it != cooldownMap_.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        if (elapsed >= settings_.ttlMs) {
            it = cooldownMap_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace globe
