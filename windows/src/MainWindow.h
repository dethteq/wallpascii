#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "Renderer.h"
#include "Ui.h"

constexpr UINT kCmdOpen = 1;
constexpr UINT kCmdSave = 2;

class DropTarget;
struct PhotoCard;

// The one window of the app: settings sidebar on the left, live preview on
// the right, output options and Save in the bottom bar. Mirrors the macOS
// MainWindowController.
class MainWindow {
public:
    static bool registerClass(HINSTANCE instance);
    MainWindow();
    ~MainWindow();
    HWND hwnd() const { return hwnd_; }
    void show(int cmdShow);
    void openPhoto(const std::wstring& path);

private:
    struct Item {
        Widget* widget;
        float width;  // 0 = takes the rest of the row
        float height;  // 0 = row height
    };
    struct Row {
        enum Kind { Section, Labeled, Full } kind = Labeled;
        Label* label = nullptr;
        std::vector<Item> items;
        float height = 28;
        float gap = 8;
        bool visible = true;
    };

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    // UI construction.
    template <typename T, typename... Args>
    T* make(Args&&... args);
    Row* section(const std::wstring& title);
    Row* row(const std::wstring& title, std::vector<Item> items, float height = 28);
    Slider* slider(double min, double max, double value, Label** valueLabel);
    Row* sliderRow(const std::wstring& title, Slider* s, Label* value);
    Segments* segments(std::vector<std::wstring> labels);
    EditBox* editBox(bool multiline, int id, DWORD style = 0);
    void buildUI();
    void createEditFonts();

    // Layout and painting (DIPs).
    float width() const;
    float height() const;
    void layout();
    void layoutSidebar();
    void layoutBar();
    D2D1_RECT_F previewFrame() const;
    void paint();
    void ensureTarget();
    void paintPreview(Canvas& c);
    void paintScrollbar(Canvas& c);
    bool scrollbarThumb(D2D1_RECT_F* out) const;
    void scrollTo(float y);

    // Settings <-> controls.
    void syncControls();
    void updateDerivedViews();
    void updateCaption();
    void readControls();
    wallpascii::Charset selectedCharset() const;
    void updateAutoResolution();
    void saveSettings();
    void loadSettings();
    void scheduleSave();

    // Actions.
    void changed(Widget* sender);
    void colorsChanged();
    void hexEdited(EditBox* box);
    void scheduleRender();
    void openDialog();
    void saveDialog();
    void showError(const std::wstring& title, const std::string& detail);

    // Mouse.
    Widget* widgetAt(float x, float y) const;
    void mouseDown(float x, float y, bool doubleClick);
    void mouseMove(float x, float y);
    void mouseUp(float x, float y);

    HWND hwnd_ = nullptr;
    Host host_;
    float scale_ = 1;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    HFONT editFont_ = nullptr;
    HFONT editSmallFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    DropTarget* dropTarget_ = nullptr;
    HKEY settingsKey_ = nullptr;

    // Settings; the job is copied to the render thread on every change.
    RenderJob job_;
    bool autoResolution_ = true;
    wallpascii::ImageFormat format_ = wallpascii::ImageFormat::Png;
    std::wstring photoName_;
    std::vector<std::wstring> fonts_;
    bool syncing_ = false;  // controls are being set from code

    // Rendering.
    std::unique_ptr<RenderWorker> worker_;
    double lastRenderMs_ = 0;
    std::shared_ptr<const wallpascii::RgbImage> previewImage_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> previewBitmap_;
    int previewBitmapW_ = 0, previewBitmapH_ = 0;
    bool previewStale_ = true;
    wallpascii::RgbImage thumbImage_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> thumbBitmap_;
    bool dropping_ = false;

    // Widgets.
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::deque<Row> rows_;
    float scrollY_ = 0;
    float contentHeight_ = 0;
    Widget* hot_ = nullptr;
    Widget* pressed_ = nullptr;
    bool scrolling_ = false;
    float scrollGrabY_ = 0, scrollGrabStart_ = 0;
    bool trackingLeave_ = false;

    PhotoCard* photoCard_ = nullptr;
    Button* chooseButton_ = nullptr;
    Popup* fitPopup_ = nullptr;
    Slider* brightness_ = nullptr;
    Slider* contrast_ = nullptr;
    Checkbox* invert_ = nullptr;
    Popup* charsetPopup_ = nullptr;
    Row* customRow_ = nullptr;
    EditBox* customChars_ = nullptr;
    Popup* fontPopup_ = nullptr;
    Slider* fontSize_ = nullptr;
    Label* fontSizeValue_ = nullptr;
    Segments* modeSeg_ = nullptr;
    Row* textBoxRow_ = nullptr;
    EditBox* textBox_ = nullptr;
    Row* layoutRow_ = nullptr;
    Segments* layoutSeg_ = nullptr;
    Row* alignRow_ = nullptr;
    Segments* alignSeg_ = nullptr;
    Row* textSizeRow_ = nullptr;
    Slider* textSize_ = nullptr;
    Label* textSizeValue_ = nullptr;
    Segments* colorModeSeg_ = nullptr;
    Row* colorTargetRow_ = nullptr;
    Segments* colorTargetSeg_ = nullptr;
    Row* strengthRow_ = nullptr;
    Slider* strength_ = nullptr;
    Label* strengthValue_ = nullptr;
    Row* saturationRow_ = nullptr;
    Slider* saturation_ = nullptr;
    Label* saturationValue_ = nullptr;
    Row* colorBrightRow_ = nullptr;
    Slider* colorBright_ = nullptr;
    Label* colorBrightValue_ = nullptr;
    ColorSwatch* bgSwatch_ = nullptr;
    ColorSwatch* fgSwatch_ = nullptr;
    EditBox* bgHex_ = nullptr;
    EditBox* fgHex_ = nullptr;
    std::vector<PresetSwatch*> presets_;
    Label* caption_ = nullptr;
    Label* resLabel_ = nullptr;
    Segments* resSeg_ = nullptr;
    EditBox* widthField_ = nullptr;
    Label* timesLabel_ = nullptr;
    EditBox* heightField_ = nullptr;
    Label* formatLabel_ = nullptr;
    Segments* formatSeg_ = nullptr;
    Button* saveButton_ = nullptr;
};
