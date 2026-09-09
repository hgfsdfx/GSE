#include "stdafx.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <Windows.h>

namespace {
float SRGBToLinear(float value) {
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}
// Original compact 5x7 bitmap alphabet; rendered as batched geometry.
const unsigned char letters[26][5] = {
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
    {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
    {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
    {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
    {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67}
};
const unsigned char digits[10][5] = {
    {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
    {24,20,18,127,16},{39,69,69,69,57},{62,73,73,73,50},{1,113,9,5,3},
    {54,73,73,73,54},{38,73,73,73,62}
};
}

Renderer::Renderer(int width, int height) : width_(width), height_(height) {
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) return;
    const auto folder = std::filesystem::path(executable).parent_path() / L"Shaders";
    program_ = LoadShaderProgram(folder / L"SolidRect.vs", folder / L"SolidRect.fs");
    if (!program_) return;
    viewport_ = glGetUniformLocation(program_, "u_Viewport");
    linearScene_ = glGetUniformLocation(program_, "u_LinearScene");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);
    vertices_.reserve(180000);
    postProcessor_.Initialize(folder);
    Resize(width, height);
}
Renderer::~Renderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
}
void Renderer::Resize(int width, int height) {
    width_ = std::max(1, width); height_ = std::max(1, height);
    postProcessor_.Resize(width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
}
void Renderer::Begin(Color c) {
    vertices_.clear();
    overlay_ = false;
    capturing_ = postProcessor_.BeginScene();
    if (capturing_) c = { SRGBToLinear(c.r), SRGBToLinear(c.g), SRGBToLinear(c.b), c.a };
    glClearColor(c.r, c.g, c.b, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
void Renderer::BeginOverlay() {
    if (overlay_) return;
    Flush();
    if (capturing_) postProcessor_.Present();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_FRAMEBUFFER_SRGB);
    overlay_ = true;
}
void Renderer::End() {
    if (!overlay_) BeginOverlay();
    Flush();
}
void Renderer::Flush() {
    if (!IsInitialized() || vertices_.empty()) return;
    glUseProgram(program_); glUniform2f(viewport_, float(width_), float(height_));
    glUniform1i(linearScene_, capturing_ && !overlay_ ? 1 : 0);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0);
    vertices_.clear();
}
void Renderer::Triangle(Point a, Point b, Point c, Color color) {
    for (Point p : { a, b, c }) vertices_.push_back({p.x, p.y, color.r, color.g, color.b, color.a, color.emission});
}
void Renderer::Quad(Point a, Point b, Point c, Point d, Color color) {
    Triangle(a, b, c, color); Triangle(a, c, d, color);
}
void Renderer::Rect(float x, float y, float w, float h, Color c) {
    Quad({x,y},{x+w,y},{x+w,y+h},{x,y+h},c);
}
void Renderer::Line(Point a, Point b, float width, Color c) {
    Point delta = b-a;
    float len = std::sqrt(delta.x*delta.x + delta.y*delta.y);
    if (len < 0.001f) return;
    Point normal(-delta.y / len * width * .5f, delta.x / len * width * .5f);
    Quad(a+normal,b+normal,b-normal,a-normal,c);
}
void Renderer::Circle(Point center, float radius, Color c, int segments) {
    for (int i=0;i<segments;++i) {
        float a = i * 6.2831853f / segments, b = (i+1) * 6.2831853f / segments;
        Triangle(center, center+Point(std::cos(a),std::sin(a))*radius,
            center+Point(std::cos(b),std::sin(b))*radius,c);
    }
}
void Renderer::Ring(Point center, float radius, float width, Color c) {
    for (int i=0;i<40;++i) {
        float a=i*6.2831853f/40,b=(i+1)*6.2831853f/40;
        Line(center+Point(std::cos(a),std::sin(a))*radius,
            center+Point(std::cos(b),std::sin(b))*radius,width,c);
    }
}
void Renderer::Text(float x, float y, const std::string& text, Color c, float scale) {
    const float start=x;
    for (unsigned char ch : text) {
        if (ch=='\n') { x=start; y+=10*scale; continue; }
        if (ch>='a' && ch<='z') ch-=32;
        unsigned char punctuation[5] = {};
        const unsigned char* glyph=punctuation;
        if(ch>='A'&&ch<='Z') glyph=letters[ch-'A'];
        else if(ch>='0'&&ch<='9') glyph=digits[ch-'0'];
        else switch(ch) {
            case '-': for(auto& v:punctuation)v=8; break;
            case '.': punctuation[2]=64; break;
            case ':': punctuation[2]=36; break;
            case '/': punctuation[0]=64;punctuation[1]=48;punctuation[2]=8;punctuation[3]=6;punctuation[4]=1;break;
            case '[': punctuation[1]=127;punctuation[2]=65;break;
            case ']': punctuation[2]=65;punctuation[3]=127;break;
            case '+': punctuation[1]=8;punctuation[2]=28;punctuation[3]=8;break;
            case '>': punctuation[1]=65;punctuation[2]=34;punctuation[3]=20;punctuation[4]=8;break;
            case '%': punctuation[0]=99;punctuation[1]=19;punctuation[2]=8;punctuation[3]=100;punctuation[4]=99;break;
        }
        for(int col=0;col<5;++col) for(int row=0;row<7;++row)
            if(glyph[col] & (1<<row)) Rect(x+col*scale,y+row*scale,scale,scale,c);
        x+=6*scale;
    }
}
