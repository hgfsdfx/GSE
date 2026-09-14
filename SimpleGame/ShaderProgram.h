#pragma once
#include <filesystem>
#include "Dependencies/glew.h"

// Returns zero on any file, compile or link error. Reports diagnostics to stderr.
GLuint LoadShaderProgram(const std::filesystem::path& vertexPath,
                         const std::filesystem::path& fragmentPath);
