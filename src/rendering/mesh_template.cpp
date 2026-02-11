#include "mesh_template.h"
#include <glad/glad.h>

namespace globe {

std::mutex MeshTemplate::mutex_;
std::unordered_map<MeshTemplate::TemplateKey, MeshTemplate::TemplateData, MeshTemplate::TemplateKeyHash>
    MeshTemplate::templates_;

uint32_t MeshTemplate::GetMainIndexCount(int segments, uint8_t stitchMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    TemplateKey key{segments, stitchMask, 0};
    auto& data = templates_[key];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments, stitchMask, 0,
                                    data.mainIndexCount, data.skirtIndexCount);
    }
    return data.mainIndexCount;
}

uint32_t MeshTemplate::GetMainIndexCount(int segments) {
    return GetMainIndexCount(segments, 0);
}

uint32_t MeshTemplate::GetSkirtIndexCount(int segments, uint8_t skirtMask) {
    uint32_t activeEdges = 0;
    for (int bit = 0; bit < 4; ++bit) {
        if (skirtMask & (1u << bit)) {
            ++activeEdges;
        }
    }
    return activeEdges * static_cast<uint32_t>(6 * segments);
}

uint32_t MeshTemplate::GetIndexCount(int segments, uint8_t stitchMask, uint8_t skirtMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    TemplateKey key{segments, stitchMask, skirtMask};
    auto& data = templates_[key];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments, stitchMask, skirtMask,
                                    data.mainIndexCount, data.skirtIndexCount);
    }
    return static_cast<uint32_t>(data.indices.size());
}

const std::vector<unsigned int>& MeshTemplate::GetIndices(int segments, uint8_t stitchMask, uint8_t skirtMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    TemplateKey key{segments, stitchMask, skirtMask};
    auto& data = templates_[key];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments, stitchMask, skirtMask,
                                    data.mainIndexCount, data.skirtIndexCount);
    }
    return data.indices;
}

uint32_t MeshTemplate::GetOrCreateEbo(int segments, uint8_t stitchMask, uint8_t skirtMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    TemplateKey key{segments, stitchMask, skirtMask};
    auto& data = templates_[key];
    if (data.indices.empty()) {
        data.indices = BuildIndices(segments, stitchMask, skirtMask,
                                    data.mainIndexCount, data.skirtIndexCount);
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

bool MeshTemplate::Exists(int segments, uint8_t stitchMask, uint8_t skirtMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    TemplateKey key{segments, stitchMask, skirtMask};
    auto it = templates_.find(key);
    return it != templates_.end() && !it->second.indices.empty();
}

uint32_t MeshTemplate::GetIndexCount(int segments) {
    return GetIndexCount(segments, 0, 0x0F);
}

const std::vector<unsigned int>& MeshTemplate::GetIndices(int segments) {
    return GetIndices(segments, 0, 0x0F);
}

uint32_t MeshTemplate::GetOrCreateEbo(int segments) {
    return GetOrCreateEbo(segments, 0, 0x0F);
}

bool MeshTemplate::Exists(int segments) {
    return Exists(segments, 0, 0x0F);
}

void MeshTemplate::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, data] : templates_) {
        if (data.ebo != 0) {
            glDeleteBuffers(1, &data.ebo);
            data.ebo = 0;
        }
    }
    templates_.clear();
}

std::vector<unsigned int> MeshTemplate::BuildIndices(
    int segments,
    uint8_t stitchMask,
    uint8_t skirtMask,
    uint32_t& outMainIndexCount,
    uint32_t& outSkirtIndexCount
) {
    std::vector<unsigned int> indices;
    outMainIndexCount = static_cast<uint32_t>(6 * segments * segments);
    outSkirtIndexCount = GetSkirtIndexCount(segments, skirtMask);
    indices.reserve(static_cast<size_t>(outMainIndexCount + outSkirtIndexCount));

    const bool stitchNorth = (stitchMask & 0x01) != 0;
    const bool stitchEast  = (stitchMask & 0x02) != 0;
    const bool stitchSouth = (stitchMask & 0x04) != 0;
    const bool stitchWest  = (stitchMask & 0x08) != 0;

    auto vidx = [segments](int x, int y) -> unsigned int {
        return static_cast<unsigned int>(y * (segments + 1) + x);
    };

    auto appendCell = [&](int ix, int iy) {
        unsigned int tl = vidx(ix, iy);
        unsigned int tr = vidx(ix + 1, iy);
        unsigned int bl = vidx(ix, iy + 1);
        unsigned int br = vidx(ix + 1, iy + 1);
        indices.push_back(tl);
        indices.push_back(bl);
        indices.push_back(tr);
        indices.push_back(tr);
        indices.push_back(bl);
        indices.push_back(br);
    };

    auto handledByEdgeStitch = [&](int ix, int iy) {
        if (stitchNorth && iy == 0) return true;
        if (stitchSouth && iy == segments - 1) return true;
        // Keep corner cells owned by north/south strips to avoid overlap.
        if (stitchWest && ix == 0 && iy > 0 && iy < segments - 1) return true;
        if (stitchEast && ix == segments - 1 && iy > 0 && iy < segments - 1) return true;
        return false;
    };

    // Regular (non-stitched) cells.
    for (int iy = 0; iy < segments; ++iy) {
        for (int ix = 0; ix < segments; ++ix) {
            if (handledByEdgeStitch(ix, iy)) {
                continue;
            }
            appendCell(ix, iy);
        }
    }

    // North stitched strip (pair 2 cells -> 3 tris).
    if (stitchNorth && segments >= 2) {
        int ix = 0;
        for (; ix + 1 < segments; ix += 2) {
            unsigned int t0 = vidx(ix, 0);
            unsigned int t2 = vidx(ix + 2, 0);
            unsigned int b0 = vidx(ix, 1);
            unsigned int b1 = vidx(ix + 1, 1);
            unsigned int b2 = vidx(ix + 2, 1);
            indices.push_back(t0); indices.push_back(b0); indices.push_back(b1);
            indices.push_back(t0); indices.push_back(b1); indices.push_back(t2);
            indices.push_back(t2); indices.push_back(b1); indices.push_back(b2);
        }
        if (ix < segments) {
            appendCell(ix, 0);
        }
    }

    // South stitched strip.
    if (stitchSouth && segments >= 2) {
        int iy = segments - 1;
        int ix = 0;
        for (; ix + 1 < segments; ix += 2) {
            unsigned int u0 = vidx(ix, iy);
            unsigned int u1 = vidx(ix + 1, iy);
            unsigned int u2 = vidx(ix + 2, iy);
            unsigned int v0 = vidx(ix, iy + 1);
            unsigned int v2 = vidx(ix + 2, iy + 1);
            indices.push_back(u0); indices.push_back(v0); indices.push_back(u1);
            indices.push_back(u1); indices.push_back(v0); indices.push_back(v2);
            indices.push_back(u1); indices.push_back(v2); indices.push_back(u2);
        }
        if (ix < segments) {
            appendCell(ix, iy);
        }
    }

    // West stitched strip (excluding corner row cells handled by north/south).
    if (stitchWest && segments >= 3) {
        int iyStart = 1;
        int iyEndExclusive = segments - 1;
        int iy = iyStart;
        for (; iy + 1 < iyEndExclusive; iy += 2) {
            unsigned int t0 = vidx(0, iy);
            unsigned int t2 = vidx(0, iy + 2);
            unsigned int i0 = vidx(1, iy);
            unsigned int i1 = vidx(1, iy + 1);
            unsigned int i2 = vidx(1, iy + 2);
            indices.push_back(t0); indices.push_back(i0); indices.push_back(i1);
            indices.push_back(t0); indices.push_back(i1); indices.push_back(t2);
            indices.push_back(t2); indices.push_back(i1); indices.push_back(i2);
        }
        if (iy < iyEndExclusive) {
            appendCell(0, iy);
        }
    }

    // East stitched strip (excluding corner row cells handled by north/south).
    if (stitchEast && segments >= 3) {
        int iyStart = 1;
        int iyEndExclusive = segments - 1;
        int iy = iyStart;
        for (; iy + 1 < iyEndExclusive; iy += 2) {
            unsigned int i0 = vidx(segments - 1, iy);
            unsigned int i1 = vidx(segments - 1, iy + 1);
            unsigned int i2 = vidx(segments - 1, iy + 2);
            unsigned int t0 = vidx(segments, iy);
            unsigned int t2 = vidx(segments, iy + 2);
            indices.push_back(i0); indices.push_back(i1); indices.push_back(t0);
            indices.push_back(t0); indices.push_back(i1); indices.push_back(t2);
            indices.push_back(t2); indices.push_back(i1); indices.push_back(i2);
        }
        if (iy < iyEndExclusive) {
            appendCell(segments - 1, iy);
        }
    }
    outMainIndexCount = static_cast<uint32_t>(indices.size());

    const unsigned int mainVertexCount = static_cast<unsigned int>((segments + 1) * (segments + 1));

    // Dynamic skirt vertex offsets: only edges present in skirtMask have vertices.
    // Must match the layout in TileMeshBuilder::GenerateSkirts().
    unsigned int skirtCursor = mainVertexCount;
    unsigned int northSkirtStart = skirtCursor;
    if (skirtMask & 0x01) skirtCursor += segments + 1;
    unsigned int southSkirtStart = skirtCursor;
    if (skirtMask & 0x04) skirtCursor += segments + 1;
    unsigned int westSkirtStart = skirtCursor;
    if (skirtMask & 0x08) skirtCursor += segments + 1;
    unsigned int eastSkirtStart = skirtCursor;
    if (skirtMask & 0x02) skirtCursor += segments + 1;

    auto appendEdge = [&](uint8_t edgeBit) {
        if ((skirtMask & edgeBit) == 0) {
            return;
        }
        if (edgeBit == 0x01) {
            for (int i = 0; i < segments; ++i) {
                unsigned int v0 = i;
                unsigned int v1 = i + 1;
                unsigned int v2 = northSkirtStart + i;
                unsigned int v3 = northSkirtStart + i + 1;
                indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
                indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
            }
        } else if (edgeBit == 0x04) {
            for (int i = 0; i < segments; ++i) {
                unsigned int v0 = segments * (segments + 1) + i;
                unsigned int v1 = segments * (segments + 1) + i + 1;
                unsigned int v2 = southSkirtStart + i;
                unsigned int v3 = southSkirtStart + i + 1;
                indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
                indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
            }
        } else if (edgeBit == 0x08) {
            for (int j = 0; j < segments; ++j) {
                unsigned int v0 = j * (segments + 1);
                unsigned int v1 = (j + 1) * (segments + 1);
                unsigned int v2 = westSkirtStart + j;
                unsigned int v3 = westSkirtStart + j + 1;
                indices.push_back(v0); indices.push_back(v3); indices.push_back(v2);
                indices.push_back(v0); indices.push_back(v1); indices.push_back(v3);
            }
        } else if (edgeBit == 0x02) {
            for (int j = 0; j < segments; ++j) {
                unsigned int v0 = j * (segments + 1) + segments;
                unsigned int v1 = (j + 1) * (segments + 1) + segments;
                unsigned int v2 = eastSkirtStart + j;
                unsigned int v3 = eastSkirtStart + j + 1;
                indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
                indices.push_back(v0); indices.push_back(v3); indices.push_back(v1);
            }
        }
    };

    appendEdge(0x01);
    appendEdge(0x02);
    appendEdge(0x04);
    appendEdge(0x08);

    if (indices.size() >= outMainIndexCount) {
        outSkirtIndexCount = static_cast<uint32_t>(indices.size() - outMainIndexCount);
    } else {
        outSkirtIndexCount = 0;
    }

    return indices;
}

} // namespace globe
