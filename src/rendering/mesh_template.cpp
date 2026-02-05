#include "mesh_template.h"
#include <glad/glad.h>

namespace globe {

std::mutex MeshTemplate::mutex_;
std::unordered_map<int, MeshTemplate::TemplateData> MeshTemplate::templates_;

uint32_t MeshTemplate::GetIndexCount(int segments) {
    return static_cast<uint32_t>(6 * segments * segments + 24 * segments);
}

const std::vector<unsigned int>& MeshTemplate::GetIndices(int segments) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& data = templates_[segments];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments);
    }
    return data.indices;
}

uint32_t MeshTemplate::GetOrCreateEbo(int segments) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& data = templates_[segments];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments);
    }
    if (data.ebo == 0) {
        glGenBuffers(1, &data.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     data.indices.size() * sizeof(unsigned int),
                     data.indices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    return data.ebo;
}

bool MeshTemplate::Exists(int segments) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = templates_.find(segments);
    return it != templates_.end() && !it->second.indices.empty();
}

void MeshTemplate::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [segments, data] : templates_) {
        if (data.ebo != 0) {
            glDeleteBuffers(1, &data.ebo);
            data.ebo = 0;
        }
    }
}

std::vector<unsigned int> MeshTemplate::BuildIndices(int segments) {
    std::vector<unsigned int> indices;
    indices.reserve(GetIndexCount(segments));

    // Indices for main grid
    for (int iy = 0; iy < segments; ++iy) {
        for (int ix = 0; ix < segments; ++ix) {
            unsigned int tl = iy * (segments + 1) + ix;
            unsigned int tr = tl + 1;
            unsigned int bl = tl + (segments + 1);
            unsigned int br = bl + 1;

            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));

    unsigned int northSkirtStart = mainVertexCount;
    unsigned int southSkirtStart = northSkirtStart + segments + 1;
    unsigned int westSkirtStart = southSkirtStart + segments + 1;
    unsigned int eastSkirtStart = westSkirtStart + segments + 1;

    // North edge skirt
    for (int i = 0; i < segments; ++i) {
        unsigned int v0 = i;
        unsigned int v1 = i + 1;
        unsigned int v2 = northSkirtStart + i;
        unsigned int v3 = northSkirtStart + i + 1;
        indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
        indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
    }

    // South edge skirt (reversed winding)
    for (int i = 0; i < segments; ++i) {
        unsigned int v0 = segments * (segments + 1) + i;
        unsigned int v1 = segments * (segments + 1) + i + 1;
        unsigned int v2 = southSkirtStart + i;
        unsigned int v3 = southSkirtStart + i + 1;
        indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
        indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
    }

    // West edge skirt (reversed winding)
    for (int j = 0; j < segments; ++j) {
        unsigned int v0 = j * (segments + 1);
        unsigned int v1 = (j + 1) * (segments + 1);
        unsigned int v2 = westSkirtStart + j;
        unsigned int v3 = westSkirtStart + j + 1;
        indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
        indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
    }

    // East edge skirt
    for (int j = 0; j < segments; ++j) {
        unsigned int v0 = j * (segments + 1) + segments;
        unsigned int v1 = (j + 1) * (segments + 1) + segments;
        unsigned int v2 = eastSkirtStart + j;
        unsigned int v3 = eastSkirtStart + j + 1;
        indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
        indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
    }

    return indices;
}

} // namespace globe
