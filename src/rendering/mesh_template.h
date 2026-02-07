#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace globe {

// MeshTemplate - Shared index buffers for tile meshes
class MeshTemplate {
public:
    static uint32_t GetMainIndexCount(int segments, uint8_t stitchMask);
    static uint32_t GetMainIndexCount(int segments);
    static uint32_t GetSkirtIndexCount(int segments, uint8_t skirtMask);
    static uint32_t GetIndexCount(int segments, uint8_t stitchMask, uint8_t skirtMask);
    static const std::vector<unsigned int>& GetIndices(int segments, uint8_t stitchMask, uint8_t skirtMask);
    static uint32_t GetOrCreateEbo(int segments, uint8_t stitchMask, uint8_t skirtMask);
    static bool Exists(int segments, uint8_t stitchMask, uint8_t skirtMask);

    // Backward-compatible helpers: full skirts, no stitching.
    static uint32_t GetIndexCount(int segments);
    static const std::vector<unsigned int>& GetIndices(int segments);
    static uint32_t GetOrCreateEbo(int segments);
    static bool Exists(int segments);
    static void Clear();

private:
    struct TemplateKey {
        int segments = 0;
        uint8_t stitchMask = 0;
        uint8_t skirtMask = 0;

        bool operator==(const TemplateKey& other) const {
            return segments == other.segments &&
                   stitchMask == other.stitchMask &&
                   skirtMask == other.skirtMask;
        }
    };

    struct TemplateKeyHash {
        std::size_t operator()(const TemplateKey& key) const noexcept {
            std::size_t h = static_cast<std::size_t>(key.segments);
            h = (h * 1315423911u) ^ static_cast<std::size_t>(key.stitchMask);
            h = (h * 2654435761u) ^ static_cast<std::size_t>(key.skirtMask);
            return h;
        }
    };

    struct TemplateData {
        std::vector<unsigned int> indices;
        uint32_t ebo = 0;
        uint32_t mainIndexCount = 0;
        uint32_t skirtIndexCount = 0;
    };

    static std::mutex mutex_;
    static std::unordered_map<TemplateKey, TemplateData, TemplateKeyHash> templates_;

    static std::vector<unsigned int> BuildIndices(int segments, uint8_t stitchMask, uint8_t skirtMask,
                                                  uint32_t& outMainIndexCount,
                                                  uint32_t& outSkirtIndexCount);
};

} // namespace globe
