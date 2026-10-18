#pragma once

#include <CoreGraphics/CoreGraphics.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "CoreTextRasterizer.h"
#include "wallpascii/compose.h"
#include "wallpascii/image.h"

// A snapshot of everything needed to draw one wallpaper.
struct RenderJob {
    std::shared_ptr<const wallpascii::GrayImage> photo;
    wallpascii::WallpaperSettings settings;
    wallpascii::RenderColors colors;
    std::string font = "SF Mono";
    double fontSize = 20;  // output pixels
    double textSize = 64;  // output pixels, side-by-side text
};

// Renders jobs, reusing Core Text rasterizers (and their glyph caches) across
// calls. Use from a single serial queue.
class Renderer {
public:
    wallpascii::RgbImage render(const RenderJob& job);

private:
    std::shared_ptr<const CoreTextRasterizer> rasterizer(const std::string& font, double size);
    std::map<std::pair<std::string, int>, std::shared_ptr<const CoreTextRasterizer>> rasterizers_;
};

// Loads a photo: JPEG/PNG through the core, anything else ImageIO reads
// (HEIC from iPhones, TIFF, ...) through the system, upright per EXIF.
wallpascii::GrayImage loadPhoto(const std::string& path, std::string* error);

// Caller releases the result.
CGImageRef createCGImage(const wallpascii::RgbImage& img);
