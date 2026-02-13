// RockMesh Runtime - GPU resources for RockTree mesh
// Sprint 1: Single quadkey vertical slice

#pragma once

#include <glad/glad.h>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace globe {

// CPU-side mesh data (produced by worker, consumed by main thread)
struct RockMeshCpu {
    std::string id;  // nodeKey
    
    // Vertices: stride 9 floats (pos3, normal3, uv2, heightKm=0)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    
    // Texture (optional)
    std::vector<uint8_t> rgba;
    int texWidth = 0;
    int texHeight = 0;
    bool hasTexture = false;
    
    // Metadata
    int triangleCount = 0;
    bool valid = false;
    std::string error;
};

// GPU-side mesh resources (main thread only)
struct RockMeshGpu {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint texture = 0;
    bool ownsTexture = false;  // true if we created the texture (not fallback)
    
    uint32_t indexCount = 0;
    std::string id;
    bool valid = false;
    
    // Create from CPU data (main thread, GL context active)
    // Uses fallbackTexture if cpu.hasTexture is false
    bool Create(const RockMeshCpu& cpu, GLuint fallbackTexture);
    
    // Destroy GL resources
    void Destroy();
    
    // Bind and draw
    void Draw() const;
};

} // namespace globe
