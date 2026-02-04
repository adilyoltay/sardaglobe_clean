#include "shader_manager.h"
#include <glad/glad.h>
#include <iostream>
#include <sstream>

namespace globe {

ShaderManager::ShaderManager() {}

ShaderManager::~ShaderManager() {
    if (tileProgram_ != 0) {
        glDeleteProgram(tileProgram_);
    }
    // Delete cached variant programs
    for (auto& [flags, program] : programCache_) {
        if (program != 0) {
            glDeleteProgram(program);
        }
    }
}

uint32_t ShaderManager::CompileShader(uint32_t type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t ShaderManager::LinkProgram(uint32_t vertShader, uint32_t fragShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Program link error: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return program;
}

uint32_t ShaderManager::GetTileProgram() {
    return GetTileProgram(ShaderFlags::None);
}

uint32_t ShaderManager::GetTileProgram(ShaderFlags flags) {
    uint32_t flagsKey = static_cast<uint32_t>(flags);
    
    // Check cache first
    auto it = programCache_.find(flagsKey);
    if (it != programCache_.end()) {
        return it->second;
    }
    
    // Build and compile new variant
    GLuint vert = CompileShader(GL_VERTEX_SHADER, shaders::TILE_VERTEX);
    std::string fragSource = BuildFragmentShader(flags);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());
    
    uint32_t program = 0;
    if (vert && frag) {
        program = LinkProgram(vert, frag);
    }
    
    // Cache the program
    programCache_[flagsKey] = program;
    
    // Also set as default if no flags
    if (flags == ShaderFlags::None) {
        tileProgram_ = program;
    }
    
    return program;
}

std::string ShaderManager::BuildFragmentShader(ShaderFlags flags) {
    std::ostringstream ss;
    
    ss << "#version 330 core\n";
    ss << "in vec2 vTexCoord;\n";
    ss << "in vec3 vNormal;\n";
    ss << "in vec3 vWorldPos;\n";
    ss << "\n";
    ss << "uniform sampler2D uTexture;\n";
    ss << "uniform float uFade;\n";
    
    if (HasFlag(flags, ShaderFlags::DebugLOD)) {
        ss << "uniform int uLodLevel;\n";
    }
    
    ss << "\n";
    ss << "out vec4 fragColor;\n";
    ss << "\n";
    ss << "void main() {\n";
    ss << "    vec4 texColor = texture(uTexture, vTexCoord);\n";
    
    if (HasFlag(flags, ShaderFlags::DebugSeams)) {
        // Highlight tile edges
        ss << "    float edgeDist = min(min(vTexCoord.x, 1.0 - vTexCoord.x), min(vTexCoord.y, 1.0 - vTexCoord.y));\n";
        ss << "    if (edgeDist < 0.02) { texColor.rgb = vec3(1.0, 0.0, 0.0); }\n";
    }
    
    if (HasFlag(flags, ShaderFlags::DebugLOD)) {
        // Color-code by LOD level
        ss << "    vec3 lodColors[10] = vec3[](";
        ss << "vec3(1,0,0), vec3(1,0.5,0), vec3(1,1,0), vec3(0.5,1,0), vec3(0,1,0),";
        ss << "vec3(0,1,0.5), vec3(0,1,1), vec3(0,0.5,1), vec3(0,0,1), vec3(0.5,0,1));\n";
        ss << "    int idx = clamp(uLodLevel, 0, 9);\n";
        ss << "    texColor.rgb = mix(texColor.rgb, lodColors[idx], 0.4);\n";
    }
    
    if (HasFlag(flags, ShaderFlags::NoLighting)) {
        ss << "    vec3 color = texColor.rgb;\n";
    } else {
        // Standard lighting
        ss << "    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));\n";
        ss << "    vec3 normal = normalize(vNormal);\n";
        ss << "    float diff = max(dot(normal, lightDir), 0.3);\n";
        ss << "    vec3 color = texColor.rgb * diff;\n";
    }
    
    ss << "    fragColor = vec4(color, texColor.a * uFade);\n";
    ss << "}\n";
    
    return ss.str();
}

void ShaderManager::CacheUniformLocations(uint32_t program) {
    mvpLoc_ = glGetUniformLocation(program, "uMVP");
    texLoc_ = glGetUniformLocation(program, "uTexture");
    fadeLoc_ = glGetUniformLocation(program, "uFade");
    lodLevelLoc_ = glGetUniformLocation(program, "uLodLevel");
}

void ShaderManager::UseTileShader() {
    UseTileShader(ShaderFlags::None);
}

void ShaderManager::UseTileShader(ShaderFlags flags) {
    uint32_t program = GetTileProgram(flags);
    if (program) {
        glUseProgram(program);
        CacheUniformLocations(program);
        activeFlags_ = flags;
    }
}

} // namespace globe
