#pragma once

#include "wallpascii/ascii.h"
#include "wallpascii/image.h"

namespace wallpascii {

// Draws single glyphs. The native shells implement this with Core Text and
// DirectWrite; the core ships a bitmap-font version for the CLI and tests.
class GlyphRasterizer {
public:
    virtual ~GlyphRasterizer() = default;
    virtual CellSize cellSize() const = 0;
    // Draws `ch` into the cell whose top-left corner is (x, y).
    virtual void drawGlyph(RgbImage& target, int x, int y, char32_t ch, Color fg) const = 0;
};

// Built-in 8x8 bitmap font (ASCII) plus procedural shade blocks (░▒▓█),
// scaled to a cell of roughly 0.6 x 1.2 times the font size.
class BitmapRasterizer : public GlyphRasterizer {
public:
    explicit BitmapRasterizer(int fontSizePx);
    CellSize cellSize() const override { return cell_; }
    void drawGlyph(RgbImage& target, int x, int y, char32_t ch, Color fg) const override;

private:
    CellSize cell_;
};

// Shade blocks ░▒▓█ drawn as dither patterns over the whole cell, so they tile
// seamlessly whatever the font. Returns false for any other character.
bool drawShadeBlock(RgbImage& target, int x, int y, CellSize cell, char32_t ch, Color fg);

struct RenderColors {
    Color background{0, 0, 0};
    Color glyph{255, 255, 255};
};

// Draws each non-space glyph of the grid into `target`, shifted by (offsetX, offsetY).
// Used to put several grids (picture and text block) on one wallpaper.
// A grid with per-cell colors (Original color mode) mixes them with `colors`
// by its strength; with Background/Both targets it also fills the cells.
void drawGrid(RgbImage& target, const GlyphGrid& grid, int offsetX, int offsetY,
              const RenderColors& colors, const GlyphRasterizer& rasterizer);

// Renders the grid at full resolution: background fill, then each non-space glyph.
RgbImage renderGrid(const GlyphGrid& grid, int width, int height, const RenderColors& colors,
                    const GlyphRasterizer& rasterizer);

}  // namespace wallpascii
