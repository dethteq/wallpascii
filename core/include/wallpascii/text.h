#pragma once

#include <string>

#include "wallpascii/ascii.h"

namespace wallpascii {

enum class TextAlign { Left, Center, Right };

// Lays out a text block for side-by-side mode on a grid covering the area
// (same geometry as buildGrid: whole cells, centered, leftover as margins).
// Explicit newlines start new lines, other lines wrap at spaces, and words
// longer than a line are split. The block is centered vertically; lines that
// do not fit are dropped from the end. `paddingCells` empty columns are kept
// on the left and right so the text does not touch the picture.
GlyphGrid layoutText(const std::string& utf8, int areaWidth, int areaHeight, CellSize cell,
                     TextAlign align, int paddingCells = 2);

}  // namespace wallpascii
