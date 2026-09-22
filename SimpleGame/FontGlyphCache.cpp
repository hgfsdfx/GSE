#include "stdafx.h"
#include "FontGlyphCache.h"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <utility>

#pragma comment(lib, "Gdi32.lib")

struct FontGlyphCache::Impl
{
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previousFont = nullptr;
    TEXTMETRICW metrics{};
    std::map<wchar_t, Glyph> glyphs;

    Impl()
    {
        dc = CreateCompatibleDC(nullptr);
        if (!dc)
        {
            std::cerr << "한글 글꼴용 그리기 컨텍스트를 생성하지 못했습니다.\n";
            return;
        }
        for (const wchar_t* face : {L"Malgun Gothic", L"Gulim"})
        {
            HFONT candidate = CreateFontW(-24,
                                          0,
                                          0,
                                          0,
                                          FW_NORMAL,
                                          FALSE,
                                          FALSE,
                                          FALSE,
                                          HANGEUL_CHARSET,
                                          OUT_TT_PRECIS,
                                          CLIP_DEFAULT_PRECIS,
                                          ANTIALIASED_QUALITY,
                                          DEFAULT_PITCH,
                                          face);
            if (!candidate)
            {
                continue;
            }
            const auto previous = SelectObject(dc, candidate);
            WORD index = 0xffff;
            const bool supported =
                previous && previous != HGDI_ERROR
                && GetGlyphIndicesW(dc, L"가", 1, &index, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR
                && index != 0xffff && GetTextMetricsW(dc, &metrics);
            if (supported)
            {
                font = candidate;
                previousFont = previous;
                break;
            }
            if (previous && previous != HGDI_ERROR)
            {
                SelectObject(dc, previous);
            }
            DeleteObject(candidate);
        }
        if (!font)
        {
            std::cerr
                << "한글 글꼴을 찾지 못했습니다. Windows의 맑은 고딕 또는 굴림이 필요합니다.\n";
        }
    }

    ~Impl()
    {
        if (previousFont)
        {
            SelectObject(dc, previousFont);
        }
        if (font)
        {
            DeleteObject(font);
        }
        if (dc)
        {
            DeleteDC(dc);
        }
    }
};

FontGlyphCache::FontGlyphCache()
    : impl_(std::make_unique<Impl>())
{
}

FontGlyphCache::~FontGlyphCache() = default;

std::wstring FontGlyphCache::Decode(const std::string& utf8)
{
    if (utf8.empty() || utf8.size() > size_t(std::numeric_limits<int>::max()))
    {
        return {};
    }
    const int length = static_cast<int>(utf8.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, nullptr, 0);
    std::wstring decoded(count, L'\0');
    if (count)
    {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, decoded.data(), count);
    }
    // The UI uses BMP characters. Display one replacement glyph for a supplementary code point.
    std::wstring result;
    result.reserve(decoded.size());
    for (size_t i = 0; i < decoded.size(); ++i)
    {
        const wchar_t ch = decoded[i];
        if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < decoded.size() && decoded[i + 1] >= 0xdc00
            && decoded[i + 1] <= 0xdfff)
        {
            result.push_back(L'\ufffd');
            ++i;
        }
        else
        {
            result.push_back(ch);
        }
    }
    return result;
}

const FontGlyphCache::Glyph& FontGlyphCache::Get(wchar_t character)
{
    const auto found = impl_->glyphs.find(character);
    if (found != impl_->glyphs.end())
    {
        return found->second;
    }
    Glyph glyph;
    MAT2 identity{};
    identity.eM11.value = 1;
    identity.eM22.value = 1;
    GLYPHMETRICS metrics{};
    const DWORD bytes = impl_->font ? GetGlyphOutlineW(impl_->dc,
                                                       character,
                                                       GGO_GRAY8_BITMAP,
                                                       &metrics,
                                                       0,
                                                       nullptr,
                                                       &identity)
                                    : GDI_ERROR;
    constexpr float unit = 10.f / 24.f;
    if (bytes != GDI_ERROR)
    {
        glyph.advance = float(metrics.gmCellIncX) * unit;
    }
    std::vector<unsigned char> bitmap(bytes == GDI_ERROR ? 0 : bytes);
    const bool ready = bytes != GDI_ERROR
                       && (bytes == 0
                           || GetGlyphOutlineW(impl_->dc,
                                               character,
                                               GGO_GRAY8_BITMAP,
                                               &metrics,
                                               bytes,
                                               bitmap.data(),
                                               &identity)
                                  != GDI_ERROR);
    if (ready)
    {
        const size_t stride = (size_t(metrics.gmBlackBoxX) + 3) & ~size_t(3);
        for (UINT row = 0; bytes && row < metrics.gmBlackBoxY; ++row)
        {
            for (UINT col = 0; col < metrics.gmBlackBoxX;)
            {
                const auto coverage = bitmap[row * stride + col];
                const UINT begin = col++;
                while (col < metrics.gmBlackBoxX && bitmap[row * stride + col] == coverage)
                {
                    ++col;
                }
                if (coverage)
                {
                    glyph.runs.push_back(
                        {(float(metrics.gmptGlyphOrigin.x) + begin) * unit,
                         (float(impl_->metrics.tmAscent - impl_->metrics.tmInternalLeading
                                - metrics.gmptGlyphOrigin.y)
                          + row)
                             * unit,
                         (col - begin) * unit,
                         unit,
                         std::min(1.f, coverage / 64.f)});
                }
            }
        }
    }
    else
    {
        // A visible missing-glyph box is preferable to silently dropping a character.
        glyph.runs = {{1, 1, 8, 1, 1}, {1, 9, 8, 1, 1}, {1, 2, 1, 7, 1}, {8, 2, 1, 7, 1}};
    }
    return impl_->glyphs.emplace(character, std::move(glyph)).first->second;
}
