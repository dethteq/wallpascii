#include "wallpascii/image.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "stb_image.h"
#include "stb_image_write.h"

namespace wallpascii {

namespace {

void setError(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}

std::filesystem::path utf8Path(const std::string& path) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
#else
    return std::filesystem::u8path(path);
#endif
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

uint16_t read16(const uint8_t* p, bool little) {
    return little ? uint16_t(p[0] | (p[1] << 8)) : uint16_t((p[0] << 8) | p[1]);
}

uint32_t read32(const uint8_t* p, bool little) {
    return little ? uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24)
                  : (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Looks for the orientation tag in the IFD0 of an EXIF TIFF block.
int orientationFromTiff(const uint8_t* t, size_t len) {
    if (len < 8) return 1;
    bool little;
    if (t[0] == 'I' && t[1] == 'I') little = true;
    else if (t[0] == 'M' && t[1] == 'M') little = false;
    else return 1;
    if (read16(t + 2, little) != 42) return 1;
    const uint32_t ifd = read32(t + 4, little);
    if (ifd + 2 > len) return 1;
    const uint16_t count = read16(t + ifd, little);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t e = size_t(ifd) + 2 + size_t(i) * 12;
        if (e + 12 > len) break;
        if (read16(t + e, little) == 0x0112) {
            const int v = read16(t + e + 8, little);
            return (v >= 1 && v <= 8) ? v : 1;
        }
    }
    return 1;
}

}  // namespace

bool parseHexColor(const std::string& text, Color& out) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return false;
    for (char ch : s)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    const unsigned long v = std::stoul(s, nullptr, 16);
    out = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return true;
}

RgbImage::RgbImage(int w, int h, Color fill) : width(w), height(h), pixels(size_t(w) * h * 3) {
    for (size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i] = fill.r;
        pixels[i + 1] = fill.g;
        pixels[i + 2] = fill.b;
    }
}

int readExifOrientation(const uint8_t* data, size_t size) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return 1;
    size_t pos = 2;
    while (pos + 4 <= size) {
        if (data[pos] != 0xFF) return 1;
        const uint8_t marker = data[pos + 1];
        if (marker == 0xD9 || marker == 0xDA) return 1;  // end of image / start of scan
        const size_t segLen = (size_t(data[pos + 2]) << 8) | data[pos + 3];
        if (segLen < 2 || pos + 2 + segLen > size) return 1;
        const uint8_t* seg = data + pos + 4;
        const size_t payload = segLen - 2;
        if (marker == 0xE1 && payload >= 6 && std::equal(seg, seg + 6, "Exif\0\0"))
            return orientationFromTiff(seg + 6, payload - 6);
        pos += 2 + segLen;
    }
    return 1;
}

GrayImage applyOrientation(const GrayImage& img, int o) {
    if (o <= 1 || o > 8 || img.empty()) return img;
    const int W = img.width, H = img.height;
    const bool swap = o >= 5;
    GrayImage out;
    out.width = swap ? H : W;
    out.height = swap ? W : H;
    out.luma.resize(img.luma.size());
    out.alpha.resize(img.alpha.size());
    const bool color = img.hasColor();
    if (color) out.rgb.resize(img.rgb.size());
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = x, sy = y;
            switch (o) {
            case 2: sx = W - 1 - x; sy = y; break;
            case 3: sx = W - 1 - x; sy = H - 1 - y; break;
            case 4: sx = x; sy = H - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = H - 1 - x; break;
            case 7: sx = W - 1 - y; sy = H - 1 - x; break;
            case 8: sx = W - 1 - y; sy = x; break;
            }
            const size_t d = size_t(y) * out.width + x;
            const size_t s = size_t(sy) * W + sx;
            out.luma[d] = img.luma[s];
            out.alpha[d] = img.alpha[s];
            if (color) std::copy_n(&img.rgb[s * 3], 3, &out.rgb[d * 3]);
        }
    }
    return out;
}

GrayImage loadImageFromMemory(const uint8_t* data, size_t size, std::string* error) {
    GrayImage img;
    if (!data || size == 0 || size > size_t(INT32_MAX)) {
        setError(error, "empty or too large input");
        return img;
    }
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(data, int(size), &w, &h, &n, 4);
    if (!px) {
        setError(error, std::string("cannot decode image: ") + stbi_failure_reason());
        return img;
    }
    img.width = w;
    img.height = h;
    img.luma.resize(size_t(w) * h);
    img.alpha.resize(size_t(w) * h);
    img.rgb.resize(size_t(w) * h * 3);
    for (size_t i = 0; i < img.luma.size(); ++i) {
        const stbi_uc* p = px + i * 4;
        std::copy_n(p, 3, &img.rgb[i * 3]);
        const float y = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        img.luma[i] = uint8_t(std::clamp(y + 0.5f, 0.0f, 255.0f));
        img.alpha[i] = p[3];
    }
    stbi_image_free(px);
    return applyOrientation(img, readExifOrientation(data, size));
}

GrayImage loadImage(const std::string& path, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        setError(error, "cannot read file: " + path);
        return {};
    }
    return loadImageFromMemory(bytes.data(), bytes.size(), error);
}

ImageFormat formatFromPath(const std::string& path) {
    std::string ext = utf8Path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return (ext == ".jpg" || ext == ".jpeg") ? ImageFormat::Jpeg : ImageFormat::Png;
}

bool saveImage(const RgbImage& img, const std::string& path, ImageFormat format, int jpegQuality,
               std::string* error) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.size() != size_t(img.width) * img.height * 3) {
        setError(error, "invalid image");
        return false;
    }
    std::vector<uint8_t> encoded;
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        const auto* d = static_cast<const uint8_t*>(data);
        v->insert(v->end(), d, d + size);
    };
    int ok;
    if (format == ImageFormat::Jpeg)
        ok = stbi_write_jpg_to_func(sink, &encoded, img.width, img.height, 3, img.pixels.data(),
                                    std::clamp(jpegQuality, 1, 100));
    else
        ok = stbi_write_png_to_func(sink, &encoded, img.width, img.height, 3, img.pixels.data(),
                                    img.width * 3);
    if (!ok) {
        setError(error, "encoding failed");
        return false;
    }
    std::ofstream f(utf8Path(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        setError(error, "cannot open for writing: " + path);
        return false;
    }
    f.write(reinterpret_cast<const char*>(encoded.data()), std::streamsize(encoded.size()));
    if (!f) {
        setError(error, "write failed: " + path);
        return false;
    }
    return true;
}

}  // namespace wallpascii
