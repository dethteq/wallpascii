#pragma once

#include "wallpascii/image.h"

namespace wallpascii {

enum class ColorMode {
    Mono,      // every glyph in the glyph color
    Original,  // colors come from the photo, cell by cell
};

// What the photo colors paint in Original mode.
enum class ColorTarget {
    Glyphs,      // the characters; the background stays plain
    Background,  // each cell is filled with its color, glyphs contrast on top
    Both,        // dimmed color behind, full color glyphs on top
};

struct ColorSettings {
    ColorMode mode = ColorMode::Mono;
    ColorTarget target = ColorTarget::Glyphs;
    float strength = 1.0f;    // 0..1: 0 = glyph/background colors, 1 = pure photo colors
    float saturation = 1.0f;  // 0..2: 0 = gray, 1 = as in the photo, 2 = twice as vivid
    float brightness = 0.0f;  // -1..1: darkens or lifts the photo colors
};

// Per-cell color of the photo, already adjusted by saturation and brightness.
// `coverage` is how much of the cell the photo covers (0..1).
struct CellColor {
    Color color;
    float coverage = 0.0f;
};

// Luma-preserving saturation, then brightness towards black or white.
Color adjustColor(Color c, float saturation, float brightness);
// Linear mix: t = 0 gives a, t = 1 gives b.
Color mixColor(Color a, Color b, float t);
// Rec. 709 luma, 0..255.
float colorLuma(Color c);

}  // namespace wallpascii
