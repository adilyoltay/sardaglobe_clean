#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glad/glad.h>

struct LabelInfo {
    std::string text;
    glm::vec3 worldPos;
    glm::vec4 color = glm::vec4(1.0f);
    
    // Atlas coordinates (UV)
    glm::vec2 uvMin = glm::vec2(0.0f);
    glm::vec2 uvMax = glm::vec2(0.0f);
    int width = 0;
    int height = 0;
    bool visible = true;
};

class LabelManager {
public:
    LabelManager();
    ~LabelManager();

    void Init();
    void Shutdown();

    // Add a label request. Returns an ID.
    int AddLabel(const std::string& text, double lat, double lon, double alt);
    
    // Remove a label by ID
    void RemoveLabel(int id);
    
    // Clear all labels
    void ClearLabels();
    
    // Render all visible labels
    void Render(const glm::mat4& mvp, const glm::vec3& cameraPos);

private:
    GLuint atlasTexture_ = 0;
    int atlasWidth_ = 1024;
    int atlasHeight_ = 1024;
    int currentY_ = 0;
    int currentRowHeight_ = 0;
    int currentX_ = 0;
    
    std::unordered_map<int, LabelInfo> labels_;
    int nextLabelId_ = 1;
    
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint program_ = 0;
    GLint uMvpLoc_ = -1;
    GLint uTexLoc_ = -1;
    
    // Helper to rasterize text using ImGui font
    bool RasterizeTextToAtlas(const std::string& text, LabelInfo& info);
    
    void CreateLabelShader();
};
