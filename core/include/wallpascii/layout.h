#pragma once

namespace wallpascii {

// Same meaning as in the macOS / Windows wallpaper settings.
enum class FitMode { Fill, Fit, Stretch, Center };

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
};

// Where the photo lands inside the target area.
// `dest` is in area pixels, `source` is the part of the photo shown there.
// Parts of the area outside `dest` stay empty (rendered as background).
struct Placement {
    RectF dest;
    RectF source;
};

Placement computePlacement(int imageW, int imageH, int areaW, int areaH, FitMode mode);

struct RectI {
    int x = 0, y = 0, w = 0, h = 0;
};

// Side-by-side mode: where the ASCII picture goes; the text takes the other half.
enum class SideLayout { ImageLeft, ImageRight, ImageTop, ImageBottom };

struct SplitAreas {
    RectI image;
    RectI text;
};

// Splits the output 50/50. With an odd size the extra pixel goes to the text half.
SplitAreas splitArea(int areaW, int areaH, SideLayout layout);

}  // namespace wallpascii
