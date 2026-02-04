#include "shader_manager.h"
#include <glad/glad.h>
#include <iostream>

namespace globe {

ShaderManager::ShaderManager() {}

ShaderManager::~ShaderManager() {
    if (tileProgram_ != 0) {
        glDeleteProgram(tileProgram_);
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
    if (tileProgram_ == 0) {
        GLuint vert = CompileShader(GL_VERTEX_SHADER, shaders::TILE_VERTEX);
        GLuint frag = CompileShader(GL_FRAGMENT_SHADER, shaders::TILE_FRAGMENT);
        
        if (vert && frag) {
            tileProgram_ = LinkProgram(vert, frag);
            
            if (tileProgram_) {
                mvpLoc_ = glGetUniformLocation(tileProgram_, "uMVP");
                texLoc_ = glGetUniformLocation(tileProgram_, "uTexture");
                fadeLoc_ = glGetUniformLocation(tileProgram_, "uFade");
            }
        }
    }
    return tileProgram_;
}

void ShaderManager::UseTileShader() {
    GetTileProgram();
    if (tileProgram_) {
        glUseProgram(tileProgram_);
    }
}

} // namespace globe
