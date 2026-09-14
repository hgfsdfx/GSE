#include "stdafx.h"
#include "ShaderProgram.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    GLuint Compile(const std::filesystem::path& path, GLenum type)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Cannot load shader: " << path << '\n';
            return 0;
        }
        const std::string source((std::istreambuf_iterator<char>(file)), {});
        const char* data = source.c_str();
        const GLuint shader = glCreateShader(type);
        if (!shader)
        {
            return 0;
        }
        glShaderSource(shader, 1, &data, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[4096] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Shader compile failed: " << path << '\n' << log << '\n';
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
} // namespace

GLuint LoadShaderProgram(const std::filesystem::path& vertexPath,
                         const std::filesystem::path& fragmentPath)
{
    if (vertexPath.extension() != L".vs" || fragmentPath.extension() != L".fs")
    {
        std::cerr << "Expected independent .vs and .fs shader files: " << vertexPath << ", "
                  << fragmentPath << '\n';
        return 0;
    }
    const GLuint vertex = Compile(vertexPath, GL_VERTEX_SHADER);
    const GLuint fragment = Compile(fragmentPath, GL_FRAGMENT_SHADER);
    if (!vertex || !fragment)
    {
        if (vertex)
        {
            glDeleteShader(vertex);
        }
        if (fragment)
        {
            glDeleteShader(fragment);
        }
        return 0;
    }
    const GLuint program = glCreateProgram();
    if (program)
    {
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!program)
    {
        return 0;
    }
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Shader link failed: " << fragmentPath << '\n' << log << '\n';
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
