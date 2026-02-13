// RockMesh Runtime Implementation

#include "rockmesh_runtime.h"
#include <iostream>
#include <cstring>

namespace globe {

bool RockMeshGpu::Create(const RockMeshCpu& cpu, GLuint fallbackTexture) {
    if (!cpu.valid || cpu.vertices.empty() || cpu.indices.empty()) {
        return false;
    }
    
    id = cpu.id;
    indexCount = static_cast<uint32_t>(cpu.indices.size());
    
    // VAO
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // VBO
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 
                 cpu.vertices.size() * sizeof(float),
                 cpu.vertices.data(),
                 GL_STATIC_DRAW);
    
    // EBO
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 cpu.indices.size() * sizeof(uint32_t),
                 cpu.indices.data(),
                 GL_STATIC_DRAW);
    
    // Attribute layout (matches tile shader):
    // loc0: position3
    // loc1: normal3
    // loc2: uv2
    // loc3: heightKm (float)
    constexpr GLsizei stride = 9 * sizeof(float);
    
    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    
    // Normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    
    // UV
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    
    // HeightKm (always 0 for rockmesh)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    
    glBindVertexArray(0);
    
    // Texture
    if (cpu.hasTexture && !cpu.rgba.empty() && cpu.texWidth > 0 && cpu.texHeight > 0) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     cpu.texWidth, cpu.texHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, cpu.rgba.data());
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        ownsTexture = true;
    } else {
        // Use fallback texture
        texture = fallbackTexture;
        ownsTexture = false;
    }
    
    valid = true;
    return true;
}

void RockMeshGpu::Destroy() {
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ebo) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    // Only delete texture if we own it (not fallback)
    if (texture && ownsTexture) {
        glDeleteTextures(1, &texture);
    }
    texture = 0;
    ownsTexture = false;
    valid = false;
    indexCount = 0;
}

void RockMeshGpu::Draw() const {
    if (!valid || vao == 0 || indexCount == 0) return;
    
    glBindVertexArray(vao);
    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace globe
