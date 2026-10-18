#include "wallpascii/text.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "wallpascii/utf8.h"

namespace wallpascii {

namespace {

using Line = std::vector<char32_t>;

// Greedy word wrap of one paragraph; runs of spaces collapse to one.
void wrapParagraph(const Line& para, int cols, std::vector<Line>& out) {
    std::vector<Line> words;
    Line word;
    for (char32_t ch : para) {
        if (ch == U' ') {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        } else {
            word.push_back(ch);
        }
    }
    if (!word.empty()) words.push_back(std::move(word));

    Line line;
    for (const Line& w : words) {
        size_t pos = 0;
        while (pos < w.size()) {
            const int room = cols - int(line.size()) - (line.empty() ? 0 : 1);
            const int left = int(w.size() - pos);
            const auto from = w.begin() + std::ptrdiff_t(pos);
            if (left <= room) {
                if (!line.empty()) line.push_back(U' ');
                line.insert(line.end(), from, w.end());
                pos = w.size();
            } else if (line.empty()) {
                // The word alone is wider than the line: hard split.
                line.insert(line.end(), from, from + cols);
                pos += size_t(cols);
                out.push_back(std::move(line));
                line.clear();
            } else {
                out.push_back(std::move(line));
                line.clear();
            }
        }
    }
    out.push_back(std::move(line));  // an empty paragraph is an empty line
}

}  // namespace

GlyphGrid layoutText(const std::string& utf8, int areaWidth, int areaHeight, CellSize cell,
                     TextAlign align, int paddingCells) {
    GlyphGrid g;
    g.cell = cell;
    if (cell.width <= 0 || cell.height <= 0 || areaWidth <= 0 || areaHeight <= 0) return g;
    g.cols = areaWidth / cell.width;
    g.rows = areaHeight / cell.height;
    g.originX = (areaWidth - g.cols * cell.width) / 2;
    g.originY = (areaHeight - g.rows * cell.height) / 2;
    g.cells.assign(size_t(g.cols) * g.rows, U' ');
    const int pad = std::clamp(paddingCells, 0, (g.cols - 1) / 2);
    const int width = g.cols - 2 * pad;
    if (width <= 0 || g.rows == 0) return g;

    std::vector<Line> lines;
    Line para;
    const std::vector<char32_t> text = decodeUtf8(utf8);
    for (size_t i = 0; i < text.size(); ++i) {
        const char32_t ch = text[i];
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            wrapParagraph(para, width, lines);
            para.clear();
        } else {
            para.push_back(ch == '\t' ? U' ' : ch);
        }
    }
    wrapParagraph(para, width, lines);

    // Trailing blank lines would only push the block off center.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.size() > size_t(g.rows)) lines.resize(size_t(g.rows));

    const int top = (g.rows - int(lines.size())) / 2;
    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        const int free = width - int(line.size());
        const int left = align == TextAlign::Left ? 0 : align == TextAlign::Right ? free : free / 2;
        char32_t* row = &g.cells[size_t(top + int(i)) * g.cols];
        std::copy(line.begin(), line.end(), row + pad + left);
    }
    return g;
}

}  // namespace wallpascii
