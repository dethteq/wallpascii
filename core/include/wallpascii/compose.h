#pragma once

#include "wallpascii/ascii.h"
#include "wallpascii/image.h"
#include "wallpascii/layout.h"
#include "wallpascii/render.h"
#include "wallpascii/text.h"

namespace wallpascii {

// Everything that defines one wallpaper, shared by the CLI and the apps.
struct WallpaperSettings {
    int width = 2560;
    int height = 1440;
    // Fit, adjustments, charset, mode and text. The area and cell size are
    // filled in by compose().
    GridSettings grid;
    // Side by side: the picture takes one half, `grid.text` the other.
    bool sideBySide = false;
    SideLayout layout = SideLayout::ImageLeft;
    TextAlign align = TextAlign::Center;
};

// The laid-out wallpaper: the picture grid and, in side-by-side mode, the
// text grid, each with the area it occupies in output pixels.
struct Composition {
    RectI imageArea;
    GlyphGrid image;
    RectI textArea;
    GlyphGrid text;  // empty unless side by side
};

// `imageCell` and `textCell` come from the rasterizers that will draw them.
Composition compose(const GrayImage& photo, const WallpaperSettings& s, CellSize imageCell,
                    CellSize textCell);

RgbImage render(const Composition& c, int width, int height, const RenderColors& colors,
                const GlyphRasterizer& imageRasterizer, const GlyphRasterizer& textRasterizer);

}  // namespace wallpascii
