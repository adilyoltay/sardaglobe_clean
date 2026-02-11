#pragma once

#include "../core/tile.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace globe {

// GE-style speed gates for unpop/crossfade behavior.
constexpr float kUnpopShortenStartKmPerSec = 120.0f;
constexpr float kUnpopBypassKmPerSec = 900.0f;
constexpr float kUnpopMinDurationSec = 0.08f;
constexpr float kFadeCompleteEpsilon = 0.999f;

inline float ComputeUnpopDurationSec(float cameraSpeedKmPerSec) {
    float speed = std::max(0.0f, cameraSpeedKmPerSec);
    if (!std::isfinite(speed)) {
        speed = 0.0f;
    }
    if (speed <= kUnpopShortenStartKmPerSec) {
        return Tile::FADE_DURATION;
    }
    if (speed >= kUnpopBypassKmPerSec) {
        return kUnpopMinDurationSec;
    }
    float t = (speed - kUnpopShortenStartKmPerSec) /
              (kUnpopBypassKmPerSec - kUnpopShortenStartKmPerSec);
    return Tile::FADE_DURATION + t * (kUnpopMinDurationSec - Tile::FADE_DURATION);
}

inline bool ShouldBypassUnpop(float cameraSpeedKmPerSec) {
    float speed = std::max(0.0f, cameraSpeedKmPerSec);
    return std::isfinite(speed) && speed >= kUnpopBypassKmPerSec;
}

// UV mapping from a leaf tile to an ancestor tile's UV space.
// Returns transform (scale.xy + offset.zw): uv' = uv * scale + offset.
inline glm::vec4 ComputeUnpopUvTransform(const TileKey& leaf, const TileKey& ancestor) {
    if (ancestor.level >= leaf.level) {
        return glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    }

    int delta = leaf.level - ancestor.level;
    float scale = std::ldexp(1.0f, -delta);  // 1 / (2^delta)
    std::int64_t factor = static_cast<std::int64_t>(1) << delta;
    std::int64_t relX = static_cast<std::int64_t>(leaf.x) -
                        static_cast<std::int64_t>(ancestor.x) * factor;
    std::int64_t relY = static_cast<std::int64_t>(leaf.y) -
                        static_cast<std::int64_t>(ancestor.y) * factor;
    // TileKey.y grows southward while UV V-axis grows northward in our mesh convention.
    std::int64_t relYFromSouth = (factor - 1) - relY;

    return glm::vec4(scale, scale,
                     static_cast<float>(relX) * scale,
                     static_cast<float>(relYFromSouth) * scale);
}

// Applies "inner" first, then "outer".
inline glm::vec4 ComposeUvTransform(const glm::vec4& outerTransform, const glm::vec4& innerTransform) {
    return glm::vec4(
        innerTransform.x * outerTransform.x,
        innerTransform.y * outerTransform.y,
        innerTransform.z * outerTransform.x + outerTransform.z,
        innerTransform.w * outerTransform.y + outerTransform.w
    );
}

} // namespace globe
