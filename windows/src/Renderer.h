#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "DWriteRasterizer.h"
#include "wallpascii/compose.h"
#include "wallpascii/image.h"

// A snapshot of everything needed to draw one wallpaper.
struct RenderJob {
    std::shared_ptr<const wallpascii::GrayImage> photo;
    wallpascii::WallpaperSettings settings;
    wallpascii::RenderColors colors;
    std::wstring font = L"Consolas";
    double fontSize = 20;  // output pixels
    double textSize = 64;  // output pixels, side-by-side text
};

// Renders jobs, reusing DirectWrite rasterizers (and their glyph caches)
// across calls. Use from a single thread.
class Renderer {
public:
    wallpascii::RgbImage render(const RenderJob& job);

private:
    std::shared_ptr<const DWriteRasterizer> rasterizer(const std::wstring& font, double size);
    std::map<std::pair<std::wstring, int>, std::shared_ptr<const DWriteRasterizer>> rasterizers_;
};

// One background thread that renders previews and saves wallpapers. At most
// one preview waits behind the running task, so slider drags never pile up:
// a newer preview replaces the waiting one. Results come back to `window`
// as `message` with a heap-allocated RenderResult* in lParam.
struct RenderResult {
    bool save = false;
    std::shared_ptr<const wallpascii::RgbImage> image;  // previews
    double ms = 0;
    bool ok = true;  // saves
    std::string error;
};

class RenderWorker {
public:
    RenderWorker(HWND window, UINT message);
    ~RenderWorker();
    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    void preview(const RenderJob& job);
    void save(const RenderJob& job, const std::wstring& path, wallpascii::ImageFormat format);

private:
    struct Task {
        RenderJob job;
        bool save = false;
        std::wstring path;
        wallpascii::ImageFormat format = wallpascii::ImageFormat::Png;
    };
    void push(Task task);
    void run();

    HWND window_;
    UINT message_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    bool quit_ = false;
    std::thread thread_;
};

// Loads a photo: JPEG/PNG through the core, anything else WIC reads
// (HEIC and WebP with the Store codecs, TIFF, BMP, GIF, ...) through the
// system, upright per EXIF. Call from a thread with COM initialized.
wallpascii::GrayImage loadPhoto(const std::wstring& path, std::string* error);

// Area-averaged downscale (or nearest upscale) of a photo or wallpaper into
// top-down BGRA pixels with opaque alpha, ready for a Direct2D bitmap.
std::vector<uint8_t> scaleToBgra(const wallpascii::RgbImage& img, int w, int h);
wallpascii::RgbImage photoThumbnail(const wallpascii::GrayImage& photo, int w, int h);

std::string toUtf8(const std::wstring& s);
std::wstring toWide(const std::string& s);
