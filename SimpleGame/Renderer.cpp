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
    const auto folder = std::filesystem::path(executable).parent_path() / L"Shaders";
    program_ = LoadShaderProgram(folder / L"SolidRect.vs", folder / L"SolidRect.fs");
    if (!program_)
    {
        return;
    }
    viewport_ = glGetUniformLocation(program_, "u_Viewport");
    linearScene_ = glGetUniformLocation(program_, "u_LinearScene");
    transform_ = glGetUniformLocation(program_, "u_ModelTransform");
    if (viewport_ < 0 || linearScene_ < 0 || transform_ < 0)
    {
        std::cerr << "Scene shader uniforms are missing. Copy the current Shaders folder beside "
                     "the executable.\n";
        glDeleteProgram(program_);
        program_ = 0;
        return;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    MeshCache::ConfigureVertexAttributes();
    glBindVertexArray(0);
    vertices_.reserve(180000);
    recordedVertices_.reserve(4096);
    commands_.reserve(1024);
    postProcessor_.Initialize(folder);
    Resize(width, height);
}

Renderer::~Renderer()
{
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
    drawCalls_ = 0;
    vertices_.clear();
    commands_.clear();
    dynamicStart_ = 0;
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
    glDisable(GL_DEPTH_TEST);
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
    Flush();
    if (capturing_)
    {
        postProcessor_.Present();
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
    Flush();
}

void Renderer::Flush()
{
    assert(!recording_);
    SealDynamicBatch();
    if (!IsInitialized() || commands_.empty())
    {
        return;
    }
    glUseProgram(program_);
    glUniform2f(viewport_, float(width_), float(height_));
    glUniform1i(linearScene_, capturing_ && !overlay_ ? 1 : 0);
    if (!vertices_.empty())
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     vertices_.size() * sizeof(Vertex),
                     vertices_.data(),
                     GL_STREAM_DRAW);
    }
    // Keep painter order: cached buildings can sit between dynamic actors.
    for (const auto& command : commands_)
    {
        if (command.mesh)
        {
            glBindVertexArray(command.mesh->vao);
            glUniform4f(transform_,
                        command.origin.x,
                        command.origin.y,
                        command.scale,
                        command.scale);
            glDrawArrays(GL_TRIANGLES, 0, command.mesh->vertexCount);
            ++drawCalls_;
        }
        else
        {
            glBindVertexArray(vao_);
            glUniform4f(transform_, 0, 0, 1, 1);
            glDrawArrays(GL_TRIANGLES,
                         static_cast<GLint>(command.first),
                         static_cast<GLsizei>(command.count));
            ++drawCalls_;
        }
    }
    glBindVertexArray(0);
    vertices_.clear();
    commands_.clear();
    dynamicStart_ = 0;
}

void Renderer::SealDynamicBatch()
{
    if (vertices_.size() > dynamicStart_)
    {
        commands_.push_back({nullptr, dynamicStart_, vertices_.size() - dynamicStart_, {}, 1});
        dynamicStart_ = vertices_.size();
    }
}

bool Renderer::BeginCachedMesh(const std::string& key, Point origin, float scale)
{
    assert(!recording_);
    assert(scale > 0);
    if (const auto mesh = meshes_.Find(key))
    {
        SealDynamicBatch();
        commands_.push_back({mesh, 0, 0, origin, scale});
        return false;
    }
    recording_ = true;
    recordingKey_ = key;
    recordingOrigin_ = origin;
    recordingScale_ = scale;
    recordedVertices_.clear();
    return true;
}

void Renderer::EndCachedMesh()
{
    assert(recording_);
    recording_ = false;
    if (const auto mesh = meshes_.Store(recordingKey_, recordedVertices_))
    {
        SealDynamicBatch();
        commands_.push_back({mesh, 0, 0, recordingOrigin_, recordingScale_});
    }
    else
    {
        // Allocation/budget fallback preserves geometry and ordering.
        for (auto vertex : recordedVertices_)
        {
            vertex.x = vertex.x * recordingScale_ + recordingOrigin_.x;
            vertex.y = vertex.y * recordingScale_ + recordingOrigin_.y;
            vertices_.push_back(vertex);
        }
    }
    recordedVertices_.clear();
}

void Renderer::Triangle(Point a, Point b, Point c, Color color)
{
    for (Point p : {a, b, c})
    {
        if (recording_)
        {
            recordedVertices_.push_back(
                {p.x, p.y, color.r, color.g, color.b, color.a, color.emission});
        }
        else
        {
            vertices_.push_back({p.x, p.y, color.r, color.g, color.b, color.a, color.emission});
        }
    }
}

void Renderer::Quad(Point a, Point b, Point c, Point d, Color color)
{
    Triangle(a, b, c, color);
    Triangle(a, c, d, color);
}

void Renderer::Rect(float x, float y, float w, float h, Color c)
{
    Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, c);
}

void Renderer::Line(Point a, Point b, float width, Color c)
{
    Point delta = b - a;
    float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (len < 0.001f)
    {
        return;
    }
    Point normal(-delta.y / len * width * .5f, delta.x / len * width * .5f);
    Quad(a + normal, b + normal, b - normal, a - normal, c);
}

void Renderer::Circle(Point center, float radius, Color c, int segments)
{
    const auto& points = CirclePoints(segments);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        Triangle(center, center + points[i] * radius, center + points[i + 1] * radius, c);
    }
}

void Renderer::Ring(Point center, float radius, float width, Color c)
{
    const auto& points = CirclePoints(40);
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        Line(center + points[i] * radius, center + points[i + 1] * radius, width, c);
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
    float widest = 0;
    float width = 0;
    for (const wchar_t ch : FontGlyphCache::Decode(text))
    {
        if (ch == L'\n')
        {
            widest = std::max(widest, width);
            width = 0;
        }
        else if (ch != L'\r')
        {
            width += font_.Get(ch).advance * scale;
        }
    }
    return std::max(widest, width);
}
