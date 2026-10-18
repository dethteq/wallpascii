#include "Renderer.h"

#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace wallpascii;

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

// ---- rendering ---------------------------------------------------------------

std::shared_ptr<const DWriteRasterizer> Renderer::rasterizer(const std::wstring& font, double size) {
    const auto key = std::make_pair(font, int(std::lround(size * 4)));
    auto it = rasterizers_.find(key);
    if (it == rasterizers_.end()) {
        // Keep the cache small: settings change one slider step at a time.
        // Clearing is safe for callers: they hold their own shared_ptr.
        if (rasterizers_.size() > 8) rasterizers_.clear();
        it = rasterizers_.emplace(key, std::make_shared<const DWriteRasterizer>(font, size)).first;
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

RenderWorker::RenderWorker(HWND window, UINT message)
    : window_(window), message_(message), thread_([this] { run(); }) {}

RenderWorker::~RenderWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        tasks_.clear();
    }
    cv_.notify_one();
    thread_.join();
}

void RenderWorker::push(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!task.save && !tasks_.empty() && !tasks_.back().save) tasks_.back() = std::move(task);
        else tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void RenderWorker::preview(const RenderJob& job) { push(Task{job, false, {}, ImageFormat::Png}); }

void RenderWorker::save(const RenderJob& job, const std::wstring& path, ImageFormat format) {
    push(Task{job, true, path, format});
}

void RenderWorker::run() {
    Renderer renderer;
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
            if (quit_) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        auto* result = new RenderResult;
        result->save = task.save;
        const auto t0 = std::chrono::steady_clock::now();
        auto img = std::make_shared<RgbImage>(renderer.render(task.job));
        result->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (task.save)
            result->ok = saveImage(*img, toUtf8(task.path), task.format, 92, &result->error);
        else
            result->image = std::move(img);
        if (!PostMessageW(window_, message_, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }
}

// ---- loading -------------------------------------------------------------------

namespace {

int wicOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> query;
    if (FAILED(frame->GetMetadataQueryReader(&query))) return 1;
    PROPVARIANT v;
    PropVariantInit(&v);
    int orientation = 1;
    if (SUCCEEDED(query->GetMetadataByName(L"System.Photo.Orientation", &v)) && v.vt == VT_UI2)
        orientation = v.uiVal;
    PropVariantClear(&v);
    return orientation >= 1 && orientation <= 8 ? orientation : 1;
}

GrayImage loadWithWic(const std::wstring& path, std::string* error) {
    GrayImage img;
    auto fail = [&](const char* what) {
        if (error) *error = std::string(what) + ": " + toUtf8(path);
        return GrayImage{};
    };
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return fail("cannot start Windows Imaging");
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &decoder)))
        return fail("unsupported image");
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return fail("cannot decode image");
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0) return fail("empty image");
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()))) return fail("cannot decode image");

    img.width = int(w);
    img.height = int(h);
    img.luma.resize(size_t(w) * h);
    img.alpha.resize(size_t(w) * h);
    img.rgb.resize(size_t(w) * h * 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const uint8_t* p = &rgba[i * 4];
        const float y = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        img.luma[i] = uint8_t(std::clamp(y + 0.5f, 0.0f, 255.0f));
        img.alpha[i] = p[3];
        img.rgb[i * 3] = p[0];
        img.rgb[i * 3 + 1] = p[1];
        img.rgb[i * 3 + 2] = p[2];
    }
    return applyOrientation(img, wicOrientation(frame.Get()));
}

// Averages the source pixels under each output pixel; `src*` is the part of
// the source shown. `pixel(x, y)` returns the source color as 0..255 floats.
template <typename Pixel, typename Out>
void boxScale(float sx, float sy, float sw, float sh, int dw, int dh, Pixel pixel, Out out) {
    std::vector<int> xs(size_t(dw) + 1);
    for (int x = 0; x <= dw; ++x) xs[size_t(x)] = int(std::floor(sx + sw * x / dw));
    for (int y = 0; y < dh; ++y) {
        const int y0 = int(std::floor(sy + sh * y / dh));
        const int y1 = std::max(y0 + 1, int(std::floor(sy + sh * (y + 1) / dh)));
        for (int x = 0; x < dw; ++x) {
            const int x0 = xs[size_t(x)], x1 = std::max(x0 + 1, xs[size_t(x) + 1]);
            float acc[3] = {0, 0, 0};
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) pixel(xx, yy, acc);
            const float n = float((x1 - x0) * (y1 - y0));
            out(x, y, Color{uint8_t(acc[0] / n + 0.5f), uint8_t(acc[1] / n + 0.5f), uint8_t(acc[2] / n + 0.5f)});
        }
    }
}

}  // namespace

GrayImage loadPhoto(const std::wstring& path, std::string* error) {
    GrayImage img = loadImage(toUtf8(path), error);
    if (!img.empty()) return img;
    return loadWithWic(path, error);
}

std::vector<uint8_t> scaleToBgra(const RgbImage& img, int w, int h) {
    std::vector<uint8_t> out(size_t(std::max(0, w)) * std::max(0, h) * 4, 255);
    if (img.width <= 0 || img.height <= 0 || w <= 0 || h <= 0) return out;
    boxScale(
        0, 0, float(img.width), float(img.height), w, h,
        [&](int x, int y, float* acc) {
            const Color c = img.get(std::min(x, img.width - 1), std::min(y, img.height - 1));
            acc[0] += c.r;
            acc[1] += c.g;
            acc[2] += c.b;
        },
        [&](int x, int y, Color c) {
            uint8_t* p = &out[(size_t(y) * w + x) * 4];
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        });
    return out;
}

RgbImage photoThumbnail(const GrayImage& photo, int w, int h) {
    const Color backdrop{0x0F, 0x0E, 0x0F};
    RgbImage out(w, h, backdrop);
    if (photo.empty() || w <= 0 || h <= 0) return out;
    // Aspect fill: crop the photo to the thumbnail's proportions.
    float sw = float(photo.width), sh = float(photo.height);
    if (sw / sh > float(w) / h) sw = sh * w / h;
    else sh = sw * h / w;
    const float sx = (photo.width - sw) / 2, sy = (photo.height - sh) / 2;
    const bool color = photo.hasColor();
    boxScale(
        sx, sy, sw, sh, w, h,
        [&](int x, int y, float* acc) {
            x = std::min(x, photo.width - 1);
            y = std::min(y, photo.height - 1);
            const size_t i = size_t(y) * photo.width + x;
            const float a = photo.alpha[i] / 255.0f;
            for (int c = 0; c < 3; ++c) {
                const float v = color ? photo.rgb[i * 3 + c] : photo.luma[i];
                const float bg = c == 0 ? backdrop.r : c == 1 ? backdrop.g : backdrop.b;
                acc[c] += v * a + bg * (1 - a);
            }
        },
        [&](int x, int y, Color c) { out.set(x, y, c); });
    return out;
}
