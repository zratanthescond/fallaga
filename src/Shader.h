#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader
{
public:
    // Program ID
    unsigned int ID = 0;

    // Shader owns a GL program, so it must not be copied (a copy would free the
    // same program twice). Moves transfer ownership and leave the source at 0.
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    // The location cache belongs to the program, so it travels with the move and
    // is dropped from the moved-from object (ID == 0) so nothing can resolve a
    // stale location through it.
    Shader(Shader&& other) noexcept
        : ID(other.ID), uniformLocationCache(std::move(other.uniformLocationCache)) {
        other.ID = 0;
        other.uniformLocationCache.clear();
    }
    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            release();
            ID = other.ID;
            uniformLocationCache = std::move(other.uniformLocationCache);
            other.ID = 0;
            other.uniformLocationCache.clear();
        }
        return *this;
    }

    // Every `new Shader(...)` used to leak its linked program: nothing in the
    // codebase ever called glDeleteProgram.
    ~Shader() { release(); }

    void release() {
        if (ID != 0) {
            glDeleteProgram(ID);
            ID = 0;
        }
        uniformLocationCache.clear();
    }

    bool isValid() const { return ID != 0; }

   
    // Setters take `const char*`: every call site in the project passes a string
    // literal, so this stays source-compatible while the location lookup goes
    // through the cached uniformLocation() fast path instead of hitting the GL
    // driver once per write.
    void setVec2(const char *name, const glm::vec2 &value) const
    {
        glUniform2fv(uniformLocation(name), 1, glm::value_ptr(value));
    }
    void setVec3(const char *name, const glm::vec3 &value) const
    {
        glUniform3fv(uniformLocation(name), 1, glm::value_ptr(value));
    }
    void setVec4(const char *name, const glm::vec4 &value) const
    {
        glUniform4fv(uniformLocation(name), 1, glm::value_ptr(value));
    }
    // Constructor reads and builds the shader
    Shader(const char* vertexPath, const char* fragmentPath)
    {
        ID = 0;
        // 1. Retrieve the vertex/fragment source code from filePath
        std::string vertexCode;
        std::string fragmentCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;

        // Ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
        try 
        {
            // Open files
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            // Read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();		
            // Close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // Convert stream into string
            vertexCode   = vShaderStream.str();
            fragmentCode = fShaderStream.str();			
        }
        catch (std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
        }
        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        // 2. Compile shaders
        unsigned int vertex, fragment;
        
        // Vertex Shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        
        // Fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        
        // Shader Program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        
        // Delete the shaders as they're linked into our program now and no longer necessary
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    // Activate the shader
    void use() 
    { 
        glUseProgram(ID);
    }
  
    // Utility uniform functions
    void setMat4(const char *name, const glm::mat4 &mat) const
    {
        glUniformMatrix4fv(uniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
    }
    
    void setInt(const char *name, int value) const
    {
        glUniform1i(uniformLocation(name), value);
    }
    void setFloat(const char *name, float value) const {
    glUniform1f(uniformLocation(name), value);
}
    void setBool(const char *name, bool value) const {
    glUniform1i(uniformLocation(name), (int)value);
}
    // Get uniform location helper
    GLint getUniformLocation(const std::string &name) const 
    {
        return uniformLocation(name.c_str());
    }

private:
    // Uniform locations are resolved once per program and cached, including the
    // -1 returned for a name that does not exist: a typo'd uniform stays cheap
    // and keeps the previous silent no-op behaviour. A released or moved-from
    // program has ID == 0 and an empty cache, so it resolves to -1 without
    // touching the driver.
    GLint uniformLocation(const char* name) const {
        auto it = uniformLocationCache.find(name);
        if (it != uniformLocationCache.end()) return it->second;
        const GLint loc = (ID != 0) ? glGetUniformLocation(ID, name) : -1;
        uniformLocationCache.emplace(name, loc);
        return loc;
    }

    mutable std::unordered_map<std::string, GLint> uniformLocationCache;

    // Utility function for checking shader compilation/linking errors.
    void checkCompileErrors(unsigned int shader, std::string type)
    {
        int success;
        char infoLog[1024];
        if(type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if(!success)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if(!success)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            }
        }
    }
};