#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include "Dependencies/glew.h"
#include "PostProcessor.h"
#include "MeshCache.h"
#include "FontGlyphCache.h"
#include "GpuProfiler.h"
#include "ProfileMetrics.h"
#include "ModelBatch.h"

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

// Ordered mesh instances share one geometry atlas and one draw per batch.
class Renderer
{
  public:

    struct BatchSample
    {
        uint64_t frame, batch, instances, triangles, bytes;
        const char* stage;
        const char* reason;
        double submitMs;
    };

    struct FrameStatistics
    {
        uint64_t frame = 0, worldDraws = 0, hudDraws = 0, triangles = 0, gpuSkipped = 0;
    };

    const FrameStatistics& FrameStats() const
    {
        return frameStats_;
    }

    const std::vector<BatchSample>& Batches() const
    {
        return batches_;
    }

    std::vector<GpuProfiler::Sample> CollectGpuSamples()
    {
        return gpuProfiler_.Collect();
    }

    bool GpuTimingAvailable() const
    {
        return gpuProfiler_.Available();
    }

    size_t PendingGpuSamples() const
    {
        return gpuProfiler_.Pending();
    }

    size_t MeshCount() const
    {
        return meshes_.Count();
    }

    size_t MeshBytes() const
    {
        return meshes_.Bytes();
    }

    size_t MeshCapacityBytes() const
    {
        return meshes_.CapacityBytes();
    }

    size_t MeshLimitBytes() const
    {
        return meshes_.LimitBytes();
    }

    size_t InstanceLimit() const
    {
        return maxInstances_;
    }

    size_t TriangleLimit() const
    {
        return MaxBatchTriangles;
    }

    bool DiskCacheEnabled() const
    {
        return meshes_.DiskEnabled();
    }

    const PostProcessor& PostProcessorStats() const
    {
        return postProcessor_;
    }

    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool IsInitialized() const
    {
        return program_ != 0 && vao_ != 0 && vbo_ != 0 && instanceTexture_ != 0
               && triangle_ != nullptr && quad_ != nullptr;
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

    const MeshCache::Statistics& CacheStats() const
    {
        return meshes_.Stats();
    }

    std::uint64_t InstanceCount() const
    {
        return instanceCount_;
    }

    std::uint64_t InstanceUploadBytes() const
    {
        return instanceUploadBytes_;
    }

    // On a cache hit, enqueue the mesh and return false. On a miss, submit local
    // coordinates until EndCachedMesh(). Origin/scale apply at draw time only.
    // Do not nest recordings; the key must identify geometry AND color state.
    bool BeginCachedMesh(const std::string& key, Point origin, float scale);
    void EndCachedMesh();
    // Enqueue another instance of a stored mesh. False means the key is not cached yet.
    bool DrawCachedMesh(const std::string& key,
                        Point origin,
                        Point xAxis,
                        Point yAxis,
                        Color tint = {});

    // GLB Y-up metres -> the same 2.5D projection as world actors. Whole modular
    // appearances are reserved atomically and share one painter-depth group.
    bool DrawModel(const ModelAppearance& appearance,
                   Point origin,
                   ModelMath::Vec3 scale,
                   float yaw = 0,
                   const std::string& clip = "Idle",
                   float seconds = 0,
                   bool loop = true,
                   float emissionScale = 1,
                   Color tint = {});

    PostProcessingSettings& PostEffects()
    {
        return postProcessor_.settings;
    }

    const PostProcessingSettings& PostEffects() const
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

    void Flush(const char* reason);
    const std::vector<Point>& CirclePoints(int segments);
    const MeshCache::Mesh* GlyphMesh(wchar_t character);
    using Vertex = MeshVertex;

    struct MeshInstance
    {
        float x, y, xx, xy;
        float yx, yy, reserved0 = 0, reserved1 = 0;
        float r, g, b, a;
        float emission, reserved2 = 0, reserved3 = 0, reserved4 = 0;
    };

    struct TriangleReference
    {
        uint32_t firstVertex, instance;
    };

    void Submit(const MeshCache::Mesh& mesh, Point origin, Point xAxis, Point yAxis, Color tint);
    void RecordTriangle(Point a, Point b, Point c, Color color);

    int width_, height_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLuint instanceBuffer_ = 0, instanceTexture_ = 0;
    GLint viewport_ = -1, linearScene_ = -1, depthStep_ = -1;
    bool capturing_ = false, overlay_ = false;
    std::uint64_t drawCalls_ = 0;
    std::uint64_t instanceCount_ = 0, instanceUploadBytes_ = 0;
    PostProcessor postProcessor_;
    MeshCache meshes_;
    ModelBatch models_;
    std::vector<ModelBatch::Surface> modelSurfaces_;
    std::vector<MeshInstance> instances_;
    std::vector<TriangleReference> triangles_;
    const MeshCache::Mesh* triangle_ = nullptr;
    const MeshCache::Mesh* quad_ = nullptr;
    size_t maxInstances_ = 16384;
    static constexpr size_t MaxBatchTriangles = 1024 * 1024;
    bool recording_ = false;
    std::string recordingKey_;
    Point recordingOrigin_;
    float recordingScale_ = 1;
    std::vector<Vertex> recordedVertices_;
    std::map<int, std::vector<Point>> circles_;
    FontGlyphCache font_;
    FrameStatistics frameStats_;
    std::vector<BatchSample> batches_;
    GpuProfiler gpuProfiler_;
};
