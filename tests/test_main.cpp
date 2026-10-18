// Minimal self-contained test runner, no external framework.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "wallpascii/wallpascii.h"

using namespace wallpascii;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int g_failures = 0;

struct Register {
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                     \
    static void name();                                \
    static Register reg_##name(#name, name);           \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= (eps))

GrayImage solid(int w, int h, uint8_t luma, uint8_t alpha = 255) {
    GrayImage img;
    img.width = w;
    img.height = h;
    img.luma.assign(size_t(w) * h, luma);
    img.alpha.assign(size_t(w) * h, alpha);
    return img;
}

// Left half black, right half white.
GrayImage halves(int w, int h) {
    GrayImage img = solid(w, h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = w / 2; x < w; ++x) img.luma[size_t(y) * w + x] = 255;
    return img;
}

// Solid color photo with luma and rgb filled like the loaders do.
GrayImage solidColor(int w, int h, Color c) {
    GrayImage img = solid(w, h, uint8_t(colorLuma(c) + 0.5f));
    img.rgb.resize(size_t(w) * h * 3);
    for (size_t i = 0; i < img.luma.size(); ++i) {
        img.rgb[i * 3] = c.r;
        img.rgb[i * 3 + 1] = c.g;
        img.rgb[i * 3 + 2] = c.b;
    }
    return img;
}

GridSettings settings(int areaW, int areaH, int cellW, int cellH) {
    GridSettings s;
    s.areaWidth = areaW;
    s.areaHeight = areaH;
    s.cell = {cellW, cellH};
    return s;
}

// ---- utf8 ----

TEST(utf8_roundtrip) {
    const std::string text = "a\xC3\xA9\xE2\x96\x91\xF0\x9F\x98\x80";  // a é ░ 😀
    const auto cps = decodeUtf8(text);
    CHECK(cps.size() == 4);
    CHECK(cps[0] == U'a' && cps[1] == 0xE9 && cps[2] == 0x2591 && cps[3] == 0x1F600);
    std::string back;
    for (char32_t c : cps) appendUtf8(back, c);
    CHECK(back == text);
}

TEST(utf8_invalid) {
    const auto cps = decodeUtf8(std::string("a\xFF" "b\xE2\x96", 5));
    CHECK(cps.size() == 4);
    CHECK(cps[1] == 0xFFFD);
    CHECK(cps[2] == U'b');
    CHECK(cps[3] == 0xFFFD);
}

// ---- layout ----

TEST(fit_stretch) {
    const Placement p = computePlacement(100, 50, 400, 400, FitMode::Stretch);
    CHECK_NEAR(p.dest.w, 400, 1e-4);
    CHECK_NEAR(p.dest.h, 400, 1e-4);
    CHECK_NEAR(p.source.w, 100, 1e-4);
    CHECK_NEAR(p.source.h, 50, 1e-4);
}

TEST(fit_fill_crops_sides) {
    // 2:1 photo into a square: shows the middle half of the width.
    const Placement p = computePlacement(200, 100, 100, 100, FitMode::Fill);
    CHECK_NEAR(p.dest.w, 100, 1e-4);
    CHECK_NEAR(p.source.x, 50, 1e-4);
    CHECK_NEAR(p.source.w, 100, 1e-4);
    CHECK_NEAR(p.source.h, 100, 1e-4);
}

TEST(fit_fit_letterboxes) {
    const Placement p = computePlacement(200, 100, 100, 100, FitMode::Fit);
    CHECK_NEAR(p.dest.x, 0, 1e-4);
    CHECK_NEAR(p.dest.y, 25, 1e-4);
    CHECK_NEAR(p.dest.w, 100, 1e-4);
    CHECK_NEAR(p.dest.h, 50, 1e-4);
    CHECK_NEAR(p.source.w, 200, 1e-4);
}

TEST(fit_center_small_and_large) {
    Placement p = computePlacement(50, 20, 100, 100, FitMode::Center);
    CHECK_NEAR(p.dest.x, 25, 1e-4);
    CHECK_NEAR(p.dest.y, 40, 1e-4);
    CHECK_NEAR(p.dest.w, 50, 1e-4);
    p = computePlacement(300, 100, 100, 100, FitMode::Center);
    CHECK_NEAR(p.dest.x, 0, 1e-4);
    CHECK_NEAR(p.dest.w, 100, 1e-4);
    CHECK_NEAR(p.source.x, 100, 1e-4);
    CHECK_NEAR(p.source.w, 100, 1e-4);
    CHECK_NEAR(p.dest.y, 0, 1e-4);
}

// ---- adjustments ----

TEST(adjust_identity_and_invert) {
    AdjustLut lut = makeAdjustLut({});
    CHECK_NEAR(lut[0], 0, 1e-6);
    CHECK_NEAR(lut[255], 1, 1e-6);
    CHECK_NEAR(lut[51], 0.2, 1e-6);
    lut = makeAdjustLut({0, 0, true});
    CHECK_NEAR(lut[0], 1, 1e-6);
    CHECK_NEAR(lut[255], 0, 1e-6);
}

TEST(adjust_brightness_contrast) {
    AdjustLut lut = makeAdjustLut({0.25f, 0, false});
    CHECK_NEAR(lut[0], 0.25, 1e-6);
    CHECK_NEAR(lut[255], 1, 1e-6);
    lut = makeAdjustLut({0, 0.5f, false});  // slope 3
    CHECK(lut[100] < 100 / 255.0f);
    CHECK(lut[160] > 160 / 255.0f);
    lut = makeAdjustLut({0, -1.0f, false});  // flat mid-gray
    CHECK_NEAR(lut[0], 0.5, 1e-6);
    CHECK_NEAR(lut[255], 0.5, 1e-6);
}

// ---- charsets ----

TEST(charset_presets) {
    CHECK(charsetPreset(CharsetPreset::Simple).chars.size() == 10);
    CHECK(charsetPreset(CharsetPreset::Simple).chars.front() == U' ');
    CHECK(charsetPreset(CharsetPreset::Simple).chars.back() == U'@');
    const Charset d = charsetPreset(CharsetPreset::Detailed);
    CHECK(d.chars.size() == 70);
    CHECK(d.chars.front() == U' ');
    const Charset b = charsetPreset(CharsetPreset::Blocks);
    CHECK(b.chars.size() == 5);
    CHECK(b.chars[4] == 0x2588);
    CHECK(charsetFromUtf8("ab\n").chars.size() == 2);
}

// ---- grid ----

TEST(grid_geometry_centers_leftover) {
    GridSettings s = settings(105, 52, 10, 20);
    const GlyphGrid g = buildGrid(solid(10, 10, 0), s);
    CHECK(g.cols == 10);
    CHECK(g.rows == 2);
    CHECK(g.originX == 2);
    CHECK(g.originY == 6);
}

TEST(grid_black_is_dense_white_is_space) {
    GridSettings s = settings(100, 40, 10, 20);
    s.fit = FitMode::Stretch;
    const GlyphGrid g = buildGrid(halves(200, 80), s);
    CHECK(g.cols == 10 && g.rows == 2);
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < 5; ++c) CHECK(g.at(c, r) == U'@');
        for (int c = 5; c < 10; ++c) CHECK(g.at(c, r) == U' ');
    }
}

TEST(grid_invert_flips) {
    GridSettings s = settings(100, 40, 10, 20);
    s.fit = FitMode::Stretch;
    s.adjust.invert = true;
    const GlyphGrid g = buildGrid(halves(200, 80), s);
    CHECK(g.at(0, 0) == U' ');
    CHECK(g.at(9, 0) == U'@');
}

TEST(grid_gray_levels_map_monotonically) {
    // Horizontal gradient, upscaled: characters must never get lighter left to right.
    GrayImage img = solid(10, 1, 0);
    for (int x = 0; x < 10; ++x) img.luma[size_t(x)] = uint8_t(255 - x * 28);
    GridSettings s = settings(100, 20, 10, 20);
    s.fit = FitMode::Stretch;
    const GlyphGrid g = buildGrid(img, s);
    const auto& chars = s.charset.chars;
    auto rank = [&](char32_t c) {
        for (size_t i = 0; i < chars.size(); ++i)
            if (chars[i] == c) return int(i);
        return -1;
    };
    int prev = -1;
    for (int c = 0; c < g.cols; ++c) {
        const int r = rank(g.at(c, 0));
        CHECK(r >= prev);
        prev = r;
    }
    CHECK(rank(g.at(0, 0)) == 0);
    CHECK(rank(g.at(9, 0)) >= 8);
}

TEST(grid_fit_letterbox_is_empty) {
    // Black 2:1 photo fit into a square: top and bottom rows stay blank.
    GridSettings s = settings(100, 100, 10, 10);
    s.fit = FitMode::Fit;
    const GlyphGrid g = buildGrid(solid(200, 100, 0), s);
    CHECK(g.at(5, 0) == U' ');
    CHECK(g.at(5, 1) == U' ');
    CHECK(g.at(5, 5) == U'@');
    CHECK(g.at(5, 9) == U' ');
}

TEST(grid_transparent_pixels_are_empty) {
    GridSettings s = settings(40, 40, 10, 10);
    const GlyphGrid g = buildGrid(solid(40, 40, 0, 0), s);
    for (char32_t c : g.cells) CHECK(c == U' ');
}

TEST(text_as_image_dark_cells_are_spaces) {
    GridSettings s = settings(100, 40, 10, 20);
    s.fit = FitMode::Stretch;
    s.mode = TextMode::TextAsImage;
    s.text = "AB\nC";
    const GlyphGrid g = buildGrid(halves(200, 80), s);
    // Dark left half: spaces. Bright right half: text flows through visible cells only.
    for (int c = 0; c < 5; ++c) CHECK(g.at(c, 0) == U' ');
    const std::u32string expected = U"AB CAB CAB";
    for (int c = 5; c < 10; ++c) CHECK(g.at(c, 0) == expected[size_t(c - 5)]);
    for (int c = 5; c < 10; ++c) CHECK(g.at(c, 1) == expected[size_t(c)]);
}

TEST(grid_to_utf8) {
    GlyphGrid g;
    g.cols = 2;
    g.rows = 2;
    g.cells = {U'a', 0x2588, U' ', U'b'};
    CHECK(g.toUtf8() == "a\xE2\x96\x88\n b");
}

// ---- render & io ----

TEST(render_draws_only_glyphs) {
    BitmapRasterizer ras(10);
    const CellSize cell = ras.cellSize();
    CHECK(cell.width == 6 && cell.height == 12);
    GlyphGrid g;
    g.cols = 2;
    g.rows = 1;
    g.cell = cell;
    g.cells = {0x2588, U' '};
    const RenderColors colors{{10, 20, 30}, {200, 100, 50}};
    const RgbImage img = renderGrid(g, 12, 12, colors, ras);
    CHECK(img.get(0, 0) == colors.glyph);
    CHECK(img.get(5, 11) == colors.glyph);
    CHECK(img.get(6, 0) == colors.background);
    CHECK(img.get(11, 11) == colors.background);
}

TEST(render_letter_has_ink) {
    BitmapRasterizer ras(20);
    GlyphGrid g;
    g.cols = 1;
    g.rows = 1;
    g.cell = ras.cellSize();
    g.cells = {U'@'};
    const RgbImage img = renderGrid(g, g.cell.width, g.cell.height, {}, ras);
    int ink = 0;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) ink += img.get(x, y).r == 255;
    CHECK(ink > 20);
    CHECK(ink < img.width * img.height / 2);
}

TEST(parse_hex_color) {
    Color c;
    CHECK(parseHexColor("#1a2B3c", c));
    CHECK(c.r == 0x1a && c.g == 0x2b && c.b == 0x3c);
    CHECK(parseHexColor("FFFFFF", c));
    CHECK(!parseHexColor("#12345", c));
    CHECK(!parseHexColor("#12345g", c));
}

TEST(png_and_jpeg_roundtrip) {
    RgbImage img(64, 32, {0, 0, 0});
    for (int y = 0; y < 32; ++y)
        for (int x = 32; x < 64; ++x) img.set(x, y, {255, 255, 255});

    std::string err;
    CHECK(saveImage(img, "roundtrip.png", ImageFormat::Png, 92, &err));
    GrayImage back = loadImage("roundtrip.png", &err);
    CHECK(back.width == 64 && back.height == 32);
    if (!back.empty()) {
        CHECK(back.lumaAt(0, 0) == 0);
        CHECK(back.lumaAt(63, 31) == 255);
        CHECK(back.alphaAt(10, 10) == 255);
    }

    CHECK(saveImage(img, "roundtrip.jpg", ImageFormat::Jpeg, 95, &err));
    back = loadImage("roundtrip.jpg", &err);
    CHECK(back.width == 64 && back.height == 32);
    if (!back.empty()) {
        CHECK(back.lumaAt(8, 16) < 20);
        CHECK(back.lumaAt(56, 16) > 235);
    }
}

TEST(load_missing_file_reports_error) {
    std::string err;
    const GrayImage img = loadImage("does-not-exist.png", &err);
    CHECK(img.empty());
    CHECK(!err.empty());
}

TEST(format_from_path) {
    CHECK(formatFromPath("a/b.JPG") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.jpeg") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.png") == ImageFormat::Png);
    CHECK(formatFromPath("noext") == ImageFormat::Png);
}

TEST(exif_orientation_parsing) {
    // SOI + APP1(Exif, little-endian TIFF, IFD0 with one Orientation=6 entry) + EOI.
    const std::vector<uint8_t> tiff = {
        'I', 'I', 42, 0, 8, 0, 0, 0,          // header, IFD0 at offset 8
        1, 0,                                 // 1 entry
        0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0,  // tag 0x0112 SHORT count 1 value 6
        0, 0, 0, 0,                           // next IFD
    };
    std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE1};
    const size_t len = 2 + 6 + tiff.size();
    jpeg.push_back(uint8_t(len >> 8));
    jpeg.push_back(uint8_t(len & 0xFF));
    for (char ch : std::string("Exif\0\0", 6)) jpeg.push_back(uint8_t(ch));
    jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
    jpeg.push_back(0xFF);
    jpeg.push_back(0xD9);
    CHECK(readExifOrientation(jpeg.data(), jpeg.size()) == 6);

    const uint8_t png[] = {0x89, 'P', 'N', 'G'};
    CHECK(readExifOrientation(png, sizeof png) == 1);
}

TEST(apply_orientation_rotations) {
    // 3x2 image with distinct values:  0 1 2 / 3 4 5
    GrayImage img = solid(3, 2, 0);
    for (int i = 0; i < 6; ++i) img.luma[size_t(i)] = uint8_t(i);

    GrayImage r = applyOrientation(img, 6);  // rotate 90 CW -> 2x3: 3 0 / 4 1 / 5 2
    CHECK(r.width == 2 && r.height == 3);
    CHECK(r.lumaAt(0, 0) == 3 && r.lumaAt(1, 0) == 0 && r.lumaAt(0, 2) == 5 && r.lumaAt(1, 2) == 2);

    r = applyOrientation(img, 8);  // rotate 90 CCW -> 2 5 / 1 4 / 0 3
    CHECK(r.lumaAt(0, 0) == 2 && r.lumaAt(1, 0) == 5 && r.lumaAt(0, 2) == 0 && r.lumaAt(1, 2) == 3);

    r = applyOrientation(img, 3);  // 180 -> 5 4 3 / 2 1 0
    CHECK(r.width == 3 && r.lumaAt(0, 0) == 5 && r.lumaAt(2, 1) == 0);

    r = applyOrientation(img, 2);  // mirror -> 2 1 0 / 5 4 3
    CHECK(r.lumaAt(0, 0) == 2 && r.lumaAt(0, 1) == 5);
}

// ---- side by side ----

std::string row(const GlyphGrid& g, int r) {
    std::string out;
    for (int c = 0; c < g.cols; ++c) appendUtf8(out, g.at(c, r));
    return out;
}

TEST(split_area_layouts) {
    SplitAreas a = splitArea(101, 50, SideLayout::ImageLeft);
    CHECK(a.image.x == 0 && a.image.w == 50 && a.image.h == 50);
    CHECK(a.text.x == 50 && a.text.w == 51 && a.text.h == 50);

    a = splitArea(100, 50, SideLayout::ImageRight);
    CHECK(a.text.x == 0 && a.text.w == 50);
    CHECK(a.image.x == 50 && a.image.w == 50);

    a = splitArea(100, 51, SideLayout::ImageTop);
    CHECK(a.image.y == 0 && a.image.h == 25 && a.image.w == 100);
    CHECK(a.text.y == 25 && a.text.h == 26);

    a = splitArea(100, 50, SideLayout::ImageBottom);
    CHECK(a.text.y == 0 && a.text.h == 25);
    CHECK(a.image.y == 25 && a.image.h == 25);
}

TEST(text_layout_wraps_and_centers) {
    // 10 cols x 5 rows.
    const GlyphGrid g = layoutText("hello big world", 100, 50, {10, 10}, TextAlign::Left, 0);
    CHECK(g.cols == 10 && g.rows == 5);
    // Two lines ("hello big" / "world") centered vertically: rows 1 and 2.
    CHECK(row(g, 0) == "          ");
    CHECK(row(g, 1) == "hello big ");
    CHECK(row(g, 2) == "world     ");
    CHECK(row(g, 3) == "          ");
}

TEST(text_layout_alignment) {
    GlyphGrid g = layoutText("ab", 60, 10, {10, 10}, TextAlign::Right, 0);
    CHECK(row(g, 0) == "    ab");
    g = layoutText("ab", 60, 10, {10, 10}, TextAlign::Center, 0);
    CHECK(row(g, 0) == "  ab  ");
}

TEST(text_layout_newlines_long_words_overflow) {
    // Exact-fit word followed by another word must wrap, long words split.
    GlyphGrid g = layoutText("abcd ef\r\n\nabcdefghij", 40, 100, {10, 10}, TextAlign::Left, 0);
    CHECK(g.cols == 4 && g.rows == 10);
    // Lines: abcd / ef / (blank) / abcd / efgh / ij -> 6 lines, top = 2.
    CHECK(row(g, 2) == "abcd");
    CHECK(row(g, 3) == "ef  ");
    CHECK(row(g, 4) == "    ");
    CHECK(row(g, 5) == "abcd");
    CHECK(row(g, 7) == "ij  ");

    // More lines than rows: the tail is dropped.
    g = layoutText("a\nb\nc\nd", 10, 20, {10, 10}, TextAlign::Left, 0);
    CHECK(row(g, 0) == "a" && row(g, 1) == "b");
}

TEST(text_layout_padding) {
    // 8 cols, 2 padding each side -> 4 usable.
    const GlyphGrid g = layoutText("abcd ef", 80, 20, {10, 10}, TextAlign::Right);
    CHECK(row(g, 0) == "  abcd  ");
    CHECK(row(g, 1) == "    ef  ");
}

TEST(draw_grid_offsets) {
    const GlyphGrid g = layoutText("#", 10, 10, {10, 10}, TextAlign::Left, 0);
    struct Fixed : GlyphRasterizer {
        CellSize cellSize() const override { return {10, 10}; }
        void drawGlyph(RgbImage& t, int x, int y, char32_t, Color fg) const override { t.set(x, y, fg); }
    } fixed;
    RgbImage img(40, 20, {0, 0, 0});
    drawGrid(img, g, 30, 5, RenderColors{{0, 0, 0}, {255, 255, 255}}, fixed);
    CHECK(img.get(30, 5) == (Color{255, 255, 255}));
    CHECK(img.get(0, 0) == (Color{0, 0, 0}));
}

TEST(compose_side_by_side) {
    WallpaperSettings ws;
    ws.width = 200;
    ws.height = 100;
    ws.sideBySide = true;
    ws.layout = SideLayout::ImageRight;
    ws.grid.text = "hi";
    const Composition c = compose(solid(10, 10, 0), ws, {10, 10}, {20, 20});
    CHECK(c.imageArea.x == 100 && c.imageArea.w == 100);
    CHECK(c.image.cols == 10 && c.image.at(0, 0) == U'@');
    CHECK(c.textArea.x == 0 && c.text.cols == 5 && c.text.rows == 5);

    ws.sideBySide = false;
    const Composition full = compose(solid(10, 10, 0), ws, {10, 10}, {20, 20});
    CHECK(full.image.cols == 20 && full.text.cells.empty());
}

// ---- color ----

TEST(adjust_color_saturation_and_brightness) {
    const Color red{200, 40, 40};
    CHECK(adjustColor(red, 1.0f, 0.0f) == red);
    const Color gray = adjustColor(red, 0.0f, 0.0f);
    CHECK(gray.r == gray.g && gray.g == gray.b);
    CHECK_NEAR(gray.r, colorLuma(red), 1.0);
    const Color vivid = adjustColor(red, 2.0f, 0.0f);
    CHECK(vivid.r > red.r && vivid.g < red.g);
    CHECK(adjustColor(red, 1.0f, 1.0f) == (Color{255, 255, 255}));
    CHECK(adjustColor(red, 1.0f, -1.0f) == (Color{0, 0, 0}));
    CHECK(mixColor({0, 0, 0}, {200, 100, 50}, 0.5f) == (Color{100, 50, 25}));
}

TEST(grid_mono_has_no_colors) {
    GridSettings s = settings(40, 40, 10, 10);
    const GlyphGrid g = buildGrid(solidColor(4, 4, {255, 0, 0}), s);
    CHECK(g.colors.empty());
}

TEST(grid_original_samples_photo_colors) {
    // Left half red, right half blue.
    GrayImage img = solidColor(20, 10, {220, 20, 20});
    for (int y = 0; y < 10; ++y)
        for (int x = 10; x < 20; ++x) {
            uint8_t* p = &img.rgb[(size_t(y) * 20 + x) * 3];
            p[0] = 20; p[1] = 20; p[2] = 220;
        }
    GridSettings s = settings(100, 50, 10, 10);
    s.fit = FitMode::Stretch;
    s.color.mode = ColorMode::Original;
    const GlyphGrid g = buildGrid(img, s);
    CHECK(g.colors.size() == g.cells.size());
    CHECK(g.colors[0].color == (Color{220, 20, 20}));
    CHECK_NEAR(g.colors[0].coverage, 1.0, 1e-4);
    CHECK(g.colors[9].color == (Color{20, 20, 220}));

    s.color.saturation = 0.0f;
    const GlyphGrid gray = buildGrid(img, s);
    const Color c = gray.colors[0].color;
    CHECK(c.r == c.g && c.g == c.b);
}

TEST(grid_original_without_rgb_uses_luma) {
    GridSettings s = settings(40, 40, 10, 10);
    s.color.mode = ColorMode::Original;
    const GlyphGrid g = buildGrid(solid(4, 4, 90), s);
    CHECK(g.colors[0].color == (Color{90, 90, 90}));
}

TEST(render_original_colors_glyphs) {
    struct Fill : GlyphRasterizer {
        CellSize cellSize() const override { return {4, 4}; }
        void drawGlyph(RgbImage& t, int x, int y, char32_t, Color fg) const override { t.set(x + 1, y + 1, fg); }
    } ras;
    GlyphGrid g;
    g.cols = 2;
    g.rows = 1;
    g.cell = {4, 4};
    g.cells = {U'#', U' '};
    g.color.mode = ColorMode::Original;
    g.colors = {{{200, 0, 0}, 1.0f}, {{0, 0, 200}, 1.0f}};
    const RenderColors theme{{0, 0, 0}, {100, 100, 100}};

    RgbImage full = renderGrid(g, 8, 4, theme, ras);
    CHECK(full.get(1, 1) == (Color{200, 0, 0}));
    CHECK(full.get(0, 0) == theme.background);
    CHECK(full.get(5, 1) == theme.background);  // space: nothing drawn

    g.color.strength = 0.5f;
    CHECK(renderGrid(g, 8, 4, theme, ras).get(1, 1) == (Color{150, 50, 50}));
    g.color.strength = 0.0f;
    CHECK(renderGrid(g, 8, 4, theme, ras).get(1, 1) == theme.glyph);
}

TEST(render_original_colors_background) {
    struct Fill : GlyphRasterizer {
        CellSize cellSize() const override { return {4, 4}; }
        void drawGlyph(RgbImage& t, int x, int y, char32_t, Color fg) const override { t.set(x + 1, y + 1, fg); }
    } ras;
    GlyphGrid g;
    g.cols = 2;
    g.rows = 1;
    g.cell = {4, 4};
    g.cells = {U'#', U' '};
    g.color.mode = ColorMode::Original;
    g.color.target = ColorTarget::Background;
    g.colors = {{{250, 250, 120}, 1.0f}, {{20, 20, 120}, 0.0f}};
    const RenderColors theme{{0, 0, 0}, {255, 255, 255}};

    const RgbImage img = renderGrid(g, 8, 4, theme, ras);
    CHECK(img.get(0, 0) == (Color{250, 250, 120}));  // cell filled
    CHECK(img.get(1, 1) == theme.background);       // dark glyph on a light cell
    CHECK(img.get(4, 0) == theme.background);       // uncovered cell stays plain

    g.color.target = ColorTarget::Both;
    const RgbImage both = renderGrid(g, 8, 4, theme, ras);
    CHECK(both.get(1, 1) == (Color{250, 250, 120}));  // full color glyph
    const Color dim = both.get(0, 0);
    CHECK(dim.r < 100 && dim.r > 0);  // dimmed color behind
}

TEST(png_keeps_colors) {
    RgbImage src(4, 2, {10, 200, 30});
    const std::string path = "wallpascii_test_color.png";
    CHECK(saveImage(src, path, ImageFormat::Png));
    const GrayImage back = loadImage(path);
    CHECK(back.hasColor());
    CHECK(back.rgb[0] == 10 && back.rgb[1] == 200 && back.rgb[2] == 30);
    const GrayImage rotated = applyOrientation(back, 6);
    CHECK(rotated.hasColor() && rotated.width == 2 && rotated.rgb[1] == 200);
    std::remove(path.c_str());
}

}  // namespace

int main() {
    int failedTests = 0;
    for (const auto& t : registry()) {
        const int before = g_failures;
        t.fn();
        const bool ok = g_failures == before;
        if (!ok) ++failedTests;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}
