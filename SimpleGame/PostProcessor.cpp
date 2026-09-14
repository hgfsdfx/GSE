#include "stdafx.h"
#include "PostProcessor.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <iostream>

PostProcessor::~PostProcessor()
{
    ReleaseTargets();
    if (vao_)
    {
        glDeleteVertexArrays(1, &vao_);
    }
    if (downsampleProgram_)
    {
        glDeleteProgram(downsampleProgram_);
    }
    if (blurProgram_)
    {
        glDeleteProgram(blurProgram_);
    }
    if (compositeProgram_)
    {
        glDeleteProgram(compositeProgram_);
    }
}

bool PostProcessor::Initialize(const std::filesystem::path& folder)
{
    downsampleProgram_ = LoadShaderProgram(folder / L"Fullscreen.vs", folder / L"Downsample.fs");
    blurProgram_ = LoadShaderProgram(folder / L"Fullscreen.vs", folder / L"GaussianBlur.fs");
    compositeProgram_ = LoadShaderProgram(folder / L"Fullscreen.vs", folder / L"Composite.fs");
    if (!downsampleProgram_ || !blurProgram_ || !compositeProgram_)
    {
        std::cerr << "Post-processing unavailable. Using direct rendering.\n";
        return false;
    }
    glGenVertexArrays(1, &vao_);
    down_ = {glGetUniformLocation(downsampleProgram_, "u_Source"),
             glGetUniformLocation(downsampleProgram_, "u_Texel"),
             glGetUniformLocation(downsampleProgram_, "u_Extract"),
             glGetUniformLocation(downsampleProgram_, "u_Threshold"),
             glGetUniformLocation(downsampleProgram_, "u_Knee")};
    blur_ = {glGetUniformLocation(blurProgram_, "u_Source"),
             glGetUniformLocation(blurProgram_, "u_Direction")};
    composite_ = {glGetUniformLocation(compositeProgram_, "u_Scene"),
                  glGetUniformLocation(compositeProgram_, "u_Bloom"),
                  glGetUniformLocation(compositeProgram_, "u_SoftScene"),
                  glGetUniformLocation(compositeProgram_, "u_Exposure"),
                  glGetUniformLocation(compositeProgram_, "u_BloomStrength"),
                  glGetUniformLocation(compositeProgram_, "u_VignetteStrength"),
                  glGetUniformLocation(compositeProgram_, "u_VignetteRange"),
                  glGetUniformLocation(compositeProgram_, "u_EdgeStrength"),
                  glGetUniformLocation(compositeProgram_, "u_EdgeRange")};
    return vao_ != 0;
}

bool PostProcessor::MakeTarget(Target& target, int width, int height)
{
    glGenFramebuffers(1, &target.framebuffer);
    glGenTextures(1, &target.texture);
    if (!target.framebuffer || !target.texture)
    {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "HDR framebuffer incomplete: " << status << " (" << width << 'x' << height
                  << ")\n";
        return false;
    }
    return true;
}

void PostProcessor::ReleaseTargets()
{
    ready_ = false;
    for (Target* target : {&scene_, &bloom_[0], &bloom_[1], &softScene_[0], &softScene_[1]})
    {
        if (target->framebuffer)
        {
            glDeleteFramebuffers(1, &target->framebuffer);
        }
        if (target->texture)
        {
            glDeleteTextures(1, &target->texture);
        }
        *target = {};
    }
}

void PostProcessor::Resize(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (ready_ && width_ == width && height_ == height)
    {
        return;
    }
    ReleaseTargets();
    width_ = width;
    height_ = height;
    halfWidth_ = std::max(1, (width + 1) / 2);
    halfHeight_ = std::max(1, (height + 1) / 2);
    if (!downsampleProgram_ || !blurProgram_ || !compositeProgram_ || !vao_)
    {
        return;
    }
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (width > maxSize || height > maxSize)
    {
        std::cerr << "Window exceeds HDR texture size limit. Using direct rendering.\n";
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    bool success = MakeTarget(scene_, width_, height_);
    for (Target* target : {&bloom_[0], &bloom_[1], &softScene_[0], &softScene_[1]})
    {
        if (success)
        {
            success = MakeTarget(*target, halfWidth_, halfHeight_);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!success)
    {
        ReleaseTargets();
        std::cerr << "Cannot allocate HDR targets. Using direct rendering.\n";
        return;
    }
    ready_ = true;
}

bool PostProcessor::BeginScene()
{
    const bool capture = ready_ && settings.enabled;
    glBindFramebuffer(GL_FRAMEBUFFER, capture ? scene_.framebuffer : 0);
    glViewport(0, 0, width_, height_);
    // All HDR passes are linear; the composite shader encodes the output once.
    glDisable(GL_FRAMEBUFFER_SRGB);
    return capture;
}

void PostProcessor::BindTarget(const Target& target, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, width, height);
}

void PostProcessor::DrawTriangle()
{
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcessor::Downsample(Target& destination, bool extract)
{
    BindTarget(destination, halfWidth_, halfHeight_);
    glUseProgram(downsampleProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_.texture);
    glUniform1i(down_.source, 0);
    glUniform2f(down_.texel, 1.f / width_, 1.f / height_);
    glUniform1i(down_.extract, extract ? 1 : 0);
    glUniform1f(down_.threshold, std::clamp(settings.bloomThreshold, .01f, 10.f));
    glUniform1f(down_.knee, std::clamp(settings.bloomKnee, .0001f, 5.f));
    DrawTriangle();
}

void PostProcessor::Blur(Target (&targets)[2], int iterations, float radius)
{
    glUseProgram(blurProgram_);
    glUniform1i(blur_.source, 0);
    glActiveTexture(GL_TEXTURE0);
    radius = std::clamp(radius, .25f, 6.f);
    for (int pass = 0; pass < iterations; ++pass)
    {
        BindTarget(targets[1], halfWidth_, halfHeight_);
        glBindTexture(GL_TEXTURE_2D, targets[0].texture);
        glUniform2f(blur_.direction, radius / halfWidth_, 0);
        DrawTriangle();
        BindTarget(targets[0], halfWidth_, halfHeight_);
        glBindTexture(GL_TEXTURE_2D, targets[1].texture);
        glUniform2f(blur_.direction, 0, radius / halfHeight_);
        DrawTriangle();
    }
}

void PostProcessor::Present()
{
    if (!ready_)
    {
        return;
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    const float bloomStrength = settings.bloom ? std::clamp(settings.bloomStrength, 0.f, 2.f) : 0;
    const float edgeStrength =
        settings.edgeBlur ? std::clamp(settings.edgeBlurStrength, 0.f, 1.f) : 0;
    if (bloomStrength > 0)
    {
        Downsample(bloom_[0], true);
        Blur(bloom_, 3, settings.bloomRadius);
    }
    if (edgeStrength > 0)
    {
        Downsample(softScene_[0], false);
        Blur(softScene_, 2, settings.edgeBlurRadius);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glUseProgram(compositeProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_.texture);
    // Valid scene fallback avoids reading uninitialized filter targets when disabled.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomStrength > 0 ? bloom_[0].texture : scene_.texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, edgeStrength > 0 ? softScene_[0].texture : scene_.texture);
    glUniform1i(composite_.scene, 0);
    glUniform1i(composite_.bloom, 1);
    glUniform1i(composite_.softScene, 2);
    glUniform1f(composite_.exposure, std::clamp(settings.exposure, .25f, 4.f));
    glUniform1f(composite_.bloomStrength, bloomStrength);
    glUniform1f(composite_.edgeStrength, edgeStrength);
    const float edgeStart = std::clamp(settings.edgeBlurStart, 0.f, 1.39f);
    glUniform2f(composite_.edgeRange,
                edgeStart,
                std::clamp(settings.edgeBlurEnd, edgeStart + .01f, 1.5f));
    glUniform1f(composite_.vignetteStrength,
                settings.vignette ? std::clamp(settings.vignetteStrength, 0.f, .95f) : 0);
    const float vignetteStart = std::clamp(settings.vignetteStart, 0.f, 1.39f);
    glUniform2f(composite_.vignetteRange,
                vignetteStart,
                std::clamp(settings.vignetteEnd, vignetteStart + .01f, 1.5f));
    DrawTriangle();
    for (int i = 2; i >= 0; --i)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
