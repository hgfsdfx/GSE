#include "stdafx.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <Windows.h>

namespace
{
    float SRGBToLinear(float value)
    {
        return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
    }

    void AppendQuad(std::vector<MeshVertex>& vertices,
                    float x,
                    float y,
                    float w,
                    float h,
                    float alpha)
    {
        for (Point p : {Point{x, y},
                        Point{x + w, y},
                        Point{x + w, y + h},
                        Point{x, y},
                        Point{x + w, y + h},
                        Point{x, y + h}})
        {
            vertices.push_back({p.x, p.y, 1, 1, 1, alpha, 0});
        }
    }
} // namespace

Renderer::Renderer(int width, int height)
    : width_(width),
      height_(height)
{
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768)
    {
        return;
    }
    const auto base = std::filesystem::path(executable).parent_path();
    const auto folder = base / L"Shaders";
    if (!meshes_.Initialize(base / L"MeshCache" / L"v3"))
    {
        return;
    }
    std::cout << "메시 캐시: " << (base / L"MeshCache" / L"v3").u8string() << '\n';
    program_ = LoadShaderProgram(folder / L"SolidRect.vs", folder / L"SolidRect.fs");
    if (!program_)
    {
        return;
    }
    viewport_ = glGetUniformLocation(program_, "u_Viewport");
    linearScene_ = glGetUniformLocation(program_, "u_LinearScene");
    depthStep_ = glGetUniformLocation(program_, "u_DepthStep");
    const auto meshUniform = glGetUniformLocation(program_, "u_MeshVertices");
    const auto instanceUniform = glGetUniformLocation(program_, "u_Instances");
    if (viewport_ < 0 || linearScene_ < 0 || depthStep_ < 0 || meshUniform < 0
        || instanceUniform < 0)
    {
        std::cerr << "셰이더 버전이 맞지 않습니다. 실행 파일 옆 Shaders 폴더를 갱신해 주세요.\n";
        glDeleteProgram(program_);
        program_ = 0;
        return;
    }
    const auto modelUniform = glGetUniformLocation(program_, "u_ModelVertices");
    const auto boneUniform = glGetUniformLocation(program_, "u_Bones");
    if (modelUniform < 0 || boneUniform < 0)
    {
        std::cerr << "[AssetGPU] Model shader uniforms missing. Update Shaders.\n";
        glDeleteProgram(program_);
        program_ = 0;
        return;
    }
    models_.Initialize(base / L"MeshCache" / L"models-v1");
    glUseProgram(program_);
    glUniform1i(modelUniform, 3);
    glUniform1i(boneUniform, 2);
    glUniform1i(meshUniform, 0);
    glUniform1i(instanceUniform, 1);
    glUseProgram(0);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &instanceBuffer_);
    glGenTextures(1, &instanceTexture_);
    GLint texels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texels);
    maxInstances_ = std::min(size_t(65536), size_t(std::max(4, texels)) / 4);
    glBindBuffer(GL_TEXTURE_BUFFER, instanceBuffer_);
    glBufferData(GL_TEXTURE_BUFFER, maxInstances_ * sizeof(MeshInstance), nullptr, GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, instanceTexture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, instanceBuffer_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(TriangleReference), nullptr);
    glVertexAttribDivisor(0, 1);
    glBindVertexArray(0);
    triangle_ = meshes_.Find("primitive:triangle:v1");
    if (!triangle_)
    {
        triangle_ =
            meshes_.Store("primitive:triangle:v1",
                          {{0, 0, 1, 1, 1, 1, 0}, {1, 0, 1, 1, 1, 1, 0}, {0, 1, 1, 1, 1, 1, 0}},
                          0,
                          true);
    }
    quad_ = meshes_.Find("primitive:quad:v1");
    if (!quad_)
    {
        std::vector<Vertex> vertices;
        AppendQuad(vertices, 0, 0, 1, 1, 1);
        quad_ = meshes_.Store("primitive:quad:v1", vertices, 0, true);
    }
    meshes_.Pin(triangle_);
    meshes_.Pin(quad_);
    instances_.reserve(8192);
    triangles_.reserve(131072);
    recordedVertices_.reserve(4096);
    postProcessor_.Initialize(folder);
    gpuProfiler_.Initialize();
    Resize(width, height);
}

Renderer::~Renderer()
{
    if (instanceTexture_)
    {
        glDeleteTextures(1, &instanceTexture_);
    }
    if (instanceBuffer_)
    {
        glDeleteBuffers(1, &instanceBuffer_);
    }
    if (vbo_)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_)
    {
        glDeleteVertexArrays(1, &vao_);
    }
    if (program_)
    {
        glDeleteProgram(program_);
    }
}

void Renderer::Resize(int width, int height)
{
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    postProcessor_.Resize(width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
}

void Renderer::Begin(Color c)
{
    const auto frame = frameStats_.frame + 1;
    frameStats_ = {};
    frameStats_.frame = frame;
    batches_.clear();
    drawCalls_ = instanceCount_ = instanceUploadBytes_ = 0;
    instances_.clear();
    triangles_.clear();
    models_.Clear();
    recording_ = false;
    meshes_.BeginFrame();
    overlay_ = false;
    capturing_ = postProcessor_.BeginScene();
    if (capturing_)
    {
        c = {SRGBToLinear(c.r), SRGBToLinear(c.g), SRGBToLinear(c.b), c.a};
    }
    glClearColor(c.r, c.g, c.b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::BeginOverlay()
{
    if (overlay_)
    {
        return;
    }
    Flush("world_end");
    if (capturing_)
    {
        const bool timed = gpuProfiler_.Begin(frameStats_.frame, 0, "post");
        if (ProfilingEnabled && !timed)
        {
            ++frameStats_.gpuSkipped;
        }
        postProcessor_.Present();
        if (timed)
        {
            gpuProfiler_.End();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_FRAMEBUFFER_SRGB);
    overlay_ = true;
}

void Renderer::End()
{
    if (!overlay_)
    {
        BeginOverlay();
    }
    Flush("frame_end");
}

void Renderer::Flush(const char* reason)
{
    // Can also flush part of a large frame; FBO, painter order and HDR mode remain unchanged.
    if (!IsInitialized() || triangles_.empty())
    {
        return;
    }
    static_assert(sizeof(MeshInstance) == 16 * sizeof(float));
    static_assert(sizeof(TriangleReference) == 2 * sizeof(uint32_t));
    const auto start = ProfilingEnabled ? ProfileClock::now() : ProfileClock::time_point{};
    const char* stage = overlay_ ? "hud" : "world";
    const auto batch = drawCalls_ + 1;
    const bool timed = gpuProfiler_.Begin(frameStats_.frame, batch, stage);
    if (ProfilingEnabled && !timed)
    {
        ++frameStats_.gpuSkipped;
    }
    // Each ordered batch paints over the preceding batch; models inside this
    // batch retain per-triangle depth for limbs, faces and modular parts.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearDepth(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    models_.Bind();
    glUseProgram(program_);
    glUniform2f(viewport_, float(width_), float(height_));
    glUniform1f(depthStep_, 1.8f / float(instances_.size() + 1));
    glUniform1i(linearScene_, capturing_ && !overlay_ ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, meshes_.Texture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, instanceTexture_);
    glBindBuffer(GL_TEXTURE_BUFFER, instanceBuffer_);
    glBufferData(GL_TEXTURE_BUFFER,
                 instances_.size() * sizeof(MeshInstance),
                 instances_.data(),
                 GL_STREAM_DRAW);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 triangles_.size() * sizeof(TriangleReference),
                 triangles_.data(),
                 GL_STREAM_DRAW);
    // One ordered triangle stream can reference many meshes and transforms.
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, static_cast<GLsizei>(triangles_.size()));
    ++drawCalls_;
    if (timed)
    {
        gpuProfiler_.End();
    }
    if (overlay_)
    {
        ++frameStats_.hudDraws;
    }
    else
    {
        ++frameStats_.worldDraws;
    }
    frameStats_.triangles += triangles_.size();
    if (ProfilingEnabled)
    {
        batches_.push_back({frameStats_.frame,
                            batch,
                            instances_.size(),
                            triangles_.size(),
                            instances_.size() * sizeof(MeshInstance)
                                + triangles_.size() * sizeof(TriangleReference)
                                + models_.UploadBytes(),
                            stage,
                            reason,
                            ProfileMilliseconds(ProfileClock::now() - start)});
    }
    instanceUploadBytes_ += instances_.size() * sizeof(MeshInstance)
                            + triangles_.size() * sizeof(TriangleReference) + models_.UploadBytes();
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    instances_.clear();
    triangles_.clear();
    models_.Clear();
}

void Renderer::Submit(const MeshCache::Mesh& mesh,
                      Point origin,
                      Point xAxis,
                      Point yAxis,
                      Color tint)
{
    MeshInstance instance{origin.x,
                          origin.y,
                          xAxis.x,
                          xAxis.y,
                          yAxis.x,
                          yAxis.y,
                          0,
                          0,
                          tint.r,
                          tint.g,
                          tint.b,
                          tint.a,
                          tint.emission,
                          0,
                          0,
                          0};
    for (uint32_t first = 0; first < mesh.vertexCount;)
    {
        if (instances_.size() >= maxInstances_ || triangles_.size() >= MaxBatchTriangles)
        {
            Flush(instances_.size() >= maxInstances_ ? "instance_limit" : "triangle_limit");
        }
        const auto index = static_cast<uint32_t>(instances_.size());
        instance.reserved0 = float(instances_.size());
        instances_.push_back(instance);
        ++instanceCount_;
        const auto count =
            std::min(size_t((mesh.vertexCount - first) / 3), MaxBatchTriangles - triangles_.size());
        const auto offset = triangles_.size();
        triangles_.resize(offset + count);
        auto* references = triangles_.data() + offset;
        // Grow once per mesh, rather than checking vector capacity for every triangle.
        for (size_t i = 0; i < count; ++i)
        {
            references[i] = {mesh.firstVertex + first, index};
            first += 3;
        }
    }
}

bool Renderer::BeginCachedMesh(const std::string& key, Point origin, float scale)
{
    assert(!recording_);
    assert(scale > 0);
    // Building meshes contain font geometry. Invalidate them when font data changes.
    recordingKey_ = "scene:v3:" + font_.Identity() + ":" + key;
    if (const auto mesh = meshes_.Find(recordingKey_))
    {
        Submit(*mesh, origin, {scale, 0}, {0, scale}, {});
        return false;
    }
    recording_ = true;
    recordingOrigin_ = origin;
    recordingScale_ = scale;
    recordedVertices_.clear();
    return true;
}

bool Renderer::DrawCachedMesh(const std::string& key,
                              Point origin,
                              Point xAxis,
                              Point yAxis,
                              Color tint)
{
    assert(!recording_);
    if (const auto mesh = meshes_.Find("scene:v3:" + font_.Identity() + ":" + key))
    {
        Submit(*mesh, origin, xAxis, yAxis, tint);
        return true;
    }
    return false;
}

void Renderer::EndCachedMesh()
{
    assert(recording_);
    recording_ = false;
    if (const auto mesh = meshes_.Store(recordingKey_, recordedVertices_))
    {
        Submit(*mesh, recordingOrigin_, {recordingScale_, 0}, {0, recordingScale_}, {});
    }
    else
    {
        // Atlas pressure: reuse the pinned unit triangle, without dropping geometry.
        for (size_t i = 0; i + 2 < recordedVertices_.size(); i += 3)
        {
            const auto& a = recordedVertices_[i];
            const auto& b = recordedVertices_[i + 1];
            const auto& c = recordedVertices_[i + 2];
            Triangle(recordingOrigin_ + Point(a.x, a.y) * recordingScale_,
                     recordingOrigin_ + Point(b.x, b.y) * recordingScale_,
                     recordingOrigin_ + Point(c.x, c.y) * recordingScale_,
                     {a.r, a.g, a.b, a.a, a.emission});
        }
    }
    recordedVertices_.clear();
}

void Renderer::RecordTriangle(Point a, Point b, Point c, Color color)
{
    for (Point p : {a, b, c})
    {
        recordedVertices_.push_back({p.x, p.y, color.r, color.g, color.b, color.a, color.emission});
    }
}

void Renderer::Triangle(Point a, Point b, Point c, Color color)
{
    if (recording_)
    {
        RecordTriangle(a, b, c, color);
    }
    else
    {
        Submit(*triangle_, a, b - a, c - a, color);
    }
}

void Renderer::Quad(Point a, Point b, Point c, Point d, Color color)
{
    if (!recording_ && std::abs(c.x - b.x - d.x + a.x) < .001f
        && std::abs(c.y - b.y - d.y + a.y) < .001f)
    {
        Submit(*quad_, a, b - a, d - a, color);
    }
    else
    {
        Triangle(a, b, c, color);
        Triangle(a, c, d, color);
    }
}

void Renderer::Rect(float x, float y, float w, float h, Color color)
{
    if (recording_)
    {
        Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, color);
    }
    else
    {
        Submit(*quad_, {x, y}, {w, 0}, {0, h}, color);
    }
}

void Renderer::Line(Point a, Point b, float width, Color color)
{
    const Point delta = b - a;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length < .001f)
    {
        return;
    }
    const Point normal(-delta.y / length * width * .5f, delta.x / length * width * .5f);
    Quad(a + normal, b + normal, b - normal, a - normal, color);
}

void Renderer::Circle(Point center, float radius, Color color, int segments)
{
    segments = std::clamp(segments, 3, 128);
    if (!recording_)
    {
        const auto key = "primitive:circle:v1:" + std::to_string(segments);
        auto mesh = meshes_.Find(key);
        if (!mesh)
        {
            std::vector<Vertex> vertices;
            const auto& points = CirclePoints(segments);
            for (size_t i = 0; i + 1 < points.size(); ++i)
            {
                for (Point p : {Point{}, points[i], points[i + 1]})
                {
                    vertices.push_back({p.x, p.y, 1, 1, 1, 1, 0});
                }
            }
            mesh = meshes_.Store(key, vertices);
        }
        if (mesh)
        {
            Submit(*mesh, center, {radius, 0}, {0, radius}, color);
            return;
        }
    }
    const auto& points = CirclePoints(segments);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        Triangle(center, center + points[i] * radius, center + points[i + 1] * radius, color);
    }
}

void Renderer::Ring(Point center, float radius, float width, Color color)
{
    const auto& points = CirclePoints(40);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        Line(center + points[i] * radius, center + points[i + 1] * radius, width, color);
    }
}

const std::vector<Point>& Renderer::CirclePoints(int segments)
{
    segments = std::clamp(segments, 3, 128);
    auto& points = circles_[segments];
    if (points.empty())
    {
        points.reserve(segments + 1);
        for (int i = 0; i < segments; ++i)
        {
            const float angle = i * 6.2831853f / segments;
            points.push_back({std::cos(angle), std::sin(angle)});
        }
        points.push_back(points.front());
    }
    return points;
}

const MeshCache::Mesh* Renderer::GlyphMesh(wchar_t character)
{
    const auto key = "glyph:v3:" + font_.Identity() + ":" + std::to_string(uint32_t(character));
    if (const auto mesh = meshes_.Find(key))
    {
        return mesh;
    }
    const auto& glyph = font_.Get(character);
    std::vector<Vertex> vertices;
    vertices.reserve(glyph.runs.size() * 6);
    for (const auto& run : glyph.runs)
    {
        AppendQuad(vertices, run.x, run.y, run.width, run.height, run.coverage);
    }
    return meshes_.Store(key, vertices, glyph.advance);
}

void Renderer::Text(float x, float y, const std::string& text, Color color, float scale)
{
    const float start = x;
    for (const wchar_t ch : FontGlyphCache::Decode(text))
    {
        if (ch == L'\n')
        {
            x = start;
            y += FontGlyphCache::LineHeight * scale;
            continue;
        }
        if (ch == L'\r')
        {
            continue;
        }
        if (!recording_)
        {
            if (const auto mesh = GlyphMesh(ch))
            {
                Submit(*mesh, {x, y}, {scale, 0}, {0, scale}, color);
                x += mesh->advance * scale;
                continue;
            }
        }
        const auto& glyph = font_.Get(ch);
        for (const auto& run : glyph.runs)
        {
            Rect(x + run.x * scale,
                 y + run.y * scale,
                 run.width * scale,
                 run.height * scale,
                 color.Alpha(color.a * run.coverage));
        }
        x += glyph.advance * scale;
    }
}

float Renderer::TextWidth(const std::string& text, float scale)
{
    float widest = 0, width = 0;
    for (const wchar_t ch : FontGlyphCache::Decode(text))
    {
        if (ch == L'\n')
        {
            widest = std::max(widest, width);
            width = 0;
        }
        else if (ch != L'\r')
        {
            const auto mesh = GlyphMesh(ch);
            width += (mesh ? mesh->advance : font_.Get(ch).advance) * scale;
        }
    }
    return std::max(widest, width);
}
