#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wallpascii {

// Grayscale photo with alpha, 8 bits per channel. Luminance is stored as-is;
// brightness/contrast/invert are applied later through a lookup table.
// The loaders also keep the original colors for the Original color mode.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> luma;   // width * height
    std::vector<uint8_t> alpha;  // width * height, 255 = opaque
    std::vector<uint8_t> rgb;    // width * height * 3, straight (not premultiplied); may be empty

    bool empty() const { return width <= 0 || height <= 0; }
    bool hasColor() const { return !empty() && rgb.size() == size_t(width) * height * 3; }
    uint8_t lumaAt(int x, int y) const { return luma[size_t(y) * width + x]; }
    uint8_t alphaAt(int x, int y) const { return alpha[size_t(y) * width + x]; }
};

struct Color {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
};

// Parses "#RRGGBB" or "RRGGBB". Returns false on malformed input.
bool parseHexColor(const std::string& text, Color& out);

// Final rendered wallpaper, tightly packed RGB.
struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // width * height * 3

    RgbImage() = default;
    RgbImage(int w, int h, Color fill);
    void set(int x, int y, Color c) {
        uint8_t* p = &pixels[(size_t(y) * width + x) * 3];
        p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
    Color get(int x, int y) const {
        const uint8_t* p = &pixels[(size_t(y) * width + x) * 3];
        return {p[0], p[1], p[2]};
    }
};

// Loads a JPEG or PNG, converts it to grayscale (Rec. 709 luma), keeps the
// colors alongside and applies the
// EXIF orientation of JPEGs so phone photos come out upright.
// On failure returns an empty image and fills `error`.
GrayImage loadImage(const std::string& path, std::string* error = nullptr);
GrayImage loadImageFromMemory(const uint8_t* data, size_t size, std::string* error = nullptr);

// Reads the EXIF orientation tag (1..8) from JPEG bytes; 1 when absent.
int readExifOrientation(const uint8_t* data, size_t size);
// Rotates/flips an image according to an EXIF orientation value.
GrayImage applyOrientation(const GrayImage& img, int orientation);

enum class ImageFormat { Png, Jpeg };

// Picks the format from the file extension (.jpg/.jpeg -> JPEG, else PNG).
ImageFormat formatFromPath(const std::string& path);

bool saveImage(const RgbImage& img, const std::string& path, ImageFormat format,
               int jpegQuality = 92, std::string* error = nullptr);

}  // namespace wallpascii
