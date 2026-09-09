#pragma once
#include <filesystem>
#include "Dependencies/glew.h"

struct PostProcessingSettings {
    bool enabled = true;
    bool bloom = true;
    bool vignette = true;
    bool edgeBlur = true;
    float exposure = 1.15f;
    float bloomStrength = .38f;
    float bloomThreshold = 1.0f;
    float bloomKnee = .5f;
    float vignetteStrength = .40f;
    float vignetteStart = .35f;
    float vignetteEnd = 1.35f;
    float edgeBlurStrength = .80f;
    float edgeBlurStart = .48f;
    float edgeBlurEnd = 1.16f;
    float bloomRadius = 1.6f;       // In half-resolution texels.
    float edgeBlurRadius = 2.2f;    // In half-resolution texels.
};

// Linear RGBA16F scene -> half-resolution filters -> tone-mapped SDR output.
class PostProcessor {
public:
    PostProcessor() = default;
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    bool Initialize(const std::filesystem::path& shaderFolder);
    void Resize(int width, int height);
    bool BeginScene();
    void Present();
    bool Available() const { return ready_; }
    PostProcessingSettings settings;
private:
    struct Target { GLuint framebuffer = 0, texture = 0; };
    struct DownsampleUniforms { GLint source, texel, extract, threshold, knee; } down_{};
    struct BlurUniforms { GLint source, direction; } blur_{};
    struct CompositeUniforms {
        GLint scene, bloom, softScene, exposure, bloomStrength;
        GLint vignetteStrength, vignetteRange, edgeStrength, edgeRange;
    } composite_{};
    bool MakeTarget(Target& target, int width, int height);
    void ReleaseTargets();
    void Downsample(Target& destination, bool extract);
    void Blur(Target (&targets)[2], int iterations, float radius);
    void BindTarget(const Target& target, int width, int height);
    void DrawTriangle();
    Target scene_, bloom_[2], softScene_[2];
    GLuint downsampleProgram_ = 0, blurProgram_ = 0, compositeProgram_ = 0, vao_ = 0;
    int width_ = 1, height_ = 1, halfWidth_ = 1, halfHeight_ = 1;
    bool ready_ = false;
};
