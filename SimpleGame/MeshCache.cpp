#include "stdafx.h"
#include "ProfileMetrics.h"
#include "MeshCache.h"
#include <algorithm>
#include <iostream>

MeshCache::~MeshCache()
{
    if (texture_)
    {
        glDeleteTextures(1, &texture_);
    }
    if (buffer_)
    {
        glDeleteBuffers(1, &buffer_);
    }
}

bool MeshCache::Initialize(const std::filesystem::path& directory)
{
    directory_ = directory;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    diskEnabled_ = !error;
    if (diskEnabled_)
    {
        for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end;
             it.increment(error))
        {
            if (it->is_regular_file(error) && it->path().extension() == L".gmesh")
            {
                const auto bytes = it->file_size(error);
                if (!error)
                {
                    diskBytes_ += bytes;
                    ++diskFiles_;
                }
            }
        }
        diskEnabled_ = !error;
    }
    if (!diskEnabled_)
    {
        std::cerr << "메시 파일 캐시를 사용할 수 없습니다. 메모리 캐시로 계속합니다.\n";
    }
    GLint texels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    maxVertices_ =
        std::min(size_t(std::max(0, texels)) / 2, size_t(64 * 1024 * 1024) / sizeof(PackedVertex));
    if (maxVertices_ < 6)
    {
        return false;
    }
    glGenBuffers(1, &buffer_);
    glGenTextures(1, &texture_);
    capacity_ = std::min(maxVertices_, size_t(32768));
    glBindBuffer(GL_TEXTURE_BUFFER, buffer_);
    glBufferData(GL_TEXTURE_BUFFER, capacity_ * sizeof(PackedVertex), nullptr, GL_DYNAMIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, texture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer_);
    return buffer_ != 0 && texture_ != 0;
}

void MeshCache::BeginFrame()
{
    stats_ = {};
    ++frame_;
    // Repack only between frames, after all queued triangle references were consumed.
    if (compactNextFrame_)
    {
        bool removed = false;
        for (auto it = meshes_.begin(); it != meshes_.end();)
        {
            if (!it->second.pinned && frame_ - it->second.lastUsedFrame > 1)
            {
                it = meshes_.erase(it);
                ++stats_.evicted;
                removed = true;
            }
            else
            {
                ++it;
            }
        }
        if (removed)
        {
            Compact();
        }
        compactNextFrame_ = false;
    }
}

void MeshCache::Compact()
{
    ++stats_.compactions;
    std::vector<PackedVertex> compact;
    for (auto& entry : meshes_)
    {
        auto& mesh = entry.second;
        const auto first = compact.size();
        compact.insert(compact.end(),
                       atlas_.begin() + mesh.firstVertex,
                       atlas_.begin() + mesh.firstVertex + mesh.vertexCount);
        mesh.firstVertex = static_cast<uint32_t>(first);
    }
    atlas_.swap(compact);
    UploadAtlas();
}

void MeshCache::UploadAtlas()
{
    const CpuProfileScope profile(CpuMetric::CacheUpload);
    glBindBuffer(GL_TEXTURE_BUFFER, buffer_);
    glBufferData(GL_TEXTURE_BUFFER, capacity_ * sizeof(PackedVertex), nullptr, GL_DYNAMIC_DRAW);
    if (!atlas_.empty())
    {
        glBufferSubData(GL_TEXTURE_BUFFER, 0, atlas_.size() * sizeof(PackedVertex), atlas_.data());
        stats_.uploadedBytes += atlas_.size() * sizeof(PackedVertex);
    }
}

const MeshCache::Mesh* MeshCache::Find(const std::string& key)
{
    const auto found = meshes_.find(key);
    if (found != meshes_.end())
    {
        found->second.lastUsedFrame = frame_;
        ++stats_.hits;
        return &found->second;
    }
    std::vector<MeshVertex> vertices;
    ++stats_.misses;
    float advance = 0;
    if (LoadFile(key, vertices, advance))
    {
        if (const auto mesh = Upload(key, vertices, advance, false))
        {
            ++stats_.diskLoaded;
            return mesh;
        }
    }
    return nullptr;
}

const MeshCache::Mesh* MeshCache::Store(const std::string& key,
                                        const std::vector<MeshVertex>& vertices,
                                        float advance,
                                        bool pinned)
{
    const auto found = meshes_.find(key);
    if (found != meshes_.end())
    {
        found->second.lastUsedFrame = frame_;
        found->second.pinned |= pinned;
        return &found->second;
    }
    ++stats_.generated;
    SaveFile(key, vertices, advance);
    return Upload(key, vertices, advance, pinned);
}

const MeshCache::Mesh* MeshCache::Upload(const std::string& key,
                                         const std::vector<MeshVertex>& vertices,
                                         float advance,
                                         bool pinned)
{
    if (vertices.size() % 3 != 0 || vertices.size() > maxVertices_ - atlas_.size()
        || meshes_.size() >= MaxEntries)
    {
        compactNextFrame_ = true;
        ++stats_.uploadRejected;
        return nullptr;
    }
    const auto first = atlas_.size();
    for (const auto& v : vertices)
    {
        atlas_.push_back({v.x, v.y, v.r, v.g, v.b, v.a, v.emission, 0});
    }
    if (atlas_.size() > capacity_)
    {
        ++stats_.atlasGrowth;
        capacity_ = std::min(maxVertices_, std::max(atlas_.size(), capacity_ * 2));
        UploadAtlas();
    }
    else if (!vertices.empty())
    {
        const CpuProfileScope profile(CpuMetric::CacheUpload);
        glBindBuffer(GL_TEXTURE_BUFFER, buffer_);
        glBufferSubData(GL_TEXTURE_BUFFER,
                        first * sizeof(PackedVertex),
                        vertices.size() * sizeof(PackedVertex),
                        atlas_.data() + first);
        stats_.uploadedBytes += vertices.size() * sizeof(PackedVertex);
    }
    return &meshes_
                .emplace(key,
                         Mesh{static_cast<uint32_t>(first),
                              static_cast<uint32_t>(vertices.size()),
                              advance,
                              frame_,
                              pinned})
                .first->second;
}

void MeshCache::Pin(const Mesh* mesh)
{
    if (mesh)
    {
        const_cast<Mesh*>(mesh)->pinned = true;
    }
}

GLuint MeshCache::Texture() const
{
    return texture_;
}

const MeshCache::Statistics& MeshCache::Stats() const
{
    return stats_;
}

size_t MeshCache::Count() const
{
    return meshes_.size();
}

size_t MeshCache::Bytes() const
{
    return atlas_.size() * sizeof(PackedVertex);
}
