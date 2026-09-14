#include "stdafx.h"
#include "MeshCache.h"
#include <algorithm>
#include <limits>

MeshCache::~MeshCache()
{
    Clear();
}

void MeshCache::ConfigureVertexAttributes()
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, emission)));
}

void MeshCache::Release(Mesh& mesh)
{
    if (mesh.vao)
    {
        glDeleteVertexArrays(1, &mesh.vao);
    }
    if (mesh.vbo)
    {
        glDeleteBuffers(1, &mesh.vbo);
    }
    bytes_ -= mesh.bytes;
    mesh = {};
}

void MeshCache::Clear()
{
    for (auto& entry : meshes_)
    {
        Release(entry.second);
    }
    meshes_.clear();
}

void MeshCache::BeginFrame()
{
    ++frame_;
    for (auto it = meshes_.begin(); it != meshes_.end();)
    {
        if (frame_ - it->second.lastUsedFrame > IdleFrames)
        {
            Release(it->second);
            it = meshes_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

const MeshCache::Mesh* MeshCache::Find(const std::string& key)
{
    const auto it = meshes_.find(key);
    if (it == meshes_.end())
    {
        return nullptr;
    }
    it->second.lastUsedFrame = frame_;
    return &it->second;
}

const MeshCache::Mesh* MeshCache::Store(const std::string& key,
                                        const std::vector<MeshVertex>& vertices)
{
    if (const auto cached = Find(key))
    {
        return cached;
    }
    if (vertices.empty() || vertices.size() > MaxBytes / sizeof(MeshVertex)
        || vertices.size() > size_t(std::numeric_limits<GLsizei>::max()))
    {
        return nullptr;
    }
    const size_t bytes = vertices.size() * sizeof(MeshVertex);
    // Never evict a mesh referenced by a command queued during this frame.
    while (meshes_.size() >= MaxEntries || bytes_ + bytes > MaxBytes)
    {
        auto oldest = meshes_.end();
        for (auto it = meshes_.begin(); it != meshes_.end(); ++it)
        {
            if (it->second.lastUsedFrame != frame_
                && (oldest == meshes_.end()
                    || it->second.lastUsedFrame < oldest->second.lastUsedFrame))
            {
                oldest = it;
            }
        }
        if (oldest == meshes_.end())
        {
            return nullptr;
        }
        Release(oldest->second);
        meshes_.erase(oldest);
    }
    Mesh mesh;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    if (!mesh.vao || !mesh.vbo)
    {
        Release(mesh);
        return nullptr;
    }
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_STATIC_DRAW);
    GLint64 allocated = 0;
    glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
    if (allocated != static_cast<GLint64>(bytes))
    {
        glBindVertexArray(0);
        Release(mesh);
        return nullptr;
    }
    ConfigureVertexAttributes();
    glBindVertexArray(0);
    mesh.vertexCount = static_cast<GLsizei>(vertices.size());
    mesh.bytes = bytes;
    mesh.lastUsedFrame = frame_;
    const auto result = meshes_.emplace(key, mesh);
    bytes_ += bytes;
    return &result.first->second;
}

size_t MeshCache::Count() const
{
    return meshes_.size();
}

size_t MeshCache::Bytes() const
{
    return bytes_;
}
