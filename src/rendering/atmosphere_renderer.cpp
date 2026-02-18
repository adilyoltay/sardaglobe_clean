// P0-1: Atmosphere/Sky Dome renderer implementation

#include "atmosphere_renderer.h"
#include <iostream>
#include <glm/gtc/matrix_inverse.hpp>

namespace globe {

namespace {

// Full-screen triangle vertex shader
// Generates a triangle that covers the entire screen
const char* kSkyDomeVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
out vec2 vTexCoord;
void main() {
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

// Sky dome fragment shader with Rayleigh + Mie scattering
// Approximates atmospheric scattering for GE-style horizon
const char* kSkyDomeFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform float uTurbidity;
uniform float uIntensity;
uniform vec3 uGroundColor;

const float PI = 3.14159265359;

// Rayleigh phase function
float RayleighPhase(float cosTheta) {
    return 0.75 * (1.0 + cosTheta * cosTheta);
}

// Simplified Mie phase (Henyey-Greenstein approximation)
float MiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Calculate sky color based on view direction
vec3 CalculateSky(vec3 viewDir, vec3 sunDir, float turbidity, float intensity) {
    float cosTheta = dot(viewDir, sunDir);
    float upDot = max(viewDir.y, 0.0);  // Simplified: Y is up
    
    // Rayleigh scattering (blue sky)
    float rayleigh = RayleighPhase(cosTheta);
    vec3 rayleighColor = vec3(0.3, 0.5, 1.0) * rayleigh;
    
    // Mie scattering (sun glow/haze)
    float mie = MiePhase(cosTheta, 0.76);
    vec3 mieColor = vec3(1.0, 0.9, 0.7) * mie * turbidity * 0.3;
    
    // Horizon gradient
    float horizon = smoothstep(0.0, 0.3, upDot);
    vec3 horizonColor = mix(uGroundColor, vec3(0.5, 0.7, 1.0), horizon);
    
    // Combine
    vec3 sky = rayleighColor * 0.5 + mieColor;
    sky = mix(horizonColor, sky, 0.6);
    
    // Sun disk
    float sunAngle = acos(clamp(cosTheta, -1.0, 1.0));
    float sunDisk = smoothstep(0.05, 0.0, sunAngle) * 2.0;
    sky += vec3(1.0, 0.95, 0.8) * sunDisk;
    
    return sky * intensity;
}

void main() {
    // Reconstruct view direction from screen position
    vec4 clipPos = vec4(vTexCoord * 2.0 - 1.0, 1.0, 1.0);
    vec4 viewPos = uInvViewProj * clipPos;
    vec3 viewDir = normalize(viewPos.xyz - uCameraPos);
    
    vec3 skyColor = CalculateSky(viewDir, uSunDir, uTurbidity, uIntensity);
    
    // Tone mapping approximation (simple reinhard)
    skyColor = skyColor / (1.0 + skyColor);
    
    FragColor = vec4(skyColor, 1.0);
}
)";

// Full-screen triangle vertices
const float kFullscreenTriangle[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f
};

} // namespace

AtmosphereRenderer::AtmosphereRenderer() = default;

AtmosphereRenderer::~AtmosphereRenderer() {
    Shutdown();
}

bool AtmosphereRenderer::Init() {
    if (initialized_) {
        return true;
    }
    
    // Create shader program
    if (!CreateShader()) {
        std::cerr << "[AtmosphereRenderer] Failed to create shader\n";
        return false;
    }
    
    // Create VAO/VBO for full-screen triangle
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenTriangle), kFullscreenTriangle, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    initialized_ = true;
    std::cout << "[AtmosphereRenderer] Initialized\n";
    return true;
}

void AtmosphereRenderer::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    DeleteShader();
    
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    
    initialized_ = false;
}

void AtmosphereRenderer::Render(const glm::mat4& invViewProj,
                                 const glm::vec3& cameraPosKm,
                                 const glm::vec3& sunDir,
                                 const AtmosphereSettings& settings) {
    if (!initialized_ || !settings.enabled) {
        return;
    }
    
    // Save GL state
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLint prevProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    
    // Disable depth test for sky dome (render at infinity)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    
    // Use shader
    glUseProgram(program_);
    
    // Set uniforms (only those actually used by shader)
    glUniformMatrix4fv(uInvViewProj_, 1, GL_FALSE, &invViewProj[0][0]);
    glUniform3fv(uCameraPos_, 1, &cameraPosKm.x);
    glUniform3fv(uSunDir_, 1, &sunDir.x);
    glUniform1f(uTurbidity_, settings.turbidity);
    glUniform1f(uIntensity_, settings.intensity);
    glUniform3fv(uGroundColor_, 1, settings.groundColor);
    
    // Bind VAO and draw full-screen triangle
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    
    // Restore GL state
    glDepthMask(GL_TRUE);
    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    glUseProgram(prevProgram);
}

bool AtmosphereRenderer::CreateShader() {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &kSkyDomeVertexShader, nullptr);
    glCompileShader(vertexShader);
    
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, log);
        std::cerr << "[AtmosphereRenderer] Vertex shader error: " << log << "\n";
        glDeleteShader(vertexShader);
        return false;
    }
    
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &kSkyDomeFragmentShader, nullptr);
    glCompileShader(fragmentShader);
    
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, log);
        std::cerr << "[AtmosphereRenderer] Fragment shader error: " << log << "\n";
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    
    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);
    
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program_, 512, nullptr, log);
        std::cerr << "[AtmosphereRenderer] Link error: " << log << "\n";
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // Get uniform locations (only those used by shader)
    uInvViewProj_ = glGetUniformLocation(program_, "uInvViewProj");
    uCameraPos_ = glGetUniformLocation(program_, "uCameraPos");
    uSunDir_ = glGetUniformLocation(program_, "uSunDir");
    uTurbidity_ = glGetUniformLocation(program_, "uTurbidity");
    uIntensity_ = glGetUniformLocation(program_, "uIntensity");
    uGroundColor_ = glGetUniformLocation(program_, "uGroundColor");
    
    return true;
}

void AtmosphereRenderer::DeleteShader() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

} // namespace globe
