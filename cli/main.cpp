// wallpascii-cli: drives the core from the command line.
// Uses the built-in bitmap font; the GUI apps render glyphs with system fonts.

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "wallpascii/wallpascii.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace wallpascii;

namespace {

void printUsage() {
    std::puts(
        "Usage: wallpascii-cli -i PHOTO -o OUTPUT [options]\n"
        "\n"
        "  -i, --input PATH        JPEG or PNG photo\n"
        "  -o, --output PATH       .png or .jpg/.jpeg (format follows the extension)\n"
        "      --size WxH          output resolution in pixels (default 2560x1440)\n"
        "      --fit MODE          fill | fit | stretch | center (default fill)\n"
        "      --brightness N      -1..1 (default 0)\n"
        "      --contrast N        -1..1 (default 0)\n"
        "      --invert            invert brightness\n"
        "      --charset NAME      simple | detailed | blocks (default simple)\n"
        "      --custom CHARS      custom charset, sparse to dense (UTF-8)\n"
        "      --font-size PX      glyph size in pixels (default 14)\n"
        "      --mode MODE         ascii | text | side (text = text as image, side = text beside the picture)\n"
        "      --text TEXT         text for --mode text / side\n"
        "      --text-file PATH    read the text from a UTF-8 file instead\n"
        "      --layout POS        side mode: picture left | right | top | bottom (default left)\n"
        "      --align A           side mode: text left | center | right (default center)\n"
        "      --text-size PX      side mode: text glyph size in pixels (default 2x --font-size)\n"
        "      --threshold N       text mode: cells darker than N become spaces (default 0.5)\n"
        "      --bg #RRGGBB        background color (default #000000)\n"
        "      --fg #RRGGBB        glyph color (default #FFFFFF)\n"
        "      --color MODE        mono | original (colors of the photo; default mono)\n"
        "      --color-target T    original colors paint glyphs | background | both (default glyphs)\n"
        "      --color-strength N  0..1, mix between --fg/--bg and the photo colors (default 1)\n"
        "      --saturation N      0..2, photo color saturation (default 1)\n"
        "      --color-brightness N  -1..1, darken or lift the photo colors (default 0)\n"
        "      --format FMT        png | jpeg (overrides the extension)\n"
        "      --quality N         JPEG quality 1..100 (default 92)\n"
        "      --txt PATH          also write the character grid as UTF-8 text\n"
        "  -h, --help              show this help");
}

bool parseSize(const std::string& s, int& w, int& h) {
    const auto x = s.find_first_of("xX");
    if (x == std::string::npos) return false;
    try {
        w = std::stoi(s.substr(0, x));
        h = std::stoi(s.substr(x + 1));
    } catch (...) {
        return false;
    }
    return w > 0 && h > 0 && w <= 16384 && h <= 16384;
}

int run(const std::vector<std::string>& args) {
    std::string input, output, txtPath, formatOverride;
    int width = 2560, height = 1440, fontSize = 14, textSize = 0, quality = 92;
    WallpaperSettings ws;
    GridSettings& s = ws.grid;
    RenderColors colors;

    auto fail = [](const std::string& msg) {
        std::fprintf(stderr, "error: %s\n", msg.c_str());
        return 2;
    };

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string v;
        try {
            if (a == "-h" || a == "--help") { printUsage(); return 0; }
            else if (a == "--invert") s.adjust.invert = true;
            else if (!value(v)) return fail("missing value for " + a);
            else if (a == "-i" || a == "--input") input = v;
            else if (a == "-o" || a == "--output") output = v;
            else if (a == "--txt") txtPath = v;
            else if (a == "--size") { if (!parseSize(v, width, height)) return fail("bad --size: " + v); }
            else if (a == "--fit") {
                if (v == "fill") s.fit = FitMode::Fill;
                else if (v == "fit") s.fit = FitMode::Fit;
                else if (v == "stretch") s.fit = FitMode::Stretch;
                else if (v == "center") s.fit = FitMode::Center;
                else return fail("bad --fit: " + v);
            }
            else if (a == "--brightness") s.adjust.brightness = std::stof(v);
            else if (a == "--contrast") s.adjust.contrast = std::stof(v);
            else if (a == "--charset") {
                if (v == "simple") s.charset = charsetPreset(CharsetPreset::Simple);
                else if (v == "detailed") s.charset = charsetPreset(CharsetPreset::Detailed);
                else if (v == "blocks") s.charset = charsetPreset(CharsetPreset::Blocks);
                else return fail("bad --charset: " + v);
            }
            else if (a == "--custom") {
                s.charset = charsetFromUtf8(v);
                if (s.charset.chars.empty()) return fail("--custom needs at least one character");
            }
            else if (a == "--font-size") fontSize = std::stoi(v);
            else if (a == "--mode") {
                if (v == "ascii") s.mode = TextMode::Ascii;
                else if (v == "text") s.mode = TextMode::TextAsImage;
                else if (v == "side") ws.sideBySide = true;
                else return fail("bad --mode: " + v);
            }
            else if (a == "--text") s.text = v;
            else if (a == "--text-file") {
                std::ifstream f(v, std::ios::binary);
                if (!f) return fail("cannot read " + v);
                s.text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            }
            else if (a == "--layout") {
                if (v == "left") ws.layout = SideLayout::ImageLeft;
                else if (v == "right") ws.layout = SideLayout::ImageRight;
                else if (v == "top") ws.layout = SideLayout::ImageTop;
                else if (v == "bottom") ws.layout = SideLayout::ImageBottom;
                else return fail("bad --layout: " + v);
            }
            else if (a == "--align") {
                if (v == "left") ws.align = TextAlign::Left;
                else if (v == "center") ws.align = TextAlign::Center;
                else if (v == "right") ws.align = TextAlign::Right;
                else return fail("bad --align: " + v);
            }
            else if (a == "--text-size") textSize = std::stoi(v);
            else if (a == "--threshold") s.textThreshold = std::stof(v);
            else if (a == "--bg") { if (!parseHexColor(v, colors.background)) return fail("bad --bg: " + v); }
            else if (a == "--fg") { if (!parseHexColor(v, colors.glyph)) return fail("bad --fg: " + v); }
            else if (a == "--color") {
                if (v == "mono") s.color.mode = ColorMode::Mono;
                else if (v == "original") s.color.mode = ColorMode::Original;
                else return fail("bad --color: " + v);
            }
            else if (a == "--color-target") {
                if (v == "glyphs") s.color.target = ColorTarget::Glyphs;
                else if (v == "background") s.color.target = ColorTarget::Background;
                else if (v == "both") s.color.target = ColorTarget::Both;
                else return fail("bad --color-target: " + v);
            }
            else if (a == "--color-strength") s.color.strength = std::stof(v);
            else if (a == "--saturation") s.color.saturation = std::stof(v);
            else if (a == "--color-brightness") s.color.brightness = std::stof(v);
            else if (a == "--format") formatOverride = v;
            else if (a == "--quality") quality = std::stoi(v);
            else return fail("unknown option: " + a);
        } catch (...) {
            return fail("bad value for " + a + ": " + v);
        }
    }

    if (input.empty() || output.empty()) {
        printUsage();
        return 2;
    }
    if (s.mode == TextMode::TextAsImage && s.text.empty()) return fail("--mode text needs --text");
    if (ws.sideBySide && s.text.empty()) return fail("--mode side needs --text or --text-file");

    const auto t0 = std::chrono::steady_clock::now();
    std::string err;
    GrayImage photo = loadImage(input, &err);
    if (photo.empty()) return fail(err);

    BitmapRasterizer rasterizer(fontSize);
    BitmapRasterizer textRasterizer(textSize > 0 ? textSize : fontSize * 2);
    ws.width = width;
    ws.height = height;
    const Composition comp = compose(photo, ws, rasterizer.cellSize(), textRasterizer.cellSize());
    const GlyphGrid& grid = comp.image;
    const RgbImage out = render(comp, width, height, colors, rasterizer, textRasterizer);

    ImageFormat fmt = formatFromPath(output);
    if (formatOverride == "png") fmt = ImageFormat::Png;
    else if (formatOverride == "jpeg" || formatOverride == "jpg") fmt = ImageFormat::Jpeg;
    else if (!formatOverride.empty()) return fail("bad --format: " + formatOverride);

    if (!saveImage(out, output, fmt, quality, &err)) return fail(err);
    if (!txtPath.empty()) {
        std::ofstream f(txtPath, std::ios::binary);
        f << grid.toUtf8() << '\n';
        if (ws.sideBySide) f << '\n' << comp.text.toUtf8() << '\n';
        if (!f) return fail("cannot write " + txtPath);
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: %dx%d, photo %dx%d, grid %dx%d cells of %dx%d px, %lld ms\n", output.c_str(), width,
                height, photo.width, photo.height, grid.cols, grid.rows, grid.cell.width, grid.cell.height,
                static_cast<long long>(ms));
    return 0;
}

}  // namespace

#ifdef _WIN32
// Take arguments as UTF-16 and convert to UTF-8 so non-ASCII paths and text work.
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    return run(args);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
