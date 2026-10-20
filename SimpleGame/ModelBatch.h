#pragma once
#include "ModelAsset.h"
#include "Dependencies/glew.h"
#include <set>

// Geometry is uploaded once; only palettes and bone matrices are streamed per batch.
class ModelBatch
{
  public:

    struct Surface
    {
        uint32_t first = 0, count = 0, palette = 0;
    };

    ~ModelBatch();
    bool Initialize(const std::filesystem::path& cacheDirectory);
    size_t RequiredTexels(const ModelAppearance& appearance) const;
    bool Fits(size_t texels) const;
    bool Prepare(const ModelAppearance& appearance,
                 const std::string& clip,
                 float seconds,
                 bool loop,
                 std::vector<Surface>& surfaces);
    void Bind();
    void Clear();
    size_t UploadBytes() const;

  private:

    bool Geometry(const ModelSurface& surface, Surface& output);
    std::vector<ModelVertex> Read(const ModelSurface& surface);
    void Write(const ModelSurface& surface);
    GLuint geometryBuffer_ = 0, geometryTexture_ = 0, boneBuffer_ = 0, boneTexture_ = 0;
    size_t vertexLimit_ = 0, texelLimit_ = 0, capacity_ = 0;
    std::vector<ModelVertex> vertices_;
    std::vector<ModelMath::Vec4> bones_;
    std::map<std::string, Surface> meshes_;
    std::set<std::string> rejected_;
    std::filesystem::path directory_;
    uint64_t diskBytes_ = 0;
    bool diskEnabled_ = false;
};
