#include "DWriteRasterizer.h"

#include <algorithm>
#include <cmath>
#include <functional>

using Microsoft::WRL::ComPtr;
using namespace wallpascii;

IDWriteFactory2* dwriteFactory() {
    static IDWriteFactory2* factory = [] {
        IDWriteFactory2* f = nullptr;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                            reinterpret_cast<IUnknown**>(&f));
        return f;
    }();
    return factory;
}

namespace {

ComPtr<IDWriteFont> findFont(const std::wstring& family) {
    ComPtr<IDWriteFontCollection> fonts;
    if (!dwriteFactory() || FAILED(dwriteFactory()->GetSystemFontCollection(&fonts))) return nullptr;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(fonts->FindFamilyName(family.c_str(), &index, &exists)) || !exists) return nullptr;
    ComPtr<IDWriteFontFamily> fam;
    ComPtr<IDWriteFont> font;
    if (FAILED(fonts->GetFontFamily(index, &fam)) ||
        FAILED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, &font)))
        return nullptr;
    return font;
}

using RunCallback = std::function<void(float x, const DWRITE_GLYPH_RUN* run)>;

// Collects the glyph runs of a text layout, fallback fonts included, instead
// of drawing them. Lives on the stack, so reference counting is a no-op.
class RunCollector final : public IDWriteTextRenderer {
public:
    explicit RunCollector(RunCallback cb) : cb_(std::move(cb)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
            riid == __uuidof(IDWriteTextRenderer)) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override {
        *m = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) override {
        *ppd = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT, DWRITE_MEASURING_MODE,
                                           const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        cb_(x, run);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*,
                                                IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL,
                                               IUnknown*) override {
        return S_OK;
    }

private:
    RunCallback cb_;
};

}  // namespace

bool hasFontFamily(const std::wstring& family) { return findFont(family) != nullptr; }

DWriteRasterizer::DWriteRasterizer(const std::wstring& family, double sizePx) {
    const float size = float(std::max(4.0, sizePx));
    std::wstring name = family;
    ComPtr<IDWriteFont> font = findFont(name);
    for (const wchar_t* fallback : {L"Consolas", L"Courier New"}) {
        if (font) break;
        name = fallback;
        font = findFont(name);
    }

    // Monospace cell: the advance of '0' by the line height without leading,
    // so ASCII art rows sit tight like in a terminal.
    double advance = size * 0.6, ascent = size * 0.8, descent = size * 0.2;
    ComPtr<IDWriteFontFace> face;
    if (font && SUCCEEDED(font->CreateFontFace(&face))) {
        DWRITE_FONT_METRICS fm{};
        face->GetMetrics(&fm);
        const double scale = size / std::max<UINT16>(1, fm.designUnitsPerEm);
        ascent = fm.ascent * scale;
        descent = fm.descent * scale;
        const UINT32 zero = '0';
        UINT16 glyph = 0;
        DWRITE_GLYPH_METRICS gm{};
        if (SUCCEEDED(face->GetGlyphIndices(&zero, 1, &glyph)) && glyph &&
            SUCCEEDED(face->GetDesignGlyphMetrics(&glyph, 1, &gm)))
            advance = gm.advanceWidth * scale;
    }
    ascent_ = ascent;
    cell_.width = std::max(1, int(std::lround(advance)));
    cell_.height = std::max(1, int(std::ceil(ascent + descent)));

    if (dwriteFactory())
        dwriteFactory()->CreateTextFormat(name.c_str(), nullptr, DWRITE_FONT_WEIGHT_REGULAR,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                          L"en-us", &format_);
}

const std::vector<uint8_t>& DWriteRasterizer::mask(char32_t ch) const {
    auto it = masks_.find(ch);
    if (it != masks_.end()) return it->second;

    const int w = cell_.width, h = cell_.height;
    std::vector<uint8_t> m(size_t(w) * h, 0);

    wchar_t units[2];
    UINT32 count = 1;
    if (ch > 0xFFFF) {
        const char32_t v = ch - 0x10000;
        units[0] = wchar_t(0xD800 + (v >> 10));
        units[1] = wchar_t(0xDC00 + (v & 0x3FF));
        count = 2;
    } else {
        units[0] = wchar_t(ch);
    }

    // A one-character layout: DirectWrite picks a fallback font when needed.
    ComPtr<IDWriteTextLayout> layout;
    if (format_ && SUCCEEDED(dwriteFactory()->CreateTextLayout(units, count, format_.Get(), 10000.0f,
                                                                10000.0f, &layout))) {
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        // Center glyphs that are narrower or wider than the cell (fallback fonts).
        const float shift = (w - tm.widthIncludingTrailingWhitespace) / 2;
        RunCollector collector([&](float x, const DWRITE_GLYPH_RUN* run) {
            ComPtr<IDWriteGlyphRunAnalysis> analysis;
            if (FAILED(dwriteFactory()->CreateGlyphRunAnalysis(
                    run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
                    DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, x + shift,
                    float(ascent_), &analysis)))
                return;
            RECT b{};
            if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &b)) || b.right <= b.left ||
                b.bottom <= b.top)
                return;
            const int bw = b.right - b.left, bh = b.bottom - b.top;
            std::vector<uint8_t> tex(size_t(bw) * bh);
            if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &b, tex.data(),
                                                    UINT32(tex.size()))))
                return;
            for (int y = std::max<LONG>(0, b.top); y < std::min<LONG>(h, b.bottom); ++y)
                for (int x = std::max<LONG>(0, b.left); x < std::min<LONG>(w, b.right); ++x) {
                    uint8_t& dst = m[size_t(y) * w + x];
                    dst = std::max(dst, tex[size_t(y - b.top) * bw + (x - b.left)]);
                }
        });
        layout->Draw(nullptr, &collector, 0, 0);
    }
    return masks_.emplace(ch, std::move(m)).first->second;
}

void DWriteRasterizer::drawGlyph(RgbImage& target, int x0, int y0, char32_t ch, Color fg) const {
    if (drawShadeBlock(target, x0, y0, cell_, ch, fg)) return;

    const std::vector<uint8_t>& m = mask(ch);
    const int w = cell_.width, h = cell_.height;
    for (int y = 0; y < h; ++y) {
        const int ty = y0 + y;
        if (ty < 0 || ty >= target.height) continue;
        const uint8_t* row = &m[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            const int a = row[x];
            const int tx = x0 + x;
            if (a == 0 || tx < 0 || tx >= target.width) continue;
            uint8_t* p = &target.pixels[(size_t(ty) * target.width + tx) * 3];
            p[0] = uint8_t(p[0] + (int(fg.r) - p[0]) * a / 255);
            p[1] = uint8_t(p[1] + (int(fg.g) - p[1]) * a / 255);
            p[2] = uint8_t(p[2] + (int(fg.b) - p[2]) * a / 255);
        }
    }
}
