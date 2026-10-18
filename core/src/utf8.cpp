#include "wallpascii/utf8.h"

namespace wallpascii {

std::vector<char32_t> decodeUtf8(const std::string& text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        int extra;
        char32_t cp;
        if (c < 0x80) { out.push_back(c); ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
        else { out.push_back(0xFFFD); ++i; continue; }

        bool ok = true;
        size_t consumed = 1;  // a broken sequence becomes a single U+FFFD
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= n || (s[i + k] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (s[i + k] & 0x3F);
            ++consumed;
        }
        static const char32_t minForLen[4] = {0, 0x80, 0x800, 0x10000};
        if (!ok || cp < minForLen[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(0xFFFD);
            i += ok ? size_t(extra) + 1 : consumed;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

}  // namespace wallpascii
