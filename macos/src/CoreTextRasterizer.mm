#include "CoreTextRasterizer.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

using namespace wallpascii;

namespace {

CTFontRef createFont(const std::string& name, double size) {
    if (name == "SF Mono") {
        NSFont* f = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
        return (CTFontRef)CFBridgingRetain(f);
    }
    NSString* family = [NSString stringWithUTF8String:name.c_str()];
    CTFontRef f = CTFontCreateWithName((__bridge CFStringRef)family, size, nullptr);
    return f;
}

}  // namespace

CoreTextRasterizer::CoreTextRasterizer(const std::string& fontName, double sizePx) {
    font_ = createFont(fontName, std::max(4.0, sizePx));

    // Monospace cell: the advance of '0' by the line height without leading,
    // so ASCII art rows sit tight like in a terminal.
    UniChar zero = '0';
    CGGlyph glyph = 0;
    CGSize advance = CGSizeZero;
    if (CTFontGetGlyphsForCharacters(font_, &zero, &glyph, 1))
        CTFontGetAdvancesForGlyphs(font_, kCTFontOrientationHorizontal, &glyph, &advance, 1);
    else
        advance.width = CTFontGetSize(font_) * 0.6;
    const double ascent = CTFontGetAscent(font_);
    descent_ = CTFontGetDescent(font_);
    cell_.width = std::max(1, int(std::lround(advance.width)));
    cell_.height = std::max(1, int(std::ceil(ascent + descent_)));
}

CoreTextRasterizer::~CoreTextRasterizer() {
    if (font_) CFRelease(font_);
}

const std::vector<uint8_t>& CoreTextRasterizer::mask(char32_t ch) const {
    auto it = masks_.find(ch);
    if (it != masks_.end()) return it->second;

    const int w = cell_.width, h = cell_.height;
    std::vector<uint8_t> m(size_t(w) * h, 0);

    @autoreleasepool {
        // One-character line: Core Text picks a fallback font when needed.
        UniChar units[2];
        CFIndex count = 1;
        if (ch > 0xFFFF) {
            const char32_t v = ch - 0x10000;
            units[0] = UniChar(0xD800 + (v >> 10));
            units[1] = UniChar(0xDC00 + (v & 0x3FF));
            count = 2;
        } else {
            units[0] = UniChar(ch);
        }
        CFStringRef str = CFStringCreateWithCharacters(nullptr, units, count);
        NSDictionary* attrs = @{
            (__bridge id)kCTFontAttributeName : (__bridge id)font_,
            (__bridge id)kCTForegroundColorFromContextAttributeName : @YES,
        };
        CFAttributedStringRef as =
            CFAttributedStringCreate(nullptr, str, (__bridge CFDictionaryRef)attrs);
        CTLineRef line = CTLineCreateWithAttributedString(as);

        // Alpha-only context: the coverage of the glyph is the mask.
        CGContextRef ctx = CGBitmapContextCreate(m.data(), size_t(w), size_t(h), 8, size_t(w),
                                                 nullptr, kCGImageAlphaOnly);
        if (ctx) {
            CGContextSetShouldAntialias(ctx, true);
            CGContextSetShouldSmoothFonts(ctx, false);
            CGContextSetGrayFillColor(ctx, 1, 1);
            // Center glyphs that are narrower or wider than the cell (fallback fonts).
            const double lineWidth = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
            CGContextSetTextPosition(ctx, (w - lineWidth) / 2, descent_);
            CTLineDraw(line, ctx);
            CGContextRelease(ctx);
        }
        CFRelease(line);
        CFRelease(as);
        CFRelease(str);
    }
    return masks_.emplace(ch, std::move(m)).first->second;
}

void CoreTextRasterizer::drawGlyph(RgbImage& target, int x0, int y0, char32_t ch, Color fg) const {
    if (drawShadeBlock(target, x0, y0, cell_, ch, fg)) return;

    const std::vector<uint8_t>& m = mask(ch);
    const int w = cell_.width, h = cell_.height;
    for (int y = 0; y < h; ++y) {
        const int ty = y0 + y;
        if (ty < 0 || ty >= target.height) continue;
        const uint8_t* row = &m[size_t(y) * w];  // bitmap rows run top to bottom
        for (int x = 0; x < w; ++x) {
            const int a = row[x];
            const int tx = x0 + x;
            if (a == 0 || tx < 0 || tx >= target.width) continue;
            uint8_t* p = &target.pixels[(size_t(ty) * target.width + tx) * 3];
            p[0] = uint8_t(p[0] + (int(fg.r) - p[0]) * a / 255);
            p[1] = uint8_t(p[1] + (int(fg.g) - p[1]) * a / 255);
            p[2] = uint8_t(p[2] + (int(fg.b) - p[2]) * a / 255);
        }
    }
}
