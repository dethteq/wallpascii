#include "Ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "DWriteRasterizer.h"

using Microsoft::WRL::ComPtr;
using namespace wallpascii;

COLORREF toColorRef(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
uint32_t toRgb(Color c) { return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b; }
Color toColor(uint32_t rgb) { return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)}; }

std::wstring hexString(Color c) {
    wchar_t buf[8];
    swprintf(buf, 8, L"#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

const std::wstring& uiMonoFamily() {
    static const std::wstring family = hasFontFamily(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    return family;
}

// ---- canvas ---------------------------------------------------------------------

Canvas::Canvas(ID2D1RenderTarget* rt) : rt_(rt) { rt_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_); }

ID2D1SolidColorBrush* Canvas::brush(uint32_t rgb) {
    brush_->SetColor(D2D1::ColorF(rgb));
    return brush_.Get();
}

IDWriteTextFormat* Canvas::format(Font font) {
    // Device independent, so shared by every canvas.
    static std::map<Font, ComPtr<IDWriteTextFormat>> formats;
    auto it = formats.find(font);
    if (it != formats.end()) return it->second.Get();
    const bool mono = font != Font::Ui && font != Font::UiSmall && font != Font::UiMedium;
    float size = 13;
    if (font == Font::Mono11) size = 11;
    else if (font == Font::UiSmall || font == Font::Mono12 || font == Font::MonoSemibold12) size = 12;
    const DWRITE_FONT_WEIGHT weight = font == Font::UiMedium || font == Font::MonoSemibold12
                                          ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                          : DWRITE_FONT_WEIGHT_REGULAR;
    ComPtr<IDWriteTextFormat> f;
    dwriteFactory()->CreateTextFormat(mono ? uiMonoFamily().c_str() : L"Segoe UI", nullptr, weight,
                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
    if (f) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ComPtr<IDWriteInlineObject> ellipsis;
        dwriteFactory()->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trimming, ellipsis.Get());
    }
    return formats.emplace(font, f).first->second.Get();
}

void Canvas::fill(const D2D1_RECT_F& r, uint32_t rgb, float radius) {
    if (radius > 0) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(rgb));
    else rt_->FillRectangle(r, brush(rgb));
}

void Canvas::stroke(const D2D1_RECT_F& r, uint32_t rgb, float radius, float width) {
    const float h = width / 2;
    const D2D1_RECT_F in = D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h);
    if (radius > 0) rt_->DrawRoundedRectangle(D2D1::RoundedRect(in, radius, radius), brush(rgb), width);
    else rt_->DrawRectangle(in, brush(rgb), width);
}

void Canvas::line(float x0, float y0, float x1, float y1, uint32_t rgb, float width) {
    rt_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush(rgb), width);
}

void Canvas::circle(float cx, float cy, float radius, uint32_t rgb) {
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush(rgb));
}

void Canvas::text(const std::wstring& s, const D2D1_RECT_F& r, Font font, uint32_t rgb, Align align) {
    IDWriteTextFormat* f = format(font);
    if (!f || s.empty()) return;
    f->SetTextAlignment(align == Align::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                 : DWRITE_TEXT_ALIGNMENT_TRAILING);
    rt_->DrawText(s.c_str(), UINT32(s.size()), f, r, brush(rgb), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Canvas::textWidth(const std::wstring& s, Font font) {
    IDWriteTextFormat* f = format(font);
    ComPtr<IDWriteTextLayout> layout;
    if (!f || FAILED(dwriteFactory()->CreateTextLayout(s.c_str(), UINT32(s.size()), f, 10000, 100, &layout)))
        return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

POINT Host::toScreen(float x, float y) const {
    POINT p{LONG(std::lround(x * scale)), LONG(std::lround(y * scale))};
    ClientToScreen(hwnd, &p);
    return p;
}

// ---- widgets --------------------------------------------------------------------

void Slider::paint(Canvas& c, bool hot) {
    const float cy = std::floor((rc.top + rc.bottom) / 2) + 0.5f;
    const float x0 = rc.left + 7, x1 = rc.right - 7;
    const float t = float(std::clamp((value - min) / (max - min), 0.0, 1.0));
    const float kx = x0 + (x1 - x0) * t;
    c.fill(D2D1::RectF(x0 - 2, cy - 2, x1 + 2, cy + 2), theme::Line, 2);
    c.fill(D2D1::RectF(x0 - 2, cy - 2, kx, cy + 2), theme::Accent, 2);
    c.circle(kx, cy, hot ? 7.5f : 7, theme::Text);
}

void Slider::drag(float x, float) {
    const float x0 = rc.left + 7, x1 = rc.right - 7;
    const double t = std::clamp(double(x - x0) / std::max(1.0f, x1 - x0), 0.0, 1.0);
    const double v = min + (max - min) * t;
    if (v != value) {
        value = v;
        changed();
    }
}

void Slider::doubleClick() {
    value = initial;
    changed();
}

int Segments::indexAt(float x) const {
    const int n = int(labels.size());
    const int i = int((x - rc.left) / ((rc.right - rc.left) / n));
    return std::clamp(i, 0, n - 1);
}

void Segments::paint(Canvas& c, bool hot) {
    c.fill(rc, theme::Card, 6);
    const int n = int(labels.size());
    const float w = (rc.right - rc.left) / n;
    for (int i = 0; i < n; ++i) {
        const D2D1_RECT_F seg = D2D1::RectF(rc.left + w * i, rc.top, rc.left + w * (i + 1), rc.bottom);
        const D2D1_RECT_F in = D2D1::RectF(seg.left + 2, seg.top + 2, seg.right - 2, seg.bottom - 2);
        if (i == selected) c.fill(in, theme::Accent, 4);
        else if (hot && i == hotIndex && enabled) c.fill(in, theme::CardHot, 4);
        const uint32_t color = !enabled ? theme::Faint : i == selected ? theme::Text : theme::Dim;
        // A smaller size for labels that would not fit ("Background").
        const std::wstring& label = labels[size_t(i)];
        const Font font = c.textWidth(label, Font::Ui) > w - 8 ? Font::UiSmall : Font::Ui;
        c.text(label, seg, font, color, Align::Center);
    }
}

void Segments::press(Host&, float x, float) {
    if (!enabled) return;
    const int i = indexAt(x);
    if (i != selected) {
        selected = i;
        changed();
    }
}

void Popup::paint(Canvas& c, bool hot) {
    c.fill(rc, hot ? theme::CardHot : theme::Card, 6);
    const std::wstring& title = selected >= 0 && selected < int(items.size()) ? items[size_t(selected)] : L"";
    c.text(title, D2D1::RectF(rc.left + 10, rc.top, rc.right - 26, rc.bottom), Font::Ui, theme::Text);
    const float cx = rc.right - 14, cy = (rc.top + rc.bottom) / 2;
    c.line(cx - 4, cy - 2, cx, cy + 2, theme::Dim, 1.5f);
    c.line(cx, cy + 2, cx + 4, cy - 2, theme::Dim, 1.5f);
}

void Popup::press(Host& host, float, float) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); ++i)
        AppendMenuW(menu, MF_STRING | (int(i) == selected ? MF_CHECKED : 0), i + 1, items[i].c_str());
    const POINT p = host.toScreen(rc.left, rc.bottom + 2);
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, p.x, p.y,
                                   0, host.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd > 0 && cmd - 1 != selected) {
        selected = cmd - 1;
        changed();
    }
}

void Checkbox::paint(Canvas& c, bool hot) {
    const float cy = (rc.top + rc.bottom) / 2;
    const D2D1_RECT_F box = D2D1::RectF(rc.left, cy - 8, rc.left + 16, cy + 8);
    if (checked) {
        c.fill(box, hot ? theme::AccentHot : theme::Accent, 4);
        c.line(box.left + 4, cy, box.left + 7, cy + 3, theme::Text, 1.8f);
        c.line(box.left + 7, cy + 3, box.left + 12, cy - 3, theme::Text, 1.8f);
    } else {
        c.fill(box, theme::Card, 4);
        c.stroke(box, hot ? theme::Dim : theme::Faint, 4);
    }
    c.text(title, D2D1::RectF(rc.left + 24, rc.top, rc.right, rc.bottom), Font::Ui, theme::Text);
}

void Button::paint(Canvas& c, bool hot) {
    if (accent) {
        c.fill(rc, hot ? theme::AccentHot : theme::Accent, 6);
    } else {
        c.fill(rc, hot ? theme::CardHot : theme::Card, 6);
        c.stroke(rc, theme::Line, 6);
    }
    c.text(title, rc, accent ? Font::UiMedium : Font::Ui, theme::Text, Align::Center);
}

void ColorSwatch::paint(Canvas& c, bool hot) {
    c.fill(rc, toRgb(color), 5);
    c.stroke(rc, hot ? theme::Dim : theme::Line, 5);
}

void ColorSwatch::press(Host& host, float, float) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = host.hwnd;
    cc.rgbResult = RGB(color.r, color.g, color.b);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        color = {GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult)};
        changed();
    }
}

void PresetSwatch::paint(Canvas& c, bool hot) {
    c.fill(rc, background, 5);
    c.stroke(rc, hot ? theme::Dim : theme::Line, 5);
    c.text(L"A", rc, Font::MonoSemibold12, glyph, Align::Center);
}

namespace {

// Hands the mouse wheel to the window, so the sidebar scrolls when the pointer
// is over a text field, unless the field has text of its own to scroll.
LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEWHEEL) {
        bool scrollable = false;
        if (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) {
            HDC dc = GetDC(hwnd);
            HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
            TEXTMETRICW tm{};
            GetTextMetricsW(dc, &tm);
            SelectObject(dc, old);
            ReleaseDC(hwnd, dc);
            RECT r;
            GetClientRect(hwnd, &r);
            const LRESULT lines = SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
            scrollable = lines * std::max<LONG>(1, tm.tmHeight) > r.bottom;
        }
        if (!scrollable) return SendMessageW(GetParent(hwnd), msg, wp, lp);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

}  // namespace

EditBox::EditBox(HWND parent, bool multi, int id, DWORD extraStyle) : multiline(multi) {
    DWORD style = WS_CHILD | WS_TABSTOP | extraStyle;
    if (multiline) style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    else style |= ES_AUTOHSCROLL;
    edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    SetWindowTheme(edit, L"DarkMode_Explorer", nullptr);  // dark scroll bar on Windows 10 1809+
    SetWindowSubclass(edit, editProc, 0, 0);
}

void EditBox::paint(Canvas& c, bool) {
    c.fill(rc, theme::Preview, 6);
    c.stroke(rc, GetFocus() == edit ? theme::Accent : theme::Line, 6);
}

void EditBox::place(float scale, const D2D1_RECT_F& clip) {
    const bool show = visible && rc.bottom > clip.top && rc.top < clip.bottom;
    if (!show) {
        ShowWindow(edit, SW_HIDE);
        return;
    }
    // Single-line text is vertically centered in the frame by sizing the
    // control to one line of the edit font.
    RECT r;
    if (multiline) {
        r = {LONG(std::lround((rc.left + 8) * scale)), LONG(std::lround((rc.top + 6) * scale)),
             LONG(std::lround((rc.right - 2) * scale)), LONG(std::lround((rc.bottom - 6) * scale))};
    } else {
        HDC dc = GetDC(edit);
        HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0)));
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(edit, dc);
        const LONG top = LONG(std::lround((rc.top + rc.bottom) / 2 * scale)) - tm.tmHeight / 2;
        r = {LONG(std::lround((rc.left + 8) * scale)), top, LONG(std::lround((rc.right - 8) * scale)),
             top + tm.tmHeight};
    }
    SetWindowPos(edit, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

std::wstring EditBox::textValue() const {
    std::wstring s(size_t(GetWindowTextLengthW(edit)) + 1, L'\0');
    s.resize(size_t(GetWindowTextW(edit, s.data(), int(s.size()))));
    return s;
}

void EditBox::setText(const std::wstring& s) { SetWindowTextW(edit, s.c_str()); }
