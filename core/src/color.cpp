#include "wallpascii/color.h"

#include <algorithm>
#include <cmath>

namespace wallpascii {

namespace {

uint8_t toByte(float v) { return uint8_t(std::clamp(v + 0.5f, 0.0f, 255.0f)); }

}  // namespace

float colorLuma(Color c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

Color adjustColor(Color c, float saturation, float brightness) {
    const float sat = std::clamp(saturation, 0.0f, 2.0f);
    const float bri = std::clamp(brightness, -1.0f, 1.0f);
    const float l = colorLuma(c);
    float ch[3] = {float(c.r), float(c.g), float(c.b)};
    for (float& v : ch) {
        v = std::clamp(l + (v - l) * sat, 0.0f, 255.0f);
        v = bri >= 0 ? v + (255.0f - v) * bri : v * (1.0f + bri);
    }
    return {toByte(ch[0]), toByte(ch[1]), toByte(ch[2])};
}

Color mixColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {toByte(a.r + (b.r - a.r) * t), toByte(a.g + (b.g - a.g) * t), toByte(a.b + (b.b - a.b) * t)};
}

}  // namespace wallpascii
