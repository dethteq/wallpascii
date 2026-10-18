#include "wallpascii/adjust.h"

#include <algorithm>

namespace wallpascii {

AdjustLut makeAdjustLut(const Adjustments& adj) {
    // contrast -1..1 maps to a slope of 0..inf around mid-gray; clamp just below 1
    // so the slope stays finite.
    const float c = std::clamp(adj.contrast, -1.0f, 0.99f);
    const float slope = (1.0f + c) / (1.0f - c);
    const float b = std::clamp(adj.brightness, -1.0f, 1.0f);

    AdjustLut lut{};
    for (int i = 0; i < 256; ++i) {
        float v = float(i) / 255.0f;
        v = (v - 0.5f) * slope + 0.5f + b;
        v = std::clamp(v, 0.0f, 1.0f);
        if (adj.invert) v = 1.0f - v;
        lut[size_t(i)] = v;
    }
    return lut;
}

}  // namespace wallpascii
