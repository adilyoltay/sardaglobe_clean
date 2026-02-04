#include "tile_renderer.h"
#include <glad/glad.h>
#include <cmath>

namespace globe {

TileRenderer::TileRenderer(ShaderManager& shaderManager)
    : shaderManager_(shaderManager) {
}

void TileRenderer::BeginBatch(const glm::mat4& mvp, bool wireframe) {
    stats_ = RenderStats{};
    batchActive_ = true;
    wireframeMode_ = wireframe;
    currentMvp_ = mvp;
    
    // Setup shader
    shaderManager_.UseTileShader();
    glUniformMatrix4fv(shaderManager_.GetMvpLocation(), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1i(shaderManager_.GetTextureLocation(), 0);
    glUniform1f(shaderManager_.GetFadeLocation(), 1.0f);
    
    // Enable polygon offset to reduce z-fighting between tiles
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    
    // Wireframe mode
    if (wireframeMode_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
}

void TileRenderer::RenderTile(const Tile& tile) {
    if (!batchActive_) return;
    if (!tile.IsReady() || !tile.hasMesh || tile.textureId == 0) return;
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tile.textureId);
    
    // Draw tile mesh
    glBindVertexArray(tile.vao);
    glDrawElements(GL_TRIANGLES, tile.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    
    // Update stats
    stats_.tilesRendered++;
    stats_.drawCalls++;
    stats_.trianglesRendered += tile.indexCount / 3;
}

void TileRenderer::EndBatch() {
    if (!batchActive_) return;
    
    // Reset polygon mode
    if (wireframeMode_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
    glDisable(GL_POLYGON_OFFSET_FILL);
    batchActive_ = false;
}

void TileRenderer::InitPivotGeometry() {
    // Create a simple crosshair/target for pivot visualization
    // Google Earth style: circle with crosshairs
    const int circleSegments = 32;
    const float outerRadius = 0.02f;  // Screen-relative size
    const float innerRadius = 0.008f;
    
    std::vector<float> vertices;
    
    // Outer circle
    for (int i = 0; i <= circleSegments; ++i) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / circleSegments;
        vertices.push_back(std::cos(angle) * outerRadius);
        vertices.push_back(std::sin(angle) * outerRadius);
        vertices.push_back(0.0f);
    }
    
    // Crosshair lines
    // Horizontal
    vertices.push_back(-outerRadius * 1.5f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(-innerRadius); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(innerRadius); vertices.push_back(0.0f); vertices.push_back(0.0f);
    vertices.push_back(outerRadius * 1.5f); vertices.push_back(0.0f); vertices.push_back(0.0f);
    
    // Vertical
    vertices.push_back(0.0f); vertices.push_back(-outerRadius * 1.5f); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(-innerRadius); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(innerRadius); vertices.push_back(0.0f);
    vertices.push_back(0.0f); vertices.push_back(outerRadius * 1.5f); vertices.push_back(0.0f);
    
    pivotVertexCount_ = static_cast<int>(vertices.size() / 3);
    
    glGenVertexArrays(1, &pivotVao_);
    glGenBuffers(1, &pivotVbo_);
    
    glBindVertexArray(pivotVao_);
    glBindBuffer(GL_ARRAY_BUFFER, pivotVbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
}

void TileRenderer::DestroyPivotGeometry() {
    if (pivotVao_ != 0) {
        glDeleteVertexArrays(1, &pivotVao_);
        pivotVao_ = 0;
    }
    if (pivotVbo_ != 0) {
        glDeleteBuffers(1, &pivotVbo_);
        pivotVbo_ = 0;
    }
}

void TileRenderer::RenderPivot(const glm::mat4& mvp, const glm::dvec3& pivotPoint, bool visible) {
    if (!visible || pivotVao_ == 0) return;
    
    // Transform pivot to screen space and render as overlay
    // This is a simplified version - full implementation would use screen-space rendering
    glm::vec4 clipPos = mvp * glm::vec4(glm::vec3(pivotPoint), 1.0f);
    if (clipPos.w <= 0.0f) return;  // Behind camera
    
    // Could render at screen position - for now just indicate pivot exists
    // Full implementation would use separate screen-space shader
}

} // namespace globe
