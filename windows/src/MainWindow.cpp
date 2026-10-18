#include "MainWindow.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <functional>
#include <iterator>
#include <thread>

#include "wallpascii/wallpascii.h"

using Microsoft::WRL::ComPtr;
using namespace wallpascii;

namespace {

const wchar_t kClassName[] = L"wallpascii.main";
constexpr UINT kMsgRendered = WM_APP + 1;
constexpr UINT kMsgPhoto = WM_APP + 2;
constexpr UINT_PTR kTimerSave = 1;

enum TextModeIndex { kTextOff = 0, kTextSide = 1, kTextImage = 2 };
enum EditId { kIdCustom = 101, kIdText, kIdBgHex, kIdFgHex, kIdWidth, kIdHeight };

constexpr float kSidebarWidth = 340;
constexpr float kLabelWidth = 92;
constexpr float kPad = 20;
constexpr float kBarHeight = 60;

struct ColorPreset {
    const wchar_t* name;
    uint32_t background;
    uint32_t glyph;
};
const ColorPreset kPresets[] = {
    {L"Ash", 0x0B0B0C, 0xE8E8E8},   {L"Bone", 0xE6E0D3, 0x1A1817},  {L"Ember", 0x171517, 0xE07B4A},
    {L"Blood", 0x0D0909, 0xA8322B}, {L"Tape", 0x1C1A1C, 0x9C978F},  {L"Ghost", 0x101214, 0x8FA3A8},
};

// Monospace families offered in the Font menu, when installed.
const wchar_t* const kFontCandidates[] = {
    L"Cascadia Mono", L"Consolas",       L"Lucida Console", L"Courier New", L"JetBrains Mono",
    L"Fira Code",     L"Source Code Pro", L"IBM Plex Mono",  L"Hack",        L"Iosevka",
};

// Loaded photo handed from the loader thread to the window.
struct PhotoResult {
    std::wstring path;
    std::shared_ptr<const GrayImage> photo;
    RgbImage thumb;
    std::string error;
};

std::wstring fileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring stripExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

std::wstring wformat(const wchar_t* fmt, ...) {
    wchar_t buf[256];
    va_list args;
    va_start(args, fmt);
    vswprintf(buf, 256, fmt, args);
    va_end(args);
    return buf;
}

// EDIT controls want CRLF; the core and the stored settings use LF.
std::wstring toCrlf(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'\n') out += L'\r';
        if (c != L'\r') out += c;
    }
    return out;
}

std::wstring toLf(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s)
        if (c != L'\r') out += c;
    return out;
}

D2D1_RECT_F inset(const D2D1_RECT_F& r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy);
}

}  // namespace

// ---- drag and drop ------------------------------------------------------------------

class DropTarget final : public IDropTarget {
public:
    DropTarget(std::function<void(bool)> hover, std::function<void(const std::wstring&)> drop)
        : hover_(std::move(hover)), drop_(std::move(drop)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDropTarget)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        ok_ = !firstFile(data).empty();
        *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (ok_) hover_(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        hover_(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        hover_(false);
        const std::wstring path = firstFile(data);
        *effect = path.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
        if (!path.empty()) drop_(path);
        return S_OK;
    }

private:
    static std::wstring firstFile(IDataObject* data) {
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (!data || FAILED(data->GetData(&fmt, &medium))) return {};
        std::wstring path;
        if (HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
            const UINT n = DragQueryFileW(drop, 0, nullptr, 0);
            if (n > 0) {
                path.resize(n + 1);
                DragQueryFileW(drop, 0, path.data(), n + 1);
                path.resize(n);
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return path;
    }

    ULONG refs_ = 1;
    bool ok_ = false;
    std::function<void(bool)> hover_;
    std::function<void(const std::wstring&)> drop_;
};

// ---- photo card -----------------------------------------------------------------------

struct PhotoCard : Widget {
    std::wstring name = L"no photo";
    std::wstring info = L"jpeg · png · webp · heic";
    ID2D1Bitmap* thumb = nullptr;  // owned by the window
    float buttonLeft = 0;

    bool clickable() const override { return false; }
    D2D1_RECT_F thumbRect() const {
        const float cy = (rc.top + rc.bottom) / 2;
        return D2D1::RectF(rc.left + 10, cy - 22, rc.left + 74, cy + 22);
    }
    void paint(Canvas& c, bool) override {
        c.fill(rc, theme::Card, 8);
        const D2D1_RECT_F t = thumbRect();
        c.fill(t, theme::Preview, 4);
        if (thumb) c.target()->DrawBitmap(thumb, t, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        const float cy = (rc.top + rc.bottom) / 2;
        const float x = t.right + 12, right = buttonLeft - 10;
        c.text(name, D2D1::RectF(x, cy - 19, right, cy + 1), Font::UiMedium, theme::Text);
        c.text(info, D2D1::RectF(x, cy + 1, right, cy + 17), Font::Mono11, theme::Faint);
    }
};

// ---- window class -----------------------------------------------------------------------

bool MainWindow::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

MainWindow::MainWindow() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\wallpascii", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr,
                    &settingsKey_, nullptr);
    editBrush_ = CreateSolidBrush(toColorRef(theme::Preview));

    for (const wchar_t* f : kFontCandidates)
        if (hasFontFamily(f)) fonts_.push_back(f);
    if (fonts_.empty()) fonts_.push_back(L"Courier New");

    job_.colors.background = {0x0B, 0x0B, 0x0C};
    job_.colors.glyph = {0xE8, 0xE8, 0xE8};
    job_.fontSize = 20;
    job_.textSize = 72;
    job_.font = fonts_.front();
    job_.settings.align = TextAlign::Left;

    CreateWindowExW(0, kClassName, L"wallpascii", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                    CW_USEDEFAULT, 1240, 820, nullptr, nullptr, GetModuleHandleW(nullptr), this);
}

MainWindow::~MainWindow() {
    if (settingsKey_) RegCloseKey(settingsKey_);
    if (editFont_) DeleteObject(editFont_);
    if (editSmallFont_) DeleteObject(editSmallFont_);
    if (editBrush_) DeleteObject(editBrush_);
}

void MainWindow::show(int cmdShow) {
    // Window placement from the last run, else 1240 × 820 centered on the main screen.
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    DWORD size = sizeof(wp.rcNormalPosition);
    RECT saved{};
    if (settingsKey_ &&
        RegGetValueW(settingsKey_, nullptr, L"windowRect", RRF_RT_REG_BINARY, nullptr, &saved, &size) ==
            ERROR_SUCCESS &&
        size == sizeof(RECT) && saved.right - saved.left > 200 && saved.bottom - saved.top > 200) {
        GetWindowPlacement(hwnd_, &wp);
        wp.rcNormalPosition = saved;
        DWORD maximized = 0, len = sizeof(maximized);
        RegGetValueW(settingsKey_, nullptr, L"windowMaximized", RRF_RT_REG_DWORD, nullptr, &maximized, &len);
        wp.showCmd = maximized ? SW_SHOWMAXIMIZED : cmdShow;
        SetWindowPlacement(hwnd_, &wp);
    } else {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int w = int(1240 * scale_), h = int(820 * scale_);
        SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - w) / 2,
                     work.top + (work.bottom - work.top - h) / 2, w, h, SWP_NOZORDER);
        ShowWindow(hwnd_, cmdShow);
    }
    updateAutoResolution();
    updateDerivedViews();
    scheduleRender();
}

LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- UI construction ------------------------------------------------------------------------

template <typename T, typename... Args>
T* MainWindow::make(Args&&... args) {
    auto w = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = w.get();
    widgets_.push_back(std::move(w));
    raw->onChange = [this, raw] { changed(raw); };
    return raw;
}

MainWindow::Row* MainWindow::section(const std::wstring& title) {
    Row r;
    r.kind = Row::Section;
    std::wstring upper = title;
    for (wchar_t& ch : upper) ch = wchar_t(towupper(ch));
    r.label = make<Label>(upper, Font::Mono11, theme::Faint);
    r.height = 16;
    rows_.push_back(r);
    return &rows_.back();
}

MainWindow::Row* MainWindow::row(const std::wstring& title, std::vector<Item> items, float height) {
    Row r;
    r.kind = Row::Labeled;
    r.label = make<Label>(title, Font::Ui, theme::Dim);
    r.items = std::move(items);
    r.height = height;
    rows_.push_back(r);
    return &rows_.back();
}

Slider* MainWindow::slider(double min, double max, double value, Label** valueLabel) {
    if (valueLabel) *valueLabel = make<Label>(L"", Font::Mono11, theme::Faint, Align::Right);
    return make<Slider>(min, max, value);
}

MainWindow::Row* MainWindow::sliderRow(const std::wstring& title, Slider* s, Label* value) {
    std::vector<Item> items{{s, 0, 0}};
    if (value) items.push_back({value, 34, 0});
    Row* r = row(title, items);
    r->gap = 6;
    return r;
}

Segments* MainWindow::segments(std::vector<std::wstring> labels) { return make<Segments>(std::move(labels)); }

EditBox* MainWindow::editBox(bool multiline, int id, DWORD style) {
    EditBox* e = make<EditBox>(hwnd_, multiline, id, style);
    e->onChange = nullptr;  // EN_CHANGE drives edits
    return e;
}

void MainWindow::createEditFonts() {
    if (editFont_) DeleteObject(editFont_);
    if (editSmallFont_) DeleteObject(editSmallFont_);
    auto font = [this](float size) {
        return CreateFontW(-int(std::lround(size * scale_)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH,
                           uiMonoFamily().c_str());
    };
    editFont_ = font(13);
    editSmallFont_ = font(12);
    for (EditBox* e : {customChars_, textBox_, bgHex_, fgHex_})
        if (e) SendMessageW(e->edit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    for (EditBox* e : {widthField_, heightField_})
        if (e) SendMessageW(e->edit, WM_SETFONT, reinterpret_cast<WPARAM>(editSmallFont_), TRUE);
    for (EditBox* e : {customChars_, bgHex_, fgHex_, widthField_, heightField_})
        if (e) SendMessageW(e->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

void MainWindow::buildUI() {
    // IMAGE
    section(L"Image");
    photoCard_ = make<PhotoCard>();
    {
        Row r;
        r.kind = Row::Full;
        r.items = {{photoCard_, 0, 0}};
        r.height = 64;
        rows_.push_back(r);
    }
    chooseButton_ = make<Button>(L"Choose…", false);
    chooseButton_->onChange = [this] { openDialog(); };
    fitPopup_ = make<Popup>(std::vector<std::wstring>{L"Fill Screen", L"Fit to Screen", L"Stretch to Fill", L"Center"});
    row(L"Fit", {{fitPopup_, 0, 0}});
    brightness_ = slider(-1, 1, 0, nullptr);
    sliderRow(L"Brightness", brightness_, nullptr);
    contrast_ = slider(-1, 1, 0, nullptr);
    sliderRow(L"Contrast", contrast_, nullptr);
    invert_ = make<Checkbox>(L"Invert");
    row(L"", {{invert_, 0, 0}});

    // ASCII
    section(L"ASCII");
    charsetPopup_ = make<Popup>(std::vector<std::wstring>{L"Simple   .:-=+*#%@", L"Detailed   .'`^\",:;Il!i…",
                                                          L"Blocks   ░▒▓█", L"Custom"});
    row(L"Characters", {{charsetPopup_, 0, 0}});
    customChars_ = editBox(false, kIdCustom);
    customChars_->setText(L" .oO0@");
    SendMessageW(customChars_->edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"sparse → dense"));
    customRow_ = row(L"", {{customChars_, 0, 0}});
    fontPopup_ = make<Popup>(fonts_);
    row(L"Font", {{fontPopup_, 0, 0}});
    fontSize_ = slider(6, 80, 20, &fontSizeValue_);
    sliderRow(L"Font size", fontSize_, fontSizeValue_);

    // TEXT
    section(L"Text");
    modeSeg_ = segments({L"Off", L"Side by side", L"Text as image"});
    {
        Row r;
        r.kind = Row::Full;
        r.items = {{modeSeg_, 0, 0}};
        rows_.push_back(r);
    }
    textBox_ = editBox(true, kIdText);
    {
        Row r;
        r.kind = Row::Full;
        r.items = {{textBox_, 0, 0}};
        r.height = 92;
        rows_.push_back(r);
        textBoxRow_ = &rows_.back();
    }
    layoutSeg_ = segments({L"Left", L"Right", L"Top", L"Bottom"});
    layoutRow_ = row(L"Image", {{layoutSeg_, 0, 0}});
    alignSeg_ = segments({L"Left", L"Center", L"Right"});
    alignRow_ = row(L"Alignment", {{alignSeg_, 0, 0}});
    textSize_ = slider(16, 240, 72, &textSizeValue_);
    textSizeRow_ = sliderRow(L"Text size", textSize_, textSizeValue_);

    // COLORS
    section(L"Colors");
    colorModeSeg_ = segments({L"Mono", L"Original"});
    row(L"Mode", {{colorModeSeg_, 0, 0}});
    colorTargetSeg_ = segments({L"Glyphs", L"Background", L"Both"});
    colorTargetRow_ = row(L"Paint", {{colorTargetSeg_, 0, 0}});
    strength_ = slider(0, 1, 1, &strengthValue_);
    strengthRow_ = sliderRow(L"Strength", strength_, strengthValue_);
    saturation_ = slider(0, 2, 1, &saturationValue_);
    saturationRow_ = sliderRow(L"Saturation", saturation_, saturationValue_);
    colorBright_ = slider(-1, 1, 0, &colorBrightValue_);
    colorBrightRow_ = sliderRow(L"Lift", colorBright_, colorBrightValue_);
    bgSwatch_ = make<ColorSwatch>();
    bgHex_ = editBox(false, kIdBgHex);
    SendMessageW(bgHex_->edit, EM_SETLIMITTEXT, 7, 0);
    row(L"Background", {{bgSwatch_, 56, 24}, {bgHex_, 84, 26}})->gap = 10;
    fgSwatch_ = make<ColorSwatch>();
    fgHex_ = editBox(false, kIdFgHex);
    SendMessageW(fgHex_->edit, EM_SETLIMITTEXT, 7, 0);
    row(L"Glyphs", {{fgSwatch_, 56, 24}, {fgHex_, 84, 26}})->gap = 10;
    std::vector<Item> swatches;
    for (const ColorPreset& p : kPresets) {
        PresetSwatch* s = make<PresetSwatch>(p.background, p.glyph);
        s->onChange = [this, p] {
            job_.colors.background = toColor(p.background);
            job_.colors.glyph = toColor(p.glyph);
            colorsChanged();
        };
        presets_.push_back(s);
        swatches.push_back({s, 28, 26});
    }
    row(L"Presets", swatches)->gap = 4;

    // Preview caption and the bottom bar.
    caption_ = make<Label>(L"", Font::Mono11, theme::Faint, Align::Center);
    resLabel_ = make<Label>(L"Resolution", Font::Ui, theme::Dim);
    resSeg_ = segments({L"Auto", L"Custom"});
    widthField_ = editBox(false, kIdWidth, ES_NUMBER | ES_CENTER);
    heightField_ = editBox(false, kIdHeight, ES_NUMBER | ES_CENTER);
    SendMessageW(widthField_->edit, EM_SETLIMITTEXT, 5, 0);
    SendMessageW(heightField_->edit, EM_SETLIMITTEXT, 5, 0);
    timesLabel_ = make<Label>(L"×", Font::Ui, theme::Faint, Align::Center);
    formatLabel_ = make<Label>(L"Format", Font::Ui, theme::Dim);
    formatSeg_ = segments({L"PNG", L"JPEG"});
    saveButton_ = make<Button>(L"Save…", true);
    saveButton_->onChange = [this] { saveDialog(); };

    createEditFonts();
}

// ---- layout ------------------------------------------------------------------------------------

float MainWindow::width() const {
    RECT r;
    GetClientRect(hwnd_, &r);
    return float(r.right) / scale_;
}

float MainWindow::height() const {
    RECT r;
    GetClientRect(hwnd_, &r);
    return float(r.bottom) / scale_;
}

void MainWindow::layout() {
    if (!photoCard_) return;
    layoutSidebar();
    layoutBar();
    const D2D1_RECT_F sidebar = D2D1::RectF(0, 0, kSidebarWidth, height());
    const D2D1_RECT_F bar = D2D1::RectF(kSidebarWidth, height() - kBarHeight, width(), height());
    for (EditBox* e : {customChars_, textBox_, bgHex_, fgHex_}) e->place(scale_, sidebar);
    for (EditBox* e : {widthField_, heightField_}) e->place(scale_, bar);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::layoutSidebar() {
    // Content height first, to clamp the scroll position.
    float total = 20;
    bool first = true;
    for (const Row& r : rows_) {
        if (!r.visible) continue;
        if (!first) total += r.kind == Row::Section ? 26 : 12;
        first = false;
        total += r.height;
    }
    contentHeight_ = total + 24;
    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, contentHeight_ - height()));

    float y = 20 - scrollY_;
    first = true;
    for (Row& r : rows_) {
        if (r.label) r.label->visible = r.visible;
        for (Item& it : r.items) it.widget->visible = r.visible;
        if (!r.visible) continue;
        if (!first) y += r.kind == Row::Section ? 26 : 12;
        first = false;
        float x = kPad;
        const float right = kSidebarWidth - kPad;
        if (r.kind == Row::Section) {
            r.label->rc = D2D1::RectF(x, y, right, y + r.height);
        } else if (r.kind == Row::Labeled) {
            r.label->rc = D2D1::RectF(x, y, x + kLabelWidth, y + r.height);
            x += kLabelWidth + 8;
        }
        float fixed = 0;
        int flex = 0;
        for (const Item& it : r.items) {
            fixed += it.width;
            flex += it.width == 0;
        }
        const float gaps = r.gap * float(r.items.empty() ? 0 : r.items.size() - 1);
        const float flexWidth = flex ? std::max(0.0f, (right - x - fixed - gaps) / flex) : 0;
        for (Item& it : r.items) {
            const float w = it.width ? it.width : flexWidth;
            const float h = it.height ? it.height : r.height;
            const float top = y + std::floor((r.height - h) / 2);
            it.widget->rc = D2D1::RectF(x, top, x + w, top + h);
            x += w + r.gap;
        }
        y += r.height;
    }

    // The Choose… button sits inside the photo card.
    const D2D1_RECT_F card = photoCard_->rc;
    const float cy = std::floor((card.top + card.bottom) / 2);
    chooseButton_->visible = photoCard_->visible;
    chooseButton_->rc = D2D1::RectF(card.right - 10 - 86, cy - 14, card.right - 10, cy + 14);
    photoCard_->buttonLeft = chooseButton_->rc.left;
}

void MainWindow::layoutBar() {
    const float w = width(), h = height();
    const float cy = std::floor(h - kBarHeight / 2);
    auto place = [cy](Widget* wd, float x, float width, float height) {
        wd->rc = D2D1::RectF(x, cy - height / 2, x + width, cy + height / 2);
        return x + width;
    };
    float x = kSidebarWidth + 1 + 24;
    x = place(resLabel_, x, 70, 28) + 10;
    x = place(resSeg_, x, 130, 28) + 10;
    x = place(widthField_, x, 64, 26) + 4;
    x = place(timesLabel_, x, 12, 28) + 4;
    place(heightField_, x, 64, 26);

    float r = w - 20;
    r -= 92;
    place(saveButton_, r, 92, 32);
    r -= 18 + 120;
    place(formatSeg_, r, 120, 28);
    r -= 10 + 50;
    place(formatLabel_, r, 50, 28);

    const D2D1_RECT_F frame = previewFrame();
    caption_->rc = D2D1::RectF(kSidebarWidth + 1, frame.bottom + 8, w, frame.bottom + 28);
}

// The wallpaper preview: aspect-fit, in the proportions of the output.
D2D1_RECT_F MainWindow::previewFrame() const {
    const float left = kSidebarWidth + 1 + 40, right = width() - 40;
    const float top = 32, bottom = height() - kBarHeight - 24 - 14 - 14;
    const float ow = float(std::max(1, job_.settings.width)), oh = float(std::max(1, job_.settings.height));
    const float s = std::max(0.0f, std::min((right - left) / ow, (bottom - top) / oh));
    // Whole physical pixels, so the preview bitmap maps 1:1.
    const float pw = std::floor(ow * s * scale_), ph = std::floor(oh * s * scale_);
    const float x = std::floor(((left + right) / 2) * scale_ - pw / 2);
    const float y = std::floor(((top + bottom) / 2) * scale_ - ph / 2);
    return D2D1::RectF(x / scale_, y / scale_, (x + pw) / scale_, (y + ph) / scale_);
}

bool MainWindow::scrollbarThumb(D2D1_RECT_F* out) const {
    const float h = height();
    if (contentHeight_ <= h) return false;
    const float thumb = std::max(30.0f, h * h / contentHeight_);
    const float top = (h - thumb) * scrollY_ / (contentHeight_ - h);
    *out = D2D1::RectF(kSidebarWidth - 7, top + 2, kSidebarWidth - 3, top + thumb - 2);
    return true;
}

void MainWindow::scrollTo(float y) {
    const float clamped = std::clamp(y, 0.0f, std::max(0.0f, contentHeight_ - height()));
    if (clamped == scrollY_) return;
    scrollY_ = clamped;
    layout();
}

// ---- painting --------------------------------------------------------------------------------------

void MainWindow::ensureTarget() {
    if (target_) return;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96 * scale_, 96 * scale_);
    d2d_->CreateDCRenderTarget(&props, target_.GetAddressOf());
    if (target_) target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    previewBitmap_.Reset();
    previewStale_ = true;
    photoCard_->thumb = nullptr;
}

void MainWindow::paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    ensureTarget();
    RECT client;
    GetClientRect(hwnd_, &client);
    if (target_ && SUCCEEDED(target_->BindDC(dc, &client))) {
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->Clear(D2D1::ColorF(theme::Window));
        Canvas c(target_.Get());
        const float w = width(), h = height();

        // Sidebar.
        c.fill(D2D1::RectF(0, 0, kSidebarWidth, h), theme::Sidebar);
        if (!photoCard_->thumb && thumbImage_.width > 0) {
            const std::vector<uint8_t> px = scaleToBgra(thumbImage_, thumbImage_.width, thumbImage_.height);
            thumbBitmap_.Reset();
            target_->CreateBitmap(
                D2D1::SizeU(UINT32(thumbImage_.width), UINT32(thumbImage_.height)), px.data(),
                UINT32(thumbImage_.width * 4),
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                thumbBitmap_.ReleaseAndGetAddressOf());
            photoCard_->thumb = thumbBitmap_.Get();
        }
        c.fill(D2D1::RectF(kSidebarWidth, 0, kSidebarWidth + 1, h), theme::Line);

        // Preview, caption and bottom bar.
        paintPreview(c);
        c.fill(D2D1::RectF(kSidebarWidth + 1, h - kBarHeight, w, h), theme::Sidebar);
        c.fill(D2D1::RectF(kSidebarWidth + 1, h - kBarHeight, w, h - kBarHeight + 1), theme::Line);

        for (const auto& wd : widgets_)
            if (wd->visible) wd->paint(c, wd.get() == hot_ || wd.get() == pressed_);
        paintScrollbar(c);

        if (target_->EndDraw() == HRESULT(D2DERR_RECREATE_TARGET)) target_.Reset();
    }
    EndPaint(hwnd_, &ps);
}

void MainWindow::paintPreview(Canvas& c) {
    const D2D1_RECT_F r = previewFrame();
    c.fill(r, theme::Preview);
    const int pw = int(std::lround((r.right - r.left) * scale_)), ph = int(std::lround((r.bottom - r.top) * scale_));
    if (previewImage_ && pw > 0 && ph > 0 &&
        (previewStale_ || !previewBitmap_ || pw != previewBitmapW_ || ph != previewBitmapH_)) {
        const std::vector<uint8_t> px = scaleToBgra(*previewImage_, pw, ph);
        previewBitmap_.Reset();
        target_->CreateBitmap(D2D1::SizeU(UINT32(pw), UINT32(ph)), px.data(), UINT32(pw * 4),
                              D2D1::BitmapProperties(
                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                  96 * scale_, 96 * scale_),
                              previewBitmap_.GetAddressOf());
        previewBitmapW_ = pw;
        previewBitmapH_ = ph;
        previewStale_ = false;
    }
    if (previewBitmap_) {
        // Stretched while a resize waits for the rescale on the next paint.
        target_->DrawBitmap(previewBitmap_.Get(), r, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (!job_.photo)
        c.text(L"drop a photo here  ·  Ctrl+O", r, Font::Mono13, theme::Dim, Align::Center);
    c.stroke(inset(r, -1, -1), dropping_ ? theme::Accent : theme::Line, 0, dropping_ ? 2.0f : 1.0f);
}

void MainWindow::paintScrollbar(Canvas& c) {
    D2D1_RECT_F thumb;
    if (scrollbarThumb(&thumb)) c.fill(thumb, scrolling_ ? theme::Dim : theme::Faint, 2);
}

// ---- settings <-> controls --------------------------------------------------------------------------

// Pushes the current settings into every control.
void MainWindow::syncControls() {
    const WallpaperSettings& s = job_.settings;
    fitPopup_->selected = int(s.grid.fit);
    brightness_->value = s.grid.adjust.brightness;
    contrast_->value = s.grid.adjust.contrast;
    invert_->checked = s.grid.adjust.invert;
    fontSize_->value = job_.fontSize;
    textSize_->value = job_.textSize;
    bgSwatch_->color = job_.colors.background;
    fgSwatch_->color = job_.colors.glyph;
    layoutSeg_->selected = int(s.layout);
    colorModeSeg_->selected = int(s.grid.color.mode);
    colorTargetSeg_->selected = int(s.grid.color.target);
    strength_->value = s.grid.color.strength;
    saturation_->value = s.grid.color.saturation;
    colorBright_->value = s.grid.color.brightness;
    alignSeg_->selected = int(s.align);
    resSeg_->selected = autoResolution_ ? 0 : 1;
    formatSeg_->selected = format_ == ImageFormat::Png ? 0 : 1;
    syncing_ = true;
    widthField_->setText(std::to_wstring(s.width));
    heightField_->setText(std::to_wstring(s.height));
    syncing_ = false;
    updateDerivedViews();
}

// Value labels, hex fields, which rows are visible.
void MainWindow::updateDerivedViews() {
    fontSizeValue_->text = std::to_wstring(int(std::lround(job_.fontSize)));
    textSizeValue_->text = std::to_wstring(int(std::lround(job_.textSize)));
    bgSwatch_->color = job_.colors.background;
    fgSwatch_->color = job_.colors.glyph;
    syncing_ = true;
    // Leave a field alone while it is being typed into.
    if (GetFocus() != bgHex_->edit) bgHex_->setText(hexString(job_.colors.background));
    if (GetFocus() != fgHex_->edit) fgHex_->setText(hexString(job_.colors.glyph));
    syncing_ = false;
    const int mode = modeSeg_->selected;
    textBoxRow_->visible = mode != kTextOff;
    layoutRow_->visible = alignRow_->visible = textSizeRow_->visible = mode == kTextSide;
    customRow_->visible = charsetPopup_->selected == 3;
    const ColorSettings& color = job_.settings.grid.color;
    colorTargetRow_->visible = strengthRow_->visible = saturationRow_->visible = colorBrightRow_->visible =
        color.mode == ColorMode::Original;
    strengthValue_->text = wformat(L"%d%%", int(std::lround(color.strength * 100)));
    saturationValue_->text = wformat(L"%d%%", int(std::lround(color.saturation * 100)));
    colorBrightValue_->text = wformat(L"%+d", int(std::lround(color.brightness * 100)));
    EnableWindow(widthField_->edit, !autoResolution_);
    EnableWindow(heightField_->edit, !autoResolution_);
    updateCaption();
    layout();
}

void MainWindow::updateCaption() {
    const WallpaperSettings& s = job_.settings;
    std::wstring mode = L"ASCII";
    if (modeSeg_->selected == kTextSide) {
        const wchar_t* where[] = {L"image left", L"image right", L"image top", L"image bottom"};
        mode = std::wstring(L"Side by side, ") + where[int(s.layout)];
    } else if (modeSeg_->selected == kTextImage) {
        mode = L"Text as image";
    }
    if (s.grid.color.mode == ColorMode::Original) mode += L", original colors";
    const std::wstring ms = lastRenderMs_ > 0 ? wformat(L" · %.0f ms", lastRenderMs_) : L"";
    caption_->text = wformat(L"live preview · %d × %d · ", s.width, s.height) + mode + ms;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

Charset MainWindow::selectedCharset() const {
    switch (charsetPopup_->selected) {
    case 1: return charsetPreset(CharsetPreset::Detailed);
    case 2: return charsetPreset(CharsetPreset::Blocks);
    case 3: {
        Charset c = charsetFromUtf8(toUtf8(customChars_->textValue()));
        return c.chars.empty() ? charsetPreset(CharsetPreset::Simple) : c;
    }
    default: return charsetPreset(CharsetPreset::Simple);
    }
}

// Reads every control back into the settings.
void MainWindow::readControls() {
    WallpaperSettings& s = job_.settings;
    s.grid.fit = FitMode(std::max(0, fitPopup_->selected));
    s.grid.adjust.brightness = float(brightness_->value);
    s.grid.adjust.contrast = float(contrast_->value);
    s.grid.adjust.invert = invert_->checked;
    s.grid.charset = selectedCharset();
    job_.font = fonts_[size_t(std::clamp(fontPopup_->selected, 0, int(fonts_.size()) - 1))];
    job_.fontSize = std::round(fontSize_->value);
    job_.textSize = std::round(textSize_->value);

    const int mode = modeSeg_->selected;
    s.grid.mode = mode == kTextImage ? TextMode::TextAsImage : TextMode::Ascii;
    s.sideBySide = mode == kTextSide;
    s.grid.text = toUtf8(toLf(textBox_->textValue()));
    s.layout = SideLayout(std::max(0, layoutSeg_->selected));
    s.align = TextAlign(std::max(0, alignSeg_->selected));
    s.grid.color.mode = colorModeSeg_->selected == 1 ? ColorMode::Original : ColorMode::Mono;
    s.grid.color.target = ColorTarget(std::clamp(colorTargetSeg_->selected, 0, 2));
    s.grid.color.strength = float(strength_->value);
    s.grid.color.saturation = float(saturation_->value);
    s.grid.color.brightness = float(colorBright_->value);

    autoResolution_ = resSeg_->selected == 0;
    if (autoResolution_) {
        updateAutoResolution();
    } else {
        auto number = [](EditBox* e, int fallback) {
            const std::wstring t = e->textValue();
            return t.empty() ? fallback : std::clamp(_wtoi(t.c_str()), 16, 16384);
        };
        s.width = number(widthField_, s.width);
        s.height = number(heightField_, s.height);
    }
    format_ = formatSeg_->selected == 1 ? ImageFormat::Jpeg : ImageFormat::Png;
}

// Auto resolution: the pixel size of the monitor the window is on.
void MainWindow::updateAutoResolution() {
    if (!autoResolution_) return;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
    const int w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (w == job_.settings.width && h == job_.settings.height) return;
    job_.settings.width = w;
    job_.settings.height = h;
    syncing_ = true;
    widthField_->setText(std::to_wstring(w));
    heightField_->setText(std::to_wstring(h));
    syncing_ = false;
}

// ---- remembered settings ----
// Controls are the source of truth, so they are what gets stored.

void MainWindow::scheduleSave() { SetTimer(hwnd_, kTimerSave, 400, nullptr); }

void MainWindow::saveSettings() {
    KillTimer(hwnd_, kTimerSave);
    if (!settingsKey_) return;
    auto put = [this](const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(settingsKey_, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       DWORD((v.size() + 1) * sizeof(wchar_t)));
    };
    auto num = [&](const wchar_t* name, double v) { put(name, wformat(L"%.4f", v)); };
    num(L"fit", fitPopup_->selected);
    num(L"brightness", brightness_->value);
    num(L"contrast", contrast_->value);
    num(L"invert", invert_->checked);
    num(L"charset", charsetPopup_->selected);
    put(L"customChars", customChars_->textValue());
    put(L"font", fonts_[size_t(std::clamp(fontPopup_->selected, 0, int(fonts_.size()) - 1))]);
    num(L"fontSize", fontSize_->value);
    num(L"textMode", modeSeg_->selected);
    put(L"text", toLf(textBox_->textValue()));
    num(L"layout", layoutSeg_->selected);
    num(L"align", alignSeg_->selected);
    num(L"textSize", textSize_->value);
    put(L"background", hexString(job_.colors.background));
    put(L"glyph", hexString(job_.colors.glyph));
    num(L"colorMode", colorModeSeg_->selected);
    num(L"colorTarget", colorTargetSeg_->selected);
    num(L"colorStrength", strength_->value);
    num(L"saturation", saturation_->value);
    num(L"colorBrightness", colorBright_->value);
    num(L"autoResolution", autoResolution_);
    num(L"width", job_.settings.width);
    num(L"height", job_.settings.height);
    num(L"format", formatSeg_->selected);

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd_, &wp)) {
        RegSetValueExW(settingsKey_, L"windowRect", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp.rcNormalPosition),
                       sizeof(RECT));
        const DWORD maximized = wp.showCmd == SW_SHOWMAXIMIZED;
        RegSetValueExW(settingsKey_, L"windowMaximized", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&maximized),
                       sizeof(maximized));
    }
}

void MainWindow::loadSettings() {
    if (!settingsKey_) return;
    auto str = [this](const wchar_t* name, std::wstring& out) {
        DWORD size = 0;
        if (RegGetValueW(settingsKey_, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
            return false;
        std::wstring v(size / sizeof(wchar_t) + 1, L'\0');
        size = DWORD(v.size() * sizeof(wchar_t));
        if (RegGetValueW(settingsKey_, nullptr, name, RRF_RT_REG_SZ, nullptr, v.data(), &size) != ERROR_SUCCESS)
            return false;
        v.resize(wcslen(v.c_str()));
        out = v;
        return true;
    };
    auto num = [&](const wchar_t* name, double& out) {
        std::wstring v;
        if (!str(name, v) || v.empty()) return false;
        wchar_t* end = nullptr;
        const double d = wcstod(v.c_str(), &end);
        if (end == v.c_str() || !std::isfinite(d)) return false;
        out = d;
        return true;
    };
    double d;
    std::wstring s;
    auto index = [&](const wchar_t* name, int& selected, size_t count) {
        if (num(name, d) && d >= 0 && d < double(count)) selected = int(d);
    };
    auto value = [&](const wchar_t* name, Slider* sl) {
        if (num(name, d)) sl->value = std::clamp(d, sl->min, sl->max);
    };
    index(L"fit", fitPopup_->selected, fitPopup_->items.size());
    value(L"brightness", brightness_);
    value(L"contrast", contrast_);
    if (num(L"invert", d)) invert_->checked = d != 0;
    index(L"charset", charsetPopup_->selected, charsetPopup_->items.size());
    syncing_ = true;
    if (str(L"customChars", s)) customChars_->setText(s);
    if (str(L"text", s)) textBox_->setText(toCrlf(s));
    syncing_ = false;
    if (str(L"font", s)) {
        auto it = std::find(fonts_.begin(), fonts_.end(), s);
        if (it != fonts_.end()) fontPopup_->selected = int(it - fonts_.begin());
    }
    value(L"fontSize", fontSize_);
    index(L"textMode", modeSeg_->selected, modeSeg_->labels.size());
    index(L"layout", layoutSeg_->selected, layoutSeg_->labels.size());
    index(L"align", alignSeg_->selected, alignSeg_->labels.size());
    value(L"textSize", textSize_);
    Color c;
    if (str(L"background", s) && parseHexColor(toUtf8(s), c)) job_.colors.background = c;
    if (str(L"glyph", s) && parseHexColor(toUtf8(s), c)) job_.colors.glyph = c;
    index(L"colorMode", colorModeSeg_->selected, colorModeSeg_->labels.size());
    index(L"colorTarget", colorTargetSeg_->selected, colorTargetSeg_->labels.size());
    value(L"colorStrength", strength_);
    value(L"saturation", saturation_);
    value(L"colorBrightness", colorBright_);
    if (num(L"autoResolution", d)) resSeg_->selected = d != 0 ? 0 : 1;
    syncing_ = true;
    if (num(L"width", d)) widthField_->setText(std::to_wstring(int(d)));
    if (num(L"height", d)) heightField_->setText(std::to_wstring(int(d)));
    syncing_ = false;
    index(L"format", formatSeg_->selected, formatSeg_->labels.size());
    readControls();
    updateDerivedViews();
}

// ---- actions ---------------------------------------------------------------------------------------------

void MainWindow::changed(Widget* sender) {
    if (syncing_ || !worker_) return;  // controls set from code, or still being built
    if (sender == bgSwatch_ || sender == fgSwatch_) {
        job_.colors.background = bgSwatch_->color;
        job_.colors.glyph = fgSwatch_->color;
        colorsChanged();
        return;
    }
    readControls();
    updateDerivedViews();
    scheduleSave();
    if (sender != formatSeg_) scheduleRender();
}

void MainWindow::colorsChanged() {
    updateDerivedViews();
    scheduleSave();
    scheduleRender();
}

// Typed colors apply as soon as they parse.
void MainWindow::hexEdited(EditBox* box) {
    if (!worker_) return;
    Color c;
    if (!parseHexColor(toUtf8(box->textValue()), c)) return;
    Color& target = box == bgHex_ ? job_.colors.background : job_.colors.glyph;
    if (c == target) return;
    target = c;
    colorsChanged();
}

void MainWindow::scheduleRender() {
    if (worker_) worker_->preview(job_);
}

void MainWindow::openDialog() {
    wchar_t path[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"Images\0*.jpg;*.jpeg;*.png;*.heic;*.heif;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.jxr;*.avif\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = DWORD(std::size(path));
    ofn.lpstrTitle = L"Choose Photo";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) openPhoto(path);
}

void MainWindow::openPhoto(const std::wstring& path) {
    photoCard_->name = fileName(path);
    photoCard_->info = L"loading…";
    InvalidateRect(hwnd_, nullptr, FALSE);
    HWND hwnd = hwnd_;
    std::thread([hwnd, path] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto* result = new PhotoResult;
        result->path = path;
        auto photo = std::make_shared<GrayImage>(loadPhoto(path, &result->error));
        result->thumb = photoThumbnail(*photo, 128, 88);
        result->photo = std::move(photo);
        if (!PostMessageW(hwnd, kMsgPhoto, 0, reinterpret_cast<LPARAM>(result))) delete result;
        CoUninitialize();
    }).detach();
}

void MainWindow::saveDialog() {
    readControls();
    const bool jpeg = format_ == ImageFormat::Jpeg;
    const std::wstring base = photoName_.empty() ? L"wallpaper" : stripExtension(photoName_);
    std::wstring name = base + L"-ascii." + (jpeg ? L"jpg" : L"png");
    wchar_t path[MAX_PATH * 4] = L"";
    name.copy(path, std::min(name.size(), std::size(path) - 1));
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = jpeg ? L"JPEG image\0*.jpg;*.jpeg\0" : L"PNG image\0*.png\0";
    ofn.lpstrDefExt = jpeg ? L"jpg" : L"png";
    ofn.lpstrFile = path;
    ofn.nMaxFile = DWORD(std::size(path));
    ofn.lpstrTitle = L"Save Wallpaper";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetSaveFileNameW(&ofn) && worker_) worker_->save(job_, path, format_);
}

void MainWindow::showError(const std::wstring& title, const std::string& detail) {
    MessageBoxW(hwnd_, toWide(detail).c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
}

// ---- mouse ---------------------------------------------------------------------------------------------------

Widget* MainWindow::widgetAt(float x, float y) const {
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        Widget* w = it->get();
        if (w->clickable() && w->enabled && w->contains(x, y)) return w;
    }
    return nullptr;
}

void MainWindow::mouseDown(float x, float y, bool doubleClick) {
    SetFocus(hwnd_);  // clicking outside a text field ends editing
    D2D1_RECT_F thumb;
    if (x >= kSidebarWidth - 10 && x < kSidebarWidth && scrollbarThumb(&thumb)) {
        if (y < thumb.top || y > thumb.bottom) {
            // Page towards the click.
            scrollTo(scrollY_ + (y < thumb.top ? -1 : 1) * height() * 0.8f);
        } else {
            scrolling_ = true;
            scrollGrabY_ = y;
            scrollGrabStart_ = scrollY_;
            SetCapture(hwnd_);
        }
        return;
    }
    Widget* w = widgetAt(x, y);
    if (!w) return;
    pressed_ = w;
    w->press(host_, x, y);
    if (doubleClick) w->doubleClick();
    // Menus and dialogs opened by press() swallow the mouse release.
    if (pressed_ && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        SetCapture(hwnd_);
    } else {
        pressed_ = nullptr;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::mouseMove(float x, float y) {
    if (!trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        trackingLeave_ = TrackMouseEvent(&tme);
    }
    if (scrolling_) {
        D2D1_RECT_F thumb;
        if (scrollbarThumb(&thumb)) {
            const float h = height();
            const float track = h - (thumb.bottom - thumb.top + 4);
            if (track > 0) scrollTo(scrollGrabStart_ + (y - scrollGrabY_) * (contentHeight_ - h) / track);
        }
        return;
    }
    if (pressed_) {
        pressed_->drag(x, y);
        return;
    }
    Widget* w = widgetAt(x, y);
    int segIndex = -1;
    if (auto* seg = dynamic_cast<Segments*>(w)) {
        segIndex = seg->indexAt(x);
        if (seg->hotIndex != segIndex) {
            seg->hotIndex = segIndex;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (w != hot_) {
        hot_ = w;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::mouseUp(float x, float y) {
    if (scrolling_) {
        scrolling_ = false;
        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    Widget* w = pressed_;
    pressed_ = nullptr;
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (w) w->release(x, y);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- messages ----------------------------------------------------------------------------------------------------

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    auto dipX = [this](LPARAM l) { return float(GET_X_LPARAM(l)) / scale_; };
    auto dipY = [this](LPARAM l) { return float(GET_Y_LPARAM(l)) / scale_; };

    switch (msg) {
    case WM_CREATE: {
        scale_ = float(GetDpiForWindow(hwnd_)) / 96.0f;
        host_ = {hwnd_, scale_};
        // Dark title bar (Windows 10 2004+), title bar in the window color (Windows 11).
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        const COLORREF caption = toColorRef(theme::Window), text = toColorRef(theme::Text);
        DwmSetWindowAttribute(hwnd_, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd_, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));

        buildUI();
        syncControls();
        loadSettings();
        worker_ = std::make_unique<RenderWorker>(hwnd_, kMsgRendered);
        dropTarget_ = new DropTarget(
            [this](bool on) {
                dropping_ = on;
                InvalidateRect(hwnd_, nullptr, FALSE);
            },
            [this](const std::wstring& path) { openPhoto(path); });
        RegisterDragDrop(hwnd_, dropTarget_);
        return 0;
    }
    case WM_DESTROY:
        saveSettings();
        RevokeDragDrop(hwnd_);
        if (dropTarget_) dropTarget_->Release();
        dropTarget_ = nullptr;
        worker_.reset();
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint();
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {LONG(1000 * scale_), LONG(700 * scale_)};
        return 0;
    }
    case WM_DPICHANGED: {
        scale_ = float(HIWORD(wp)) / 96.0f;
        host_.scale = scale_;
        target_.Reset();
        createEditFonts();
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_WINDOWPOSCHANGED:
    case WM_DISPLAYCHANGE:
        if (autoResolution_) {
            const int w = job_.settings.width, h = job_.settings.height;
            updateAutoResolution();
            if (w != job_.settings.width || h != job_.settings.height) {
                updateDerivedViews();
                scheduleRender();
            }
        }
        break;
    case WM_TIMER:
        if (wp == kTimerSave) saveSettings();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id == int(kCmdOpen) && !lp) {
            openDialog();
            return 0;
        }
        if (id == int(kCmdSave) && !lp) {
            saveDialog();
            return 0;
        }
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            if (code == EN_KILLFOCUS && (id == kIdBgHex || id == kIdFgHex)) updateDerivedViews();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (code == EN_CHANGE && !syncing_) {
            if (id == kIdBgHex) hexEdited(bgHex_);
            else if (id == kIdFgHex) hexEdited(fgHex_);
            else changed(nullptr);
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const bool enabled = IsWindowEnabled(reinterpret_cast<HWND>(lp));
        SetTextColor(dc, toColorRef(enabled ? theme::Text : theme::Faint));
        SetBkColor(dc, toColorRef(theme::Preview));
        return reinterpret_cast<LRESULT>(editBrush_);
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        mouseDown(dipX(lp), dipY(lp), msg == WM_LBUTTONDBLCLK);
        return 0;
    case WM_MOUSEMOVE:
        mouseMove(dipX(lp), dipY(lp));
        return 0;
    case WM_LBUTTONUP:
        mouseUp(dipX(lp), dipY(lp));
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != hwnd_) {
            pressed_ = nullptr;
            scrolling_ = false;
        }
        return 0;
    case WM_MOUSELEAVE:
        trackingLeave_ = false;
        if (hot_) {
            hot_ = nullptr;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &p);
        if (float(p.x) / scale_ < kSidebarWidth) scrollTo(scrollY_ - GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 60);
        return 0;
    }
    case kMsgRendered: {
        std::unique_ptr<RenderResult> r(reinterpret_cast<RenderResult*>(lp));
        if (r->save) {
            if (!r->ok) showError(L"Couldn't save the wallpaper", r->error);
        } else {
            previewImage_ = r->image;
            previewStale_ = true;
            lastRenderMs_ = r->ms;
            updateCaption();
        }
        return 0;
    }
    case kMsgPhoto: {
        std::unique_ptr<PhotoResult> r(reinterpret_cast<PhotoResult*>(lp));
        if (r->photo->empty()) {
            if (job_.photo) {
                photoCard_->name = photoName_;
                photoCard_->info = wformat(L"%d × %d", job_.photo->width, job_.photo->height);
            } else {
                photoCard_->name = L"no photo";
                photoCard_->info = L"jpeg · png · webp · heic";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            showError(L"Can't open this photo", r->error);
            return 0;
        }
        job_.photo = r->photo;
        photoName_ = fileName(r->path);
        thumbImage_ = std::move(r->thumb);
        photoCard_->thumb = nullptr;
        photoCard_->name = photoName_;
        photoCard_->info = wformat(L"%d × %d", job_.photo->width, job_.photo->height);
        updateDerivedViews();
        scheduleRender();
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
