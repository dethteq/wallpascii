#pragma once

#include <array>
#include <cstdint>

namespace wallpascii {

struct Adjustments {
    float brightness = 0.0f;  // -1..1, added to the normalized value
    float contrast = 0.0f;    // -1..1, 0 = unchanged
    bool invert = false;
};

// Maps an 8-bit luminance to the adjusted brightness in 0..1.
using AdjustLut = std::array<float, 256>;
AdjustLut makeAdjustLut(const Adjustments& adj);

}  // namespace wallpascii
