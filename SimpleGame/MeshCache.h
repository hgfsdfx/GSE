#pragma once
#include "Dependencies/glew.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct MeshVertex
{
    float x, y, r, g, b, a, emission;
};

// Cache entries own immutable GPU vertex data. Only transforms vary per draw.
class MeshCache
{
  public:

    struct Mesh
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
        size_t bytes = 0;
        uint64_t lastUsedFrame = 0;
    };

    MeshCache() = default;
    ~MeshCache();
    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;
    // Call only after all commands from the previous frame have been consumed.
    void BeginFrame();
    const Mesh* Find(const std::string& key);
    const Mesh* Store(const std::string& key, const std::vector<MeshVertex>& vertices);
    void Clear();
    size_t Count() const;
    size_t Bytes() const;
    static void ConfigureVertexAttributes();

  private:

    static constexpr size_t MaxEntries = 512;
    static constexpr size_t MaxBytes = 64 * 1024 * 1024;
    static constexpr uint64_t IdleFrames = 120;
    void Release(Mesh& mesh);
    std::map<std::string, Mesh> meshes_;
    size_t bytes_ = 0;
    uint64_t frame_ = 0;
};
