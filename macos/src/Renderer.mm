#include "Renderer.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace wallpascii;

std::shared_ptr<const CoreTextRasterizer> Renderer::rasterizer(const std::string& font, double size) {
    const auto key = std::make_pair(font, int(std::lround(size * 4)));
    auto it = rasterizers_.find(key);
    if (it == rasterizers_.end()) {
        // Keep the cache small: settings change one slider step at a time.
        // Clearing is safe for callers: they hold their own shared_ptr.
        if (rasterizers_.size() > 8) rasterizers_.clear();
        it = rasterizers_.emplace(key, std::make_shared<const CoreTextRasterizer>(font, size)).first;
    }
    return it->second;
}

RgbImage Renderer::render(const RenderJob& job) {
    // Each lookup may evict the cache, so hold both by shared_ptr.
    const auto image = rasterizer(job.font, job.fontSize);
    const auto text = rasterizer(job.font, job.textSize);
    static const GrayImage noPhoto;
    const GrayImage& photo = job.photo ? *job.photo : noPhoto;
    const Composition c = compose(photo, job.settings, image->cellSize(), text->cellSize());
    return wallpascii::render(c, job.settings.width, job.settings.height, job.colors, *image, *text);
}

namespace {

GrayImage loadWithImageIO(const std::string& path, std::string* error) {
    GrayImage img;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!src) {
        if (error) *error = "cannot read file: " + path;
        return img;
    }
    // A "thumbnail" at full size is the simplest way to get the EXIF rotation applied.
    NSDictionary* props = (__bridge_transfer NSDictionary*)CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
    const double pw = [props[(id)kCGImagePropertyPixelWidth] doubleValue];
    const double ph = [props[(id)kCGImagePropertyPixelHeight] doubleValue];
    NSDictionary* opts = @{
        (id)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (id)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (id)kCGImageSourceThumbnailMaxPixelSize : @(std::max(pw, ph)),
    };
    CGImageRef cg = CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef)opts);
    CFRelease(src);
    if (!cg) {
        if (error) *error = "unsupported image: " + path;
        return img;
    }

    const size_t w = CGImageGetWidth(cg), h = CGImageGetHeight(cg);
    std::vector<uint8_t> rgba(w * h * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
        CGContextRelease(ctx);
    }
    CGImageRelease(cg);
    if (!ctx) {
        if (error) *error = "cannot decode image: " + path;
        return img;
    }

    img.width = int(w);
    img.height = int(h);
    img.luma.resize(w * h);
    img.alpha.resize(w * h);
    img.rgb.resize(w * h * 3);
    for (size_t i = 0; i < w * h; ++i) {
        const uint8_t* p = &rgba[i * 4];
        const int a = p[3];
        // Un-premultiply, then Rec. 709 luma like the core loader.
        auto ch = [a](int v) { return a ? std::min(255, v * 255 / a) : 0; };
        const int r = ch(p[0]), g = ch(p[1]), b = ch(p[2]);
        const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        img.luma[i] = uint8_t(std::clamp(y + 0.5f, 0.0f, 255.0f));
        img.alpha[i] = uint8_t(a);
        img.rgb[i * 3] = uint8_t(r);
        img.rgb[i * 3 + 1] = uint8_t(g);
        img.rgb[i * 3 + 2] = uint8_t(b);
    }
    return img;
}

}  // namespace

GrayImage loadPhoto(const std::string& path, std::string* error) {
    GrayImage img = loadImage(path, error);
    if (!img.empty()) return img;
    return loadWithImageIO(path, error);
}

CGImageRef createCGImage(const RgbImage& img) {
    if (img.width <= 0 || img.height <= 0) return nullptr;
    CFDataRef data = CFDataCreate(nullptr, img.pixels.data(), CFIndex(img.pixels.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef out = CGImageCreate(size_t(img.width), size_t(img.height), 8, 24, size_t(img.width) * 3, cs,
                                   kCGBitmapByteOrderDefault | kCGImageAlphaNone, provider, nullptr, false,
                                   kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    CFRelease(data);
    return out;
}
