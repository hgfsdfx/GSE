#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "Dependencies/glew.h"
#include "PostProcessor.h"
#include "MeshCache.h"
#include "FontGlyphCache.h"

struct Point
{
    float x, y;

    Point(float px = 0, float py = 0)
        : x(px),
          y(py)
    {
    }

    Point operator+(Point p) const
    {
        return {x + p.x, y + p.y};
    }

    Point operator-(Point p) const
    {
        return {x - p.x, y - p.y};
    }

    Point operator*(float s) const
    {
        return {x * s, y * s};
    }
};

struct Color
{
    float r, g, b, a, emission;

    Color(float red = 1, float green = 1, float blue = 1, float alpha = 1, float light = 0)
        : r(red),
          g(green),
          b(blue),
          a(alpha),
          emission(light)
    {
    }

    Color Alpha(float value) const
    {
        return {r, g, b, value, emission};
    }

    // Extra radiance above the base linear color; ignored by the LDR/UI path.
    Color Emissive(float intensity) const
    {
        return {r, g, b, a, intensity};
    }
};

// Ordered dynamic batches and cached static meshes; world projection lives in Prototype.
class Renderer
{
  public:

    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool IsInitialized() const
    {
        return program_ != 0 && vao_ != 0 && vbo_ != 0;
    }

    void Resize(int width, int height);
    void Begin(Color background);
    // Composite the world, then accept crisp display-space HUD geometry.
    void BeginOverlay();
    void End();

    // Actual draw submissions in this frame, including post-processing and HUD.
    std::uint64_t DrawCalls() const
    {
        return drawCalls_ + postProcessor_.DrawCalls();
    }

    // On a cache hit, enqueue the mesh and return false. On a miss, submit local
    // coordinates until EndCachedMesh(). Origin/scale apply at draw time only.
    // Do not nest recordings; the key must identify geometry AND color state.
    bool BeginCachedMesh(const std::string& key, Point origin, float scale);
    void EndCachedMesh();

    PostProcessingSettings& PostEffects()
    {
        return postProcessor_.settings;
    }

    bool PostEffectsAvailable() const
    {
        return postProcessor_.Available();
    }

    void Triangle(Point a, Point b, Point c, Color color);
    void Quad(Point a, Point b, Point c, Point d, Color color);
    void Rect(float x, float y, float width, float height, Color color);
    void Line(Point a, Point b, float width, Color color);
    void Circle(Point center, float radius, Color color, int segments = 24);
    void Ring(Point center, float radius, float width, Color color);
    void Text(float x, float y, const std::string& text, Color color, float scale = 2);
    float TextWidth(const std::string& text, float scale = 2);

    int Width() const
    {
        return width_;
    }

    int Height() const
    {
        return height_;
    }

  private:

    void Flush();
    void SealDynamicBatch();
    const std::vector<Point>& CirclePoints(int segments);
    using Vertex = MeshVertex;

    struct DrawCommand
    {
        const MeshCache::Mesh* mesh = nullptr;
        size_t first = 0;
        size_t count = 0;
        Point origin;
        float scale = 1;
    };

    int width_, height_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint viewport_ = -1, linearScene_ = -1, transform_ = -1;
    bool capturing_ = false, overlay_ = false;
    std::uint64_t drawCalls_ = 0;
    PostProcessor postProcessor_;
    MeshCache meshes_;
    std::vector<Vertex> vertices_;
    std::vector<DrawCommand> commands_;
    size_t dynamicStart_ = 0;
    bool recording_ = false;
    std::string recordingKey_;
    Point recordingOrigin_;
    float recordingScale_ = 1;
    std::vector<Vertex> recordedVertices_;
    std::map<int, std::vector<Point>> circles_;
    FontGlyphCache font_;
};
