#pragma once
#include "Dependencies/glew.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct MeshVertex
{
    float x, y, r, g, b, a, emission;
};

// Immutable meshes share one texture-buffer atlas, with no VAO/VBO per mesh.
class MeshCache
{
  public:

    struct Mesh
    {
        uint32_t firstVertex = 0, vertexCount = 0;
        float advance = 0;
        uint64_t lastUsedFrame = 0;
        bool pinned = false;
    };

    struct Statistics
    {
        uint64_t generated = 0, diskLoaded = 0, hits = 0, uploadedBytes = 0;
        uint64_t misses = 0, fileMissing = 0, fileInvalid = 0, fileErrors = 0;
        uint64_t fileSaved = 0, fileSkipped = 0, readBytes = 0, writtenBytes = 0;
        uint64_t evicted = 0, compactions = 0, atlasGrowth = 0, uploadRejected = 0;
    };

    MeshCache() = default;
    ~MeshCache();
    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;
    bool Initialize(const std::filesystem::path& directory);
    void BeginFrame();
    const Mesh* Find(const std::string& key);
    const Mesh* Store(const std::string& key,
                      const std::vector<MeshVertex>& vertices,
                      float advance = 0,
                      bool pinned = false);
    void Pin(const Mesh* mesh);
    GLuint Texture() const;
    const Statistics& Stats() const;
    size_t Count() const;
    size_t Bytes() const;

    size_t CapacityBytes() const
    {
        return capacity_ * sizeof(PackedVertex);
    }

    size_t LimitBytes() const
    {
        return maxVertices_ * sizeof(PackedVertex);
    }

    bool DiskEnabled() const
    {
        return diskEnabled_;
    }

  private:

    struct PackedVertex
    {
        float x, y, r, g, b, a, emission, padding;
    };

    const Mesh* Upload(const std::string& key,
                       const std::vector<MeshVertex>& vertices,
                       float advance,
                       bool pinned);
    bool LoadFile(const std::string& key, std::vector<MeshVertex>& vertices, float& advance);
    void SaveFile(const std::string& key, const std::vector<MeshVertex>& vertices, float advance);
    std::filesystem::path FilePath(const std::string& key) const;
    void Compact();
    void UploadAtlas();
    static constexpr size_t MaxEntries = 8192;
    static constexpr uint64_t DiskBudget = 256ull * 1024 * 1024;
    std::map<std::string, Mesh> meshes_;
    std::vector<PackedVertex> atlas_;
    GLuint buffer_ = 0, texture_ = 0;
    size_t capacity_ = 0, maxVertices_ = 0;
    bool compactNextFrame_ = false, diskEnabled_ = false;
    uint64_t frame_ = 0, diskBytes_ = 0;
    size_t diskFiles_ = 0;
    std::filesystem::path directory_;
    Statistics stats_;
};
