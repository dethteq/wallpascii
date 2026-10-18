#pragma once

#include <string>
#include <vector>

#include "wallpascii/adjust.h"
#include "wallpascii/color.h"
#include "wallpascii/image.h"
#include "wallpascii/layout.h"

namespace wallpascii {

// Characters ordered from sparsest (least ink) to densest.
struct Charset {
    std::vector<char32_t> chars;
};

enum class CharsetPreset { Simple, Detailed, Blocks };

Charset charsetPreset(CharsetPreset preset);
// Builds a custom charset from UTF-8 text, sparse to dense. Empty if no chars.
Charset charsetFromUtf8(const std::string& utf8);

// Size of one character cell in output pixels. Comes from the font metrics
// of the shell (Core Text / DirectWrite) or from the built-in bitmap font.
struct CellSize {
    int width = 0;
    int height = 0;
};

// A grid of characters covering the output area. The grid is centered;
// leftover pixels (area not divisible by cell size) become margins.
struct GlyphGrid {
    int cols = 0;
    int rows = 0;
    int originX = 0;  // pixel offset of the first cell in the area
    int originY = 0;
    CellSize cell;
    std::vector<char32_t> cells;  // rows * cols, U+0020 = empty
    // Original color mode: the photo color of each cell (rows * cols) and how
    // to apply it. Empty in Mono mode; then everything uses the glyph color.
    ColorSettings color;
    std::vector<CellColor> colors;

    char32_t at(int col, int row) const { return cells[size_t(row) * cols + col]; }
    // Rows joined with '\n', UTF-8 encoded.
    std::string toUtf8() const;
};

enum class TextMode {
    Ascii,        // brightness picks a character from the charset
    TextAsImage,  // the user's text repeats over the grid, dark cells become spaces
};

struct GridSettings {
    int areaWidth = 0;   // output pixels
    int areaHeight = 0;
    FitMode fit = FitMode::Fill;
    Adjustments adjust;
    CellSize cell;
    TextMode mode = TextMode::Ascii;
    Charset charset = charsetPreset(CharsetPreset::Simple);
    std::string text;            // UTF-8, used by TextAsImage
    float textThreshold = 0.5f;  // TextAsImage: cells darker than this are spaces
    ColorSettings color;
};

// Per-cell measurement of the photo: adjusted brightness (0..1), how much
// of the cell is covered by opaque photo pixels (0..1) and, in Original color
// mode, the average photo color (unadjusted; gray for photos without color).
struct CellSample {
    float brightness = 1.0f;
    float coverage = 0.0f;
    Color color{255, 255, 255};
};

struct CellSamples {
    int cols = 0;
    int rows = 0;
    int originX = 0;
    int originY = 0;
    std::vector<CellSample> samples;  // rows * cols
};

CellSamples sampleCells(const GrayImage& img, const GridSettings& s);

GlyphGrid buildGrid(const GrayImage& img, const GridSettings& s);

}  // namespace wallpascii
