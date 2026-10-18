#pragma once

#include <CoreText/CoreText.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "wallpascii/render.h"

// Draws glyphs with a system font through Core Text. Each distinct character
// is rasterized once into an 8-bit coverage mask and then blended into the
// wallpaper, so a grid of thousands of cells costs only a handful of
// Core Text calls. Characters missing from the font fall back to another
// system font (Core Text font cascade); shade blocks use the core's dither
// patterns so they tile like in the CLI. Not thread-safe: use one instance
// per thread.
class CoreTextRasterizer : public wallpascii::GlyphRasterizer {
public:
    // `fontName` is a family shown in the UI ("SF Mono", "Menlo", ...);
    // `sizePx` is the font size in output pixels.
    CoreTextRasterizer(const std::string& fontName, double sizePx);
    ~CoreTextRasterizer() override;
    CoreTextRasterizer(const CoreTextRasterizer&) = delete;
    CoreTextRasterizer& operator=(const CoreTextRasterizer&) = delete;

    wallpascii::CellSize cellSize() const override { return cell_; }
    void drawGlyph(wallpascii::RgbImage& target, int x, int y, char32_t ch,
                   wallpascii::Color fg) const override;

private:
    const std::vector<uint8_t>& mask(char32_t ch) const;

    CTFontRef font_ = nullptr;
    wallpascii::CellSize cell_;
    double descent_ = 0;
    mutable std::unordered_map<char32_t, std::vector<uint8_t>> masks_;
};
