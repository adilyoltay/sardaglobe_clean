#pragma once

#include "../core/config.h"
#include <glm/glm.hpp>
#include <glad/glad.h>

namespace globe {

// P0-1: Atmosphere/Sky Dome renderer for GE visual parity
// Renders a full-screen sky dome with Rayleigh + Mie scattering
// and horizon gradient to eliminate black void during space-to-surface transitions
class AtmosphereRenderer {
public:
    AtmosphereRenderer();
    ~AtmosphereRenderer();
    
    // Initialize GL resources (VAO, VBO, shaders)
    bool Init();
    
    // Cleanup GL resources
    void Shutdown();
    
    // Render sky dome
    // @param invViewProj Inverse view-projection matrix (for ray direction reconstruction)
    // @param cameraPosKm Camera position in kilometers (ECEF)
    // @param sunDir Normalized sun direction vector
    // @param settings Atmosphere settings (turbidity, intensity, groundColor)
    // @param viewport Current viewport dimensions
    void Render(const glm::mat4& invViewProj,
                const glm::vec3& cameraPosKm,
                const glm::vec3& sunDir,
                const AtmosphereSettings& settings);
    
    // Check if initialized
    bool IsInitialized() const { return initialized_; }

private:
    bool CreateShader();
    void DeleteShader();
    
    bool initialized_ = false;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    
    // Uniform locations (only those used by shader)
    GLint uInvViewProj_ = -1;
    GLint uCameraPos_ = -1;
    GLint uSunDir_ = -1;
    GLint uTurbidity_ = -1;
    GLint uIntensity_ = -1;
    GLint uGroundColor_ = -1;
};

} // namespace globe
