#include "wallpascii/render.h"

#include <algorithm>
#include <cmath>

#include "font8x8_basic.h"

namespace wallpascii {

BitmapRasterizer::BitmapRasterizer(int fontSizePx) {
    const int size = std::max(fontSizePx, 4);
    cell_.width = std::max(3, int(std::lround(size * 0.6)));
    cell_.height = std::max(4, int(std::lround(size * 1.2)));
}

bool drawShadeBlock(RgbImage& target, int x0, int y0, CellSize cell, char32_t ch, Color fg) {
    if (!((ch >= 0x2591 && ch <= 0x2593) || ch == 0x2588)) return false;
    for (int y = 0; y < cell.height; ++y) {
        for (int x = 0; x < cell.width; ++x) {
            const int ax = x0 + x, ay = y0 + y;
            if (ax < 0 || ay < 0 || ax >= target.width || ay >= target.height) continue;
            bool on;
            switch (ch) {
            case 0x2591: on = (ax % 2 == 0) && (ay % 2 == 0); break;    // ░ 25%
            case 0x2592: on = (ax + ay) % 2 == 0; break;                // ▒ 50%
            case 0x2593: on = !((ax % 2 == 1) && (ay % 2 == 1)); break;  // ▓ 75%
            default: on = true; break;                                 // █
            }
            if (on) target.set(ax, ay, fg);
        }
    }
    return true;
}

void BitmapRasterizer::drawGlyph(RgbImage& target, int x0, int y0, char32_t ch, Color fg) const {
    const int cw = cell_.width, chh = cell_.height;
    auto put = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < target.width && y < target.height) target.set(x, y, fg);
    };

    if (drawShadeBlock(target, x0, y0, cell_, ch, fg)) return;

    // The 8x8 glyph is stretched over the full cell width and ~70% of its height,
    // vertically centered, which approximates a monospace font's proportions.
    const int gh = std::max(1, int(std::lround(chh * 0.7)));
    const int gy = (chh - gh) / 2;

    const unsigned char* rows = nullptr;
    if (ch < 128) rows = font8x8_basic[ch];

    for (int y = 0; y < gh; ++y) {
        const int by = y * 8 / gh;
        for (int x = 0; x < cw; ++x) {
            const int bx = x * 8 / cw;
            bool on;
            if (rows) {
                on = (rows[by] >> bx) & 1;  // bit 0 is the leftmost pixel
            } else {
                // No glyph in the bitmap font: draw a hollow box ("tofu").
                on = (bx == 0 || bx == 7 || by == 0 || by == 7);
            }
            if (on) put(x0 + x, y0 + gy + y);
        }
    }
}

namespace {

void fillRect(RgbImage& target, int x0, int y0, int w, int h, Color c) {
    const int x1 = std::min(x0 + w, target.width), y1 = std::min(y0 + h, target.height);
    for (int y = std::max(y0, 0); y < y1; ++y)
        for (int x = std::max(x0, 0); x < x1; ++x) target.set(x, y, c);
}

// The glyph color that reads best on a filled cell: the darker of the two
// theme colors on light cells, the lighter one on dark cells.
Color contrastOn(Color tile, const RenderColors& colors) {
    const bool fgLighter = colorLuma(colors.glyph) >= colorLuma(colors.background);
    const Color light = fgLighter ? colors.glyph : colors.background;
    const Color dark = fgLighter ? colors.background : colors.glyph;
    return colorLuma(tile) > 140.0f ? dark : light;
}

}  // namespace

void drawGrid(RgbImage& target, const GlyphGrid& grid, int offsetX, int offsetY,
              const RenderColors& colors, const GlyphRasterizer& rasterizer) {
    const bool colored = grid.color.mode == ColorMode::Original && grid.colors.size() == grid.cells.size();
    const float strength = std::clamp(grid.color.strength, 0.0f, 1.0f);
    for (int r = 0; r < grid.rows; ++r) {
        for (int c = 0; c < grid.cols; ++c) {
            const char32_t ch = grid.at(c, r);
            const int x = offsetX + grid.originX + c * grid.cell.width;
            const int y = offsetY + grid.originY + r * grid.cell.height;
            Color fg = colors.glyph;
            if (colored) {
                const CellColor& cc = grid.colors[size_t(r) * grid.cols + c];
                if (cc.coverage > 0.0f && grid.color.target != ColorTarget::Glyphs) {
                    // Partly covered edge cells fade into the background.
                    const Color behind = grid.color.target == ColorTarget::Both
                                             ? mixColor({0, 0, 0}, cc.color, 0.3f)
                                             : cc.color;
                    const Color tile = mixColor(colors.background, behind, strength * cc.coverage);
                    fillRect(target, x, y, grid.cell.width, grid.cell.height, tile);
                    fg = grid.color.target == ColorTarget::Background ? contrastOn(tile, colors)
                                                                       : mixColor(colors.glyph, cc.color, strength);
                } else {
                    fg = mixColor(colors.glyph, cc.color, strength);
                }
            }
            if (ch == U' ') continue;
            rasterizer.drawGlyph(target, x, y, ch, fg);
        }
    }
}

RgbImage renderGrid(const GlyphGrid& grid, int width, int height, const RenderColors& colors,
                    const GlyphRasterizer& rasterizer) {
    RgbImage img(width, height, colors.background);
    drawGrid(img, grid, 0, 0, colors, rasterizer);
    return img;
}

}  // namespace wallpascii
