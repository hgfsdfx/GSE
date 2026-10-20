#include "stdafx.h"
#include "ModelBatch.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
    constexpr uint32_t Magic = 0x314d5347, Version = 1;
    constexpr uint64_t DiskBudget = 64 * 1024 * 1024;
    static_assert(sizeof(ModelVertex) == 24 * sizeof(float));
    static_assert(sizeof(ModelMath::Matrix) == 16 * sizeof(float));

    uint64_t Hash(const void* data, size_t size, uint64_t value = 14695981039346656037ull)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            value = (value ^ bytes[i]) * 1099511628211ull;
        }
        return value;
    }

    std::filesystem::path CachePath(const std::filesystem::path& directory, const std::string& key)
    {
        std::ostringstream name;
        name << std::hex << Hash(key.data(), key.size()) << ".gmodel";
        return directory / name.str();
    }

    bool Finite(const ModelVertex& vertex)
    {
        for (const auto* field : {&vertex.position,
                                  &vertex.color,
                                  &vertex.normal,
                                  &vertex.joints,
                                  &vertex.weights,
                                  &vertex.emissive})
        {
            for (float value : *field)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

ModelBatch::~ModelBatch()
{
    if (geometryTexture_)
    {
        glDeleteTextures(1, &geometryTexture_);
    }
    if (geometryBuffer_)
    {
        glDeleteBuffers(1, &geometryBuffer_);
    }
    if (boneTexture_)
    {
        glDeleteTextures(1, &boneTexture_);
    }
    if (boneBuffer_)
    {
        glDeleteBuffers(1, &boneBuffer_);
    }
}

bool ModelBatch::Initialize(const std::filesystem::path& cacheDirectory)
{
    directory_ = cacheDirectory;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    diskEnabled_ = !error;
    if (diskEnabled_)
    {
        for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end;
             it.increment(error))
        {
            if (it->is_regular_file(error) && it->path().extension() == L".gmodel")
            {
                const auto bytes = it->file_size(error);
                if (!error)
                {
                    diskBytes_ += bytes;
                }
            }
        }
        diskEnabled_ = !error;
    }
    GLint texels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    texelLimit_ = std::min(size_t(std::max(0, texels)), size_t(1024 * 1024));
    vertexLimit_ =
        std::min(size_t(std::max(0, texels)) / 6, size_t(32 * 1024 * 1024) / sizeof(ModelVertex));
    if (vertexLimit_ < 3 || texelLimit_ < 8)
    {
        return false;
    }
    glGenBuffers(1, &geometryBuffer_);
    glGenTextures(1, &geometryTexture_);
    glGenBuffers(1, &boneBuffer_);
    glGenTextures(1, &boneTexture_);
    capacity_ = std::min(vertexLimit_, size_t(8192));
    glBindBuffer(GL_TEXTURE_BUFFER, geometryBuffer_);
    glBufferData(GL_TEXTURE_BUFFER, capacity_ * sizeof(ModelVertex), nullptr, GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, geometryTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, geometryBuffer_);
    glBindBuffer(GL_TEXTURE_BUFFER, boneBuffer_);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(ModelMath::Vec4), nullptr, GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, boneTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, boneBuffer_);
    bones_.reserve(std::min(texelLimit_, size_t(65536)));
    std::cout << "[AssetGPU] vertexLimit=" << vertexLimit_ << " paletteTexelLimit=" << texelLimit_
              << '\n';
    return geometryBuffer_ && geometryTexture_ && boneBuffer_ && boneTexture_;
}

std::vector<ModelVertex> ModelBatch::Read(const ModelSurface& surface)
{
    if (!diskEnabled_)
    {
        return {};
    }
    std::ifstream stream(CachePath(directory_, surface.key), std::ios::binary);
    uint32_t magic = 0, version = 0;
    uint64_t count = 0, keyHash = 0, checksum = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    stream.read(reinterpret_cast<char*>(&keyHash), sizeof(keyHash));
    stream.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
    if (!stream || magic != Magic || version != Version || count != surface.vertices.size()
        || count > vertexLimit_ || keyHash != Hash(surface.key.data(), surface.key.size()))
    {
        return {};
    }
    std::vector<ModelVertex> result(static_cast<size_t>(count));
    stream.read(reinterpret_cast<char*>(result.data()), result.size() * sizeof(ModelVertex));
    if (!stream || stream.peek() != std::char_traits<char>::eof()
        || checksum != Hash(result.data(), result.size() * sizeof(ModelVertex))
        || checksum != Hash(surface.vertices.data(), surface.vertices.size() * sizeof(ModelVertex)))
    {
        return {};
    }
    for (const auto& vertex : result)
    {
        if (!Finite(vertex))
        {
            return {};
        }
    }
    return result;
}

void ModelBatch::Write(const ModelSurface& surface)
{
    if (!diskEnabled_)
    {
        return;
    }
    const auto path = CachePath(directory_, surface.key);
    const uint64_t bytes = 32 + surface.vertices.size() * sizeof(ModelVertex);
    std::error_code error;
    const auto old = std::filesystem::file_size(path, error);
    const auto replaced = error ? 0 : old;
    if (diskBytes_ - std::min(diskBytes_, replaced) + bytes > DiskBudget)
    {
        return;
    }
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    const uint64_t count = surface.vertices.size();
    const auto key = Hash(surface.key.data(), surface.key.size());
    const auto checksum =
        Hash(surface.vertices.data(), surface.vertices.size() * sizeof(ModelVertex));
    stream.write(reinterpret_cast<const char*>(&Magic), sizeof(Magic));
    stream.write(reinterpret_cast<const char*>(&Version), sizeof(Version));
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    stream.write(reinterpret_cast<const char*>(&key), sizeof(key));
    stream.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    stream.write(reinterpret_cast<const char*>(surface.vertices.data()),
                 count * sizeof(ModelVertex));
    stream.close();
    if (!stream || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        std::filesystem::remove(temporary, error);
        diskEnabled_ = false;
        return;
    }
    diskBytes_ = diskBytes_ - std::min(diskBytes_, replaced) + bytes;
}

bool ModelBatch::Geometry(const ModelSurface& surface, Surface& output)
{
    const auto found = meshes_.find(surface.key);
    if (found != meshes_.end())
    {
        output = found->second;
        return true;
    }
    if (rejected_.count(surface.key))
    {
        return false;
    }
    if (surface.vertices.size() > vertexLimit_ - vertices_.size())
    {
        rejected_.insert(surface.key);
        std::cerr << "[AssetGPU] Atlas full; procedural fallback for " << surface.key << '\n';
        return false;
    }
    const auto decoded = Read(surface);
    const bool disk = !decoded.empty();
    const auto& source = disk ? decoded : surface.vertices;
    const auto first = vertices_.size();
    vertices_.insert(vertices_.end(), source.begin(), source.end());
    glBindBuffer(GL_TEXTURE_BUFFER, geometryBuffer_);
    if (vertices_.size() > capacity_)
    {
        capacity_ = std::min(vertexLimit_, std::max(capacity_ * 2, vertices_.size()));
        glBufferData(GL_TEXTURE_BUFFER, capacity_ * sizeof(ModelVertex), nullptr, GL_STATIC_DRAW);
        glBufferSubData(GL_TEXTURE_BUFFER,
                        0,
                        vertices_.size() * sizeof(ModelVertex),
                        vertices_.data());
    }
    else
    {
        glBufferSubData(GL_TEXTURE_BUFFER,
                        first * sizeof(ModelVertex),
                        source.size() * sizeof(ModelVertex),
                        source.data());
    }
    output = {static_cast<uint32_t>(first), static_cast<uint32_t>(source.size()), 0};
    meshes_.emplace(surface.key, output);
    if (!disk)
    {
        Write(surface);
    }
    std::cout << "[AssetGPU] " << (disk ? "DiskLoad" : "Upload") << " key=" << surface.key
              << " vertices=" << source.size()
              << " atlasBytes=" << vertices_.size() * sizeof(ModelVertex) << '\n';
    return true;
}

size_t ModelBatch::RequiredTexels(const ModelAppearance& appearance) const
{
    size_t result = 0;
    for (const auto& part : appearance.parts)
    {
        for (const auto& surface : part.asset->surfaces)
        {
            result +=
                4 + 4 * (surface.skin < 0 ? 1 : part.asset->skins[surface.skin].joints.size());
        }
    }
    return result;
}

bool ModelBatch::Fits(size_t texels) const
{
    return geometryTexture_ && boneTexture_ && texels <= texelLimit_ - bones_.size();
}

bool ModelBatch::Prepare(const ModelAppearance& appearance,
                         const std::string& clip,
                         float seconds,
                         bool loop,
                         std::vector<Surface>& surfaces)
{
    if (appearance.parts.empty() || !Fits(RequiredTexels(appearance)))
    {
        return false;
    }
    const auto boneStart = bones_.size();
    const auto* animation =
        appearance.animation ? &appearance.animation->Pose(clip, seconds, loop) : nullptr;
    const auto appendMatrix = [&](const ModelMath::Matrix& matrix)
    {
        for (int column = 0; column < 4; ++column)
        {
            const auto* values = matrix.values.data() + column * 4;
            bones_.push_back({values[0], values[1], values[2], values[3]});
        }
    };
    for (const auto& part : appearance.parts)
    {
        const auto& ownPose = animation && part.asset == appearance.animation
                                  ? *animation
                                  : part.asset->Pose(animation ? "" : clip, seconds, loop);
        const auto world = [&](int node) -> const ModelMath::Matrix&
        {
            const int source = part.animationNodes.empty() ? -1 : part.animationNodes[node];
            return animation && source >= 0 ? (*animation)[source] : ownPose[node];
        };
        for (const auto& surface : part.asset->surfaces)
        {
            Surface draw;
            if (!Geometry(surface, draw))
            {
                bones_.resize(boneStart);
                surfaces.clear();
                return false;
            }
            draw.palette = static_cast<uint32_t>(bones_.size());
            bones_.insert(bones_.end(), appearance.palette.begin(), appearance.palette.end());
            if (surface.skin < 0)
            {
                appendMatrix(world(surface.node));
            }
            else
            {
                const auto& skin = part.asset->skins[surface.skin];
                for (size_t joint = 0; joint < skin.joints.size(); ++joint)
                {
                    appendMatrix(
                        ModelMath::Multiply(world(skin.joints[joint]), skin.inverseBind[joint]));
                }
            }
            surfaces.push_back(draw);
        }
    }
    return true;
}

void ModelBatch::Bind()
{
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, boneTexture_);
    glBindBuffer(GL_TEXTURE_BUFFER, boneBuffer_);
    if (!bones_.empty())
    {
        glBufferData(GL_TEXTURE_BUFFER,
                     bones_.size() * sizeof(ModelMath::Vec4),
                     bones_.data(),
                     GL_STREAM_DRAW);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, geometryTexture_);
}

void ModelBatch::Clear()
{
    bones_.clear();
}

size_t ModelBatch::UploadBytes() const
{
    return bones_.size() * sizeof(ModelMath::Vec4);
}
