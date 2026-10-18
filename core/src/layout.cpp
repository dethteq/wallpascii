#include "wallpascii/layout.h"

#include <algorithm>

namespace wallpascii {

Placement computePlacement(int imageW, int imageH, int areaW, int areaH, FitMode mode) {
    Placement p;
    if (imageW <= 0 || imageH <= 0 || areaW <= 0 || areaH <= 0) return p;

    const float iw = float(imageW), ih = float(imageH);
    const float aw = float(areaW), ah = float(areaH);

    switch (mode) {
    case FitMode::Stretch:
        p.dest = {0, 0, aw, ah};
        p.source = {0, 0, iw, ih};
        break;

    case FitMode::Fill: {
        // Cover the whole area, crop the overflow evenly on both sides.
        const float scale = std::max(aw / iw, ah / ih);
        const float sw = aw / scale, sh = ah / scale;
        p.dest = {0, 0, aw, ah};
        p.source = {(iw - sw) / 2, (ih - sh) / 2, sw, sh};
        break;
    }

    case FitMode::Fit: {
        // Whole photo visible, empty bars on two sides.
        const float scale = std::min(aw / iw, ah / ih);
        const float dw = iw * scale, dh = ih * scale;
        p.dest = {(aw - dw) / 2, (ah - dh) / 2, dw, dh};
        p.source = {0, 0, iw, ih};
        break;
    }

    case FitMode::Center: {
        // Native size, centered; clipped where it is larger than the area.
        float dx = (aw - iw) / 2, dy = (ah - ih) / 2;
        float sx = 0, sy = 0, w = iw, h = ih;
        if (dx < 0) { sx = -dx; w = aw; dx = 0; }
        if (dy < 0) { sy = -dy; h = ah; dy = 0; }
        p.dest = {dx, dy, w, h};
        p.source = {sx, sy, w, h};
        break;
    }
    }
    return p;
}

SplitAreas splitArea(int areaW, int areaH, SideLayout layout) {
    SplitAreas a;
    const int halfW = areaW / 2, halfH = areaH / 2;
    switch (layout) {
    case SideLayout::ImageLeft:
        a.image = {0, 0, halfW, areaH};
        a.text = {halfW, 0, areaW - halfW, areaH};
        break;
    case SideLayout::ImageRight:
        a.text = {0, 0, areaW - halfW, areaH};
        a.image = {areaW - halfW, 0, halfW, areaH};
        break;
    case SideLayout::ImageTop:
        a.image = {0, 0, areaW, halfH};
        a.text = {0, halfH, areaW, areaH - halfH};
        break;
    case SideLayout::ImageBottom:
        a.text = {0, 0, areaW, areaH - halfH};
        a.image = {0, areaH - halfH, areaW, halfH};
        break;
    }
    return a;
}

}  // namespace wallpascii
