#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace globe {

// MeshTemplate - Shared index buffers for tile meshes
class MeshTemplate {
public:
    static uint32_t GetIndexCount(int segments);
    static const std::vector<unsigned int>& GetIndices(int segments);
    static uint32_t GetOrCreateEbo(int segments);
    static bool Exists(int segments);
    static void Clear();

private:
    struct TemplateData {
        std::vector<unsigned int> indices;
        uint32_t ebo = 0;
    };

    static std::mutex mutex_;
    static std::unordered_map<int, TemplateData> templates_;

    static std::vector<unsigned int> BuildIndices(int segments);
};

} // namespace globe
