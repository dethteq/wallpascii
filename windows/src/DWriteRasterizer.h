#pragma once

#include <windows.h>

#include <dwrite_2.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "wallpascii/render.h"

// The process-wide DirectWrite factory (shared, thread-safe).
IDWriteFactory2* dwriteFactory();

// True when the system font collection has this family.
bool hasFontFamily(const std::wstring& family);

// Draws glyphs with a system font through DirectWrite. Each distinct
// character is rasterized once into an 8-bit coverage mask and then blended
// into the wallpaper, so a grid of thousands of cells costs only a handful of
// DirectWrite calls. Characters missing from the font fall back to another
// system font (DirectWrite font fallback); shade blocks use the core's dither
// patterns so they tile like in the CLI. Not thread-safe: use one instance
// per thread.
class DWriteRasterizer : public wallpascii::GlyphRasterizer {
public:
    // `family` is a font family shown in the UI ("Cascadia Mono", "Consolas", ...);
    // `sizePx` is the font size in output pixels.
    DWriteRasterizer(const std::wstring& family, double sizePx);

    wallpascii::CellSize cellSize() const override { return cell_; }
    void drawGlyph(wallpascii::RgbImage& target, int x, int y, char32_t ch,
                   wallpascii::Color fg) const override;

private:
    const std::vector<uint8_t>& mask(char32_t ch) const;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
    wallpascii::CellSize cell_;
    double ascent_ = 0;
    mutable std::unordered_map<char32_t, std::vector<uint8_t>> masks_;
};
