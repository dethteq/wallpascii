#include "wallpascii/ascii.h"

#include <algorithm>
#include <cmath>

#include "wallpascii/utf8.h"

namespace wallpascii {

Charset charsetPreset(CharsetPreset preset) {
    switch (preset) {
    case CharsetPreset::Simple:
        return charsetFromUtf8(" .:-=+*#%@");
    case CharsetPreset::Detailed:
        // Classic 70-level ramp (Paul Bourke), sparse to dense.
        return charsetFromUtf8(" .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$");
    case CharsetPreset::Blocks:
        return charsetFromUtf8(" \xE2\x96\x91\xE2\x96\x92\xE2\x96\x93\xE2\x96\x88");  // " ░▒▓█"
    }
    return {};
}

Charset charsetFromUtf8(const std::string& utf8) {
    Charset cs;
    for (char32_t cp : decodeUtf8(utf8)) {
        if (cp == '\n' || cp == '\r' || cp == '\t') continue;
        cs.chars.push_back(cp);
    }
    return cs;
}

std::string GlyphGrid::toUtf8() const {
    std::string out;
    out.reserve(size_t(rows) * (cols + 1));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) appendUtf8(out, at(c, r));
        if (r + 1 < rows) out += '\n';
    }
    return out;
}

CellSamples sampleCells(const GrayImage& img, const GridSettings& s) {
    CellSamples out;
    if (s.cell.width <= 0 || s.cell.height <= 0 || s.areaWidth <= 0 || s.areaHeight <= 0) return out;

    out.cols = s.areaWidth / s.cell.width;
    out.rows = s.areaHeight / s.cell.height;
    out.originX = (s.areaWidth - out.cols * s.cell.width) / 2;
    out.originY = (s.areaHeight - out.rows * s.cell.height) / 2;
    out.samples.assign(size_t(out.cols) * out.rows, CellSample{});
    if (img.empty()) return out;

    const Placement pl = computePlacement(img.width, img.height, s.areaWidth, s.areaHeight, s.fit);
    if (pl.dest.w <= 0 || pl.dest.h <= 0) return out;
    const AdjustLut lut = makeAdjustLut(s.adjust);

    const float scaleX = pl.source.w / pl.dest.w;  // source pixels per area pixel
    const float scaleY = pl.source.h / pl.dest.h;
    const float cellArea = float(s.cell.width) * float(s.cell.height);
    const bool wantColor = s.color.mode == ColorMode::Original;
    const bool photoColor = wantColor && img.hasColor();

    for (int r = 0; r < out.rows; ++r) {
        const float cy0 = float(out.originY + r * s.cell.height);
        const float cy1 = cy0 + float(s.cell.height);
        const float oy0 = std::max(cy0, pl.dest.y);
        const float oy1 = std::min(cy1, pl.dest.y + pl.dest.h);
        if (oy1 <= oy0) continue;

        // Matching rows in the photo; always at least one pixel, even when upscaling.
        const float sy0 = pl.source.y + (oy0 - pl.dest.y) * scaleY;
        const float sy1 = pl.source.y + (oy1 - pl.dest.y) * scaleY;
        int py0 = int(std::floor(sy0));
        int py1 = int(std::ceil(sy1));
        if (py1 <= py0) py1 = py0 + 1;
        py0 = std::clamp(py0, 0, img.height - 1);
        py1 = std::clamp(py1, py0 + 1, img.height);

        for (int c = 0; c < out.cols; ++c) {
            const float cx0 = float(out.originX + c * s.cell.width);
            const float cx1 = cx0 + float(s.cell.width);
            const float ox0 = std::max(cx0, pl.dest.x);
            const float ox1 = std::min(cx1, pl.dest.x + pl.dest.w);
            if (ox1 <= ox0) continue;

            const float sx0 = pl.source.x + (ox0 - pl.dest.x) * scaleX;
            const float sx1 = pl.source.x + (ox1 - pl.dest.x) * scaleX;
            int px0 = int(std::floor(sx0));
            int px1 = int(std::ceil(sx1));
            if (px1 <= px0) px1 = px0 + 1;
            px0 = std::clamp(px0, 0, img.width - 1);
            px1 = std::clamp(px1, px0 + 1, img.width);

            double sumV = 0, sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            for (int y = py0; y < py1; ++y) {
                const uint8_t* l = &img.luma[size_t(y) * img.width];
                const uint8_t* a = &img.alpha[size_t(y) * img.width];
                for (int x = px0; x < px1; ++x) {
                    sumV += double(lut[l[x]]) * a[x];
                    sumA += a[x];
                }
                if (photoColor) {
                    const uint8_t* p = &img.rgb[size_t(y) * img.width * 3];
                    for (int x = px0; x < px1; ++x) {
                        sumR += double(p[x * 3]) * a[x];
                        sumG += double(p[x * 3 + 1]) * a[x];
                        sumB += double(p[x * 3 + 2]) * a[x];
                    }
                } else if (wantColor) {
                    for (int x = px0; x < px1; ++x) sumR += double(l[x]) * a[x];
                }
            }
            if (sumA <= 0) continue;

            const double count = double(py1 - py0) * double(px1 - px0);
            const float overlap = (ox1 - ox0) * (oy1 - oy0) / cellArea;
            CellSample& cs = out.samples[size_t(r) * out.cols + c];
            cs.brightness = float(sumV / sumA);
            cs.coverage = float(sumA / (255.0 * count)) * overlap;
            if (photoColor) {
                cs.color = {uint8_t(sumR / sumA + 0.5), uint8_t(sumG / sumA + 0.5), uint8_t(sumB / sumA + 0.5)};
            } else if (wantColor) {
                const uint8_t v = uint8_t(sumR / sumA + 0.5);
                cs.color = {v, v, v};
            }
        }
    }
    return out;
}

GlyphGrid buildGrid(const GrayImage& img, const GridSettings& s) {
    const CellSamples cs = sampleCells(img, s);
    GlyphGrid g;
    g.cols = cs.cols;
    g.rows = cs.rows;
    g.originX = cs.originX;
    g.originY = cs.originY;
    g.cell = s.cell;
    g.cells.assign(cs.samples.size(), U' ');
    g.color = s.color;
    if (s.color.mode == ColorMode::Original) {
        g.colors.resize(cs.samples.size());
        for (size_t i = 0; i < cs.samples.size(); ++i) {
            g.colors[i].color = adjustColor(cs.samples[i].color, s.color.saturation, s.color.brightness);
            g.colors[i].coverage = cs.samples[i].coverage;
        }
    }

    if (s.mode == TextMode::Ascii) {
        const auto& chars = s.charset.chars;
        if (chars.empty()) return g;
        const int n = int(chars.size());
        for (size_t i = 0; i < cs.samples.size(); ++i) {
            // Dark areas get dense characters, light areas sparse ones.
            const float ink = (1.0f - cs.samples[i].brightness) * cs.samples[i].coverage;
            const int idx = std::clamp(int(ink * float(n)), 0, n - 1);
            g.cells[i] = chars[size_t(idx)];
        }
    } else {
        std::vector<char32_t> text = decodeUtf8(s.text);
        for (char32_t& ch : text)
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = U' ';
        if (text.empty()) return g;
        // The text flows continuously through the visible cells; dark cells are
        // spaces and do not consume characters.
        size_t next = 0;
        for (size_t i = 0; i < cs.samples.size(); ++i) {
            const CellSample& smp = cs.samples[i];
            if (smp.coverage < 0.5f || smp.brightness < s.textThreshold) continue;
            g.cells[i] = text[next];
            next = (next + 1) % text.size();
        }
    }
    return g;
}

}  // namespace wallpascii
