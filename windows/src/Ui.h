#pragma once

// A small custom-drawn widget set on Direct2D: the dark lo-fi theme of the
// macOS app cannot be had from the stock Win32 controls. Only text entry
// uses native EDIT controls (hosted by EditBox) for IME, clipboard and undo.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "wallpascii/image.h"

// ---- theme -------------------------------------------------------------------
// Lo-fi and dark: washed-out warm blacks, bone-colored text, one dusty coral
// accent taken from the app icon.

namespace theme {
constexpr uint32_t Window = 0x141214;
constexpr uint32_t Sidebar = 0x1A181A;
constexpr uint32_t Card = 0x232022;
constexpr uint32_t CardHot = 0x2B272A;
constexpr uint32_t Line = 0x2D292B;
constexpr uint32_t Text = 0xD6CFC6;
constexpr uint32_t Dim = 0x8A827C;
constexpr uint32_t Faint = 0x5E5753;
constexpr uint32_t Accent = 0xC24A3E;
constexpr uint32_t AccentHot = 0xD0584B;
constexpr uint32_t Preview = 0x0F0E0F;
}  // namespace theme

COLORREF toColorRef(uint32_t rgb);
uint32_t toRgb(wallpascii::Color c);
wallpascii::Color toColor(uint32_t rgb);
std::wstring hexString(wallpascii::Color c);

enum class Font { Ui, UiSmall, UiMedium, Mono11, Mono12, Mono13, MonoSemibold12 };
enum class Align { Left, Center, Right };

// Monospace family for the UI itself: Cascadia Mono when installed.
const std::wstring& uiMonoFamily();

// Drawing helpers over one render target; coordinates are DIPs.
class Canvas {
public:
    explicit Canvas(ID2D1RenderTarget* rt);
    ID2D1RenderTarget* target() const { return rt_; }
    void fill(const D2D1_RECT_F& r, uint32_t rgb, float radius = 0);
    void stroke(const D2D1_RECT_F& r, uint32_t rgb, float radius = 0, float width = 1);
    void line(float x0, float y0, float x1, float y1, uint32_t rgb, float width = 1);
    void circle(float cx, float cy, float radius, uint32_t rgb);
    void text(const std::wstring& s, const D2D1_RECT_F& r, Font font, uint32_t rgb, Align align = Align::Left);
    float textWidth(const std::wstring& s, Font font);

private:
    ID2D1SolidColorBrush* brush(uint32_t rgb);
    IDWriteTextFormat* format(Font font);

    ID2D1RenderTarget* rt_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
};

// What a widget needs from its window.
struct Host {
    HWND hwnd = nullptr;
    float scale = 1;  // physical pixels per DIP
    POINT toScreen(float x, float y) const;
};

struct Widget {
    D2D1_RECT_F rc{};  // window DIPs, scroll applied
    bool visible = true;
    bool enabled = true;
    std::function<void()> onChange;

    virtual ~Widget() = default;
    virtual void paint(Canvas& c, bool hot) = 0;
    virtual bool clickable() const { return true; }
    // Mouse down; the widget keeps the mouse until release.
    virtual void press(Host&, float, float) {}
    virtual void drag(float, float) {}
    virtual void release(float, float) {}
    virtual void doubleClick() {}
    bool contains(float x, float y) const {
        return visible && x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
    }

protected:
    void changed() {
        if (onChange) onChange();
    }
};

struct Label : Widget {
    std::wstring text;
    Font font = Font::Ui;
    uint32_t color = theme::Dim;
    Align align = Align::Left;
    Label(std::wstring t, Font f, uint32_t c, Align a = Align::Left)
        : text(std::move(t)), font(f), color(c), align(a) {}
    bool clickable() const override { return false; }
    void paint(Canvas& c, bool) override { c.text(text, rc, font, color, align); }
};

struct Slider : Widget {
    double min, max, value, initial;
    Slider(double lo, double hi, double v) : min(lo), max(hi), value(v), initial(v) {}
    void paint(Canvas& c, bool hot) override;
    void press(Host&, float x, float y) override { drag(x, y); }
    void drag(float x, float) override;
    void doubleClick() override;  // back to the default value
};

struct Segments : Widget {
    std::vector<std::wstring> labels;
    int selected = 0;
    int hotIndex = -1;
    explicit Segments(std::vector<std::wstring> l) : labels(std::move(l)) {}
    void paint(Canvas& c, bool hot) override;
    void press(Host&, float x, float y) override;
    int indexAt(float x) const;
};

// A drop-down list: a button that opens a menu of its items.
struct Popup : Widget {
    std::vector<std::wstring> items;
    int selected = 0;
    explicit Popup(std::vector<std::wstring> i) : items(std::move(i)) {}
    void paint(Canvas& c, bool hot) override;
    void press(Host& host, float x, float y) override;
};

struct Checkbox : Widget {
    std::wstring title;
    bool checked = false;
    explicit Checkbox(std::wstring t) : title(std::move(t)) {}
    void paint(Canvas& c, bool hot) override;
    void press(Host&, float, float) override {
        checked = !checked;
        changed();
    }
};

struct Button : Widget {
    std::wstring title;
    bool accent = false;
    Button(std::wstring t, bool a) : title(std::move(t)), accent(a) {}
    void paint(Canvas& c, bool hot) override;
    void release(float x, float y) override {
        if (contains(x, y)) changed();
    }
};

// A color swatch that opens the system color picker.
struct ColorSwatch : Widget {
    wallpascii::Color color;
    void paint(Canvas& c, bool hot) override;
    void press(Host& host, float, float) override;
};

// A color preset: background swatch with an "A" in the glyph color.
struct PresetSwatch : Widget {
    uint32_t background, glyph;
    PresetSwatch(uint32_t bg, uint32_t fg) : background(bg), glyph(fg) {}
    void paint(Canvas& c, bool hot) override;
    void release(float x, float y) override {
        if (contains(x, y)) changed();
    }
};

// Hosts a native EDIT control and draws its frame. `onChange` fires on every edit.
struct EditBox : Widget {
    HWND edit = nullptr;
    bool multiline = false;
    std::wstring placeholder;
    EditBox(HWND parent, bool multiline, int id, DWORD extraStyle = 0);
    bool clickable() const override { return false; }
    void paint(Canvas& c, bool hot) override;
    // Moves the native control into the frame (window DIPs → pixels).
    void place(float scale, const D2D1_RECT_F& clip);
    std::wstring textValue() const;
    void setText(const std::wstring& s);
};
