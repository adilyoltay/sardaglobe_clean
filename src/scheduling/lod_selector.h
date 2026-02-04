#pragma once

#include "../core/tile_key.h"
#include "../core/tile.h"
#include "../core/config.h"
#include "../math/frustum.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include <functional>

namespace globe {

// Result of LOD selection
struct LodSelection {
    std::unordered_set<TileKey> required;  // All tiles needed (ancestors + leaves)
    std::vector<TileKey> leaves;           // Tiles to render
    int refinedCount = 0;                  // Number of subdivisions
};

// SSE-based LOD selector with frustum/horizon culling
class LodSelector {
public:
    using TileReadyFunc = std::function<bool(const TileKey&)>;
    
    struct Settings {
        int minZoom = 0;
        int maxZoom = 22;
        float sseThreshold = 1.4f;
        float tiltFactor = 1.0f;  // 0-1, reduces detail when tilted
    };
    
    LodSelector() = default;
    
    // Perform LOD selection
    LodSelection Select(
        const glm::vec3& cameraPos,
        const glm::mat4& mvp,
        int viewportWidth,
        int viewportHeight,
        const TileReadyFunc& isReady,
        const Settings& settings
    );

private:
    void TraverseTile(
        const TileKey& key,
        const glm::vec3& cameraPos,
        const glm::mat4& mvp,
        int viewportHeight,
        const TileReadyFunc& isReady,
        const Settings& settings,
        LodSelection& result,
        int depth
    );
    
    bool ShouldSubdivide(
        const TileKey& key,
        const glm::vec3& cameraPos,
        int viewportHeight,
        float fovDegrees,
        float sseThreshold,
        float tiltFactor
    );
    
    bool AreChildrenReady(const TileKey& key, const TileReadyFunc& isReady);
    
    Frustum frustum_;
    HorizonCuller horizon_;
    float fovDegrees_ = 45.0f;
};

} // namespace globe
