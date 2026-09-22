#pragma once
#include <memory>
#include <string>
#include <vector>

// Rasterize a Unicode glyph once, then reuse its coverage runs in the existing mesh batch.
class FontGlyphCache
{
  public:

    struct Run
    {
        float x, y, width, height, coverage;
    };

    struct Glyph
    {
        float advance = 10;
        std::vector<Run> runs;
    };

    FontGlyphCache();
    ~FontGlyphCache();
    FontGlyphCache(const FontGlyphCache&) = delete;
    FontGlyphCache& operator=(const FontGlyphCache&) = delete;
    const Glyph& Get(wchar_t character);
    static std::wstring Decode(const std::string& utf8);
    static constexpr float LineHeight = 14;

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
