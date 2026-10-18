#include "wallpascii/compose.h"

namespace wallpascii {

Composition compose(const GrayImage& photo, const WallpaperSettings& s, CellSize imageCell,
                    CellSize textCell) {
    Composition c;
    c.imageArea = {0, 0, s.width, s.height};
    if (s.sideBySide) {
        const SplitAreas split = splitArea(s.width, s.height, s.layout);
        c.imageArea = split.image;
        c.textArea = split.text;
        c.text = layoutText(s.grid.text, c.textArea.w, c.textArea.h, textCell, s.align);
    }
    GridSettings g = s.grid;
    g.areaWidth = c.imageArea.w;
    g.areaHeight = c.imageArea.h;
    g.cell = imageCell;
    c.image = buildGrid(photo, g);
    return c;
}

RgbImage render(const Composition& c, int width, int height, const RenderColors& colors,
                const GlyphRasterizer& imageRasterizer, const GlyphRasterizer& textRasterizer) {
    RgbImage img(width, height, colors.background);
    drawGrid(img, c.image, c.imageArea.x, c.imageArea.y, colors, imageRasterizer);
    drawGrid(img, c.text, c.textArea.x, c.textArea.y, colors, textRasterizer);
    return img;
}

}  // namespace wallpascii
