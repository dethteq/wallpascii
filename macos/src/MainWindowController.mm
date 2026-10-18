#import "MainWindowController.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "Renderer.h"
#include "wallpascii/wallpascii.h"

using namespace wallpascii;

// ---- theme ----------------------------------------------------------------
// Lo-fi and dark: washed-out warm blacks, bone-colored text, one dusty coral
// accent taken from the app icon.

static NSColor* Hex(uint32_t rgb) {
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                               green:((rgb >> 8) & 0xFF) / 255.0
                                blue:(rgb & 0xFF) / 255.0
                               alpha:1];
}
static NSColor* ThemeWindow() { return Hex(0x141214); }
static NSColor* ThemeSidebar() { return Hex(0x1A181A); }
static NSColor* ThemeCard() { return Hex(0x232022); }
static NSColor* ThemeLine() { return Hex(0x2D292B); }
static NSColor* ThemeText() { return Hex(0xD6CFC6); }
static NSColor* ThemeDim() { return Hex(0x8A827C); }
static NSColor* ThemeFaint() { return Hex(0x5E5753); }
static NSColor* ThemeAccent() { return Hex(0xC24A3E); }
static NSColor* ThemePreview() { return Hex(0x0F0E0F); }

static NSFont* Mono(CGFloat size) { return [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular]; }

struct ColorPreset {
    NSString* name;
    uint32_t background;
    uint32_t glyph;
};
static const ColorPreset kPresets[] = {
    {@"Ash", 0x0B0B0C, 0xE8E8E8},   {@"Bone", 0xE6E0D3, 0x1A1817},  {@"Ember", 0x171517, 0xE07B4A},
    {@"Blood", 0x0D0909, 0xA8322B}, {@"Tape", 0x1C1A1C, 0x9C978F},  {@"Ghost", 0x101214, 0x8FA3A8},
};

static Color ToColor(NSColor* c) {
    NSColor* s = [c colorUsingColorSpace:NSColorSpace.sRGBColorSpace] ?: NSColor.blackColor;
    auto ch = [](CGFloat v) { return uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
    return {ch(s.redComponent), ch(s.greenComponent), ch(s.blueComponent)};
}
static NSColor* ToNSColor(Color c) { return Hex((uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b); }
static NSString* HexString(Color c) { return [NSString stringWithFormat:@"#%02X%02X%02X", c.r, c.g, c.b]; }

// ---- views ----------------------------------------------------------------

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

// Shows the rendered wallpaper aspect-fit, in the proportions of the output,
// and accepts photos dropped on it.
@interface PreviewView : NSView
@property(nonatomic, strong) NSImage* image;
@property(nonatomic) NSSize outputSize;
@property(nonatomic) BOOL showsPlaceholder;
@property(nonatomic, copy) void (^onDropPath)(NSString* path);
@end

@implementation PreviewView {
    BOOL _dragging;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        _outputSize = NSMakeSize(16, 9);
    }
    return self;
}

- (void)setImage:(NSImage*)image { _image = image; self.needsDisplay = YES; }
- (void)setOutputSize:(NSSize)s { _outputSize = s; self.needsDisplay = YES; }
- (void)setShowsPlaceholder:(BOOL)v { _showsPlaceholder = v; self.needsDisplay = YES; }

- (NSRect)frameRect {
    const NSRect b = NSInsetRect(self.bounds, 1, 1);
    const CGFloat ow = std::max<CGFloat>(1, _outputSize.width), oh = std::max<CGFloat>(1, _outputSize.height);
    const CGFloat scale = std::min(b.size.width / ow, b.size.height / oh);
    const CGFloat w = std::floor(ow * scale), h = std::floor(oh * scale);
    return NSMakeRect(std::floor(NSMidX(b) - w / 2), std::floor(NSMidY(b) - h / 2), w, h);
}

- (void)drawRect:(NSRect)dirty {
    const NSRect r = [self frameRect];
    [ThemePreview() setFill];
    NSRectFill(r);
    if (_image) {
        [NSGraphicsContext currentContext].imageInterpolation = NSImageInterpolationHigh;
        [_image drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
    }
    if (_showsPlaceholder) {
        NSDictionary* attrs = @{NSFontAttributeName : Mono(13), NSForegroundColorAttributeName : ThemeDim()};
        NSString* s = @"drop a photo here  ·  ⌘O";
        const NSSize sz = [s sizeWithAttributes:attrs];
        [s drawAtPoint:NSMakePoint(NSMidX(r) - sz.width / 2, NSMidY(r) - sz.height / 2) withAttributes:attrs];
    }
    NSBezierPath* border = [NSBezierPath bezierPathWithRect:NSInsetRect(r, -0.5, -0.5)];
    [(_dragging ? ThemeAccent() : ThemeLine()) setStroke];
    border.lineWidth = _dragging ? 2 : 1;
    [border stroke];
}

- (NSString*)droppedPath:(id<NSDraggingInfo>)info {
    NSArray* urls = [info.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    return [urls.firstObject path];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    if (![self droppedPath:info]) return NSDragOperationNone;
    _dragging = YES;
    self.needsDisplay = YES;
    return NSDragOperationCopy;
}

- (void)draggingExited:(id<NSDraggingInfo>)info {
    _dragging = NO;
    self.needsDisplay = YES;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    _dragging = NO;
    self.needsDisplay = YES;
    NSString* path = [self droppedPath:info];
    if (path && _onDropPath) _onDropPath(path);
    return path != nil;
}

@end

// ---- controller -------------------------------------------------------------

namespace {
enum TextModeIndex { kTextOff = 0, kTextSide = 1, kTextImage = 2 };
const CGFloat kSidebarWidth = 340;
const CGFloat kLabelWidth = 92;
const CGFloat kPad = 20;
}  // namespace

@interface MainWindowController () <NSTextViewDelegate, NSTextFieldDelegate>
@end

@implementation MainWindowController {
    // Settings; the job is copied to the render queue on every change.
    RenderJob _job;
    BOOL _autoResolution;
    ImageFormat _format;
    NSString* _photoName;

    // Rendering: one serial queue owns the renderer; at most one render runs
    // and one more is queued, so slider drags never pile up.
    dispatch_queue_t _queue;
    std::shared_ptr<Renderer> _renderer;
    BOOL _rendering;
    BOOL _pending;
    double _lastRenderMs;

    // Views.
    NSStackView* _stack;
    PreviewView* _preview;
    NSTextField* _caption;
    NSImageView* _thumb;
    NSTextField* _fileName;
    NSTextField* _fileInfo;
    NSPopUpButton* _fitPopup;
    NSSlider* _brightness;
    NSSlider* _contrast;
    NSButton* _invert;
    NSPopUpButton* _charsetPopup;
    NSView* _customRow;
    NSTextField* _customChars;
    NSPopUpButton* _fontPopup;
    NSSlider* _fontSize;
    NSTextField* _fontSizeValue;
    NSSegmentedControl* _modeSeg;
    NSView* _textBox;
    NSTextView* _textView;
    NSView* _layoutRow;
    NSSegmentedControl* _layoutSeg;
    NSView* _alignRow;
    NSSegmentedControl* _alignSeg;
    NSView* _textSizeRow;
    NSSlider* _textSize;
    NSTextField* _textSizeValue;
    NSColorWell* _bgWell;
    NSColorWell* _fgWell;
    NSTextField* _bgHex;
    NSTextField* _fgHex;
    NSSegmentedControl* _colorModeSeg;
    NSView* _colorTargetRow;
    NSSegmentedControl* _colorTargetSeg;
    NSView* _strengthRow;
    NSSlider* _strength;
    NSTextField* _strengthValue;
    NSView* _saturationRow;
    NSSlider* _saturation;
    NSTextField* _saturationValue;
    NSView* _colorBrightRow;
    NSSlider* _colorBright;
    NSTextField* _colorBrightValue;
    NSSegmentedControl* _resSeg;
    NSTextField* _widthField;
    NSTextField* _heightField;
    NSSegmentedControl* _formatSeg;
}

- (instancetype)init {
    NSWindow* w = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1240, 820)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!(self = [super initWithWindow:w])) return nil;

    w.title = @"wallpascii 💀";
    w.titlebarAppearsTransparent = YES;
    w.backgroundColor = ThemeWindow();
    w.minSize = NSMakeSize(1000, 700);
    w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

    _queue = dispatch_queue_create("wallpascii.render", DISPATCH_QUEUE_SERIAL);
    _renderer = std::make_shared<Renderer>();
    _autoResolution = YES;
    _format = ImageFormat::Png;
    _job.colors.background = {0x0B, 0x0B, 0x0C};
    _job.colors.glyph = {0xE8, 0xE8, 0xE8};
    _job.fontSize = 20;
    _job.textSize = 72;
    _job.settings.align = TextAlign::Left;

    [self buildUI];
    [self updateAutoResolution];
    [self syncControls];
    [self loadDefaults];
    [w center];
    w.frameAutosaveName = @"MainWindow";

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(screenChanged:)
                                                 name:NSWindowDidChangeScreenNotification
                                               object:w];
    [self scheduleRender];
    return self;
}

// ---- UI construction ----

- (NSTextField*)label:(NSString*)text {
    NSTextField* l = [NSTextField labelWithString:text];
    l.textColor = ThemeDim();
    l.font = [NSFont systemFontOfSize:13];
    return l;
}

- (NSTextField*)valueLabel {
    NSTextField* l = [NSTextField labelWithString:@""];
    l.textColor = ThemeFaint();
    l.font = Mono(11);
    l.alignment = NSTextAlignmentRight;
    [l.widthAnchor constraintEqualToConstant:34].active = YES;
    return l;
}

- (void)addSection:(NSString*)title {
    NSTextField* l = [NSTextField labelWithString:title.uppercaseString];
    l.font = Mono(11);
    l.textColor = ThemeFaint();
    NSView* last = _stack.arrangedSubviews.lastObject;
    [self add:l];
    if (last) [_stack setCustomSpacing:26 afterView:last];
}

- (void)add:(NSView*)v {
    [_stack addArrangedSubview:v];
    [v.leadingAnchor constraintEqualToAnchor:_stack.leadingAnchor constant:kPad].active = YES;
    [v.trailingAnchor constraintEqualToAnchor:_stack.trailingAnchor constant:-kPad].active = YES;
}

- (NSView*)row:(NSString*)title control:(NSView*)control {
    NSTextField* l = [self label:title];
    [l.widthAnchor constraintEqualToConstant:kLabelWidth].active = YES;
    NSStackView* h = [NSStackView stackViewWithViews:@[ l, control ]];
    h.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    h.alignment = NSLayoutAttributeCenterY;
    h.distribution = NSStackViewDistributionFill;
    h.spacing = 8;
    [control setContentHuggingPriority:NSLayoutPriorityDefaultLow
                        forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self add:h];
    return h;
}

- (NSSlider*)slider:(double)min max:(double)max value:(NSTextField* __strong*)valueLabel {
    NSSlider* s = [NSSlider sliderWithValue:0 minValue:min maxValue:max target:self action:@selector(changed:)];
    s.continuous = YES;
    s.trackFillColor = ThemeAccent();
    if (valueLabel) *valueLabel = [self valueLabel];
    return s;
}

- (NSView*)sliderRow:(NSString*)title slider:(NSSlider*)s value:(NSTextField*)value {
    NSView* control = s;
    if (value) {
        NSStackView* h = [NSStackView stackViewWithViews:@[ s, value ]];
        h.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        h.distribution = NSStackViewDistributionFill;
        h.spacing = 6;
        [s setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
        control = h;
    }
    return [self row:title control:control];
}

- (NSSegmentedControl*)segments:(NSArray<NSString*>*)labels {
    NSSegmentedControl* s = [NSSegmentedControl segmentedControlWithLabels:labels
                                                              trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                    target:self
                                                                    action:@selector(changed:)];
    s.segmentDistribution = NSSegmentDistributionFillEqually;
    s.selectedSegmentBezelColor = ThemeAccent();
    return s;
}

- (NSPopUpButton*)popup:(NSArray<NSString*>*)items {
    NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [p addItemsWithTitles:items];
    p.bezelColor = ThemeCard();
    p.contentTintColor = ThemeDim();
    p.target = self;
    p.action = @selector(changed:);
    return p;
}

- (NSView*)photoCard {
    NSView* card = [[NSView alloc] init];
    card.wantsLayer = YES;
    card.layer.backgroundColor = ThemeCard().CGColor;
    card.layer.cornerRadius = 8;

    _thumb = [[NSImageView alloc] init];
    _thumb.imageScaling = NSImageScaleProportionallyUpOrDown;
    _thumb.wantsLayer = YES;
    _thumb.layer.backgroundColor = ThemePreview().CGColor;
    _thumb.layer.cornerRadius = 4;
    _thumb.layer.masksToBounds = YES;
    [_thumb.widthAnchor constraintEqualToConstant:64].active = YES;
    [_thumb.heightAnchor constraintEqualToConstant:44].active = YES;

    _fileName = [NSTextField labelWithString:@"no photo"];
    _fileName.textColor = ThemeText();
    _fileName.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    _fileName.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [_fileName setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                        forOrientation:NSLayoutConstraintOrientationHorizontal];
    _fileInfo = [NSTextField labelWithString:@"jpeg · png · heic"];
    _fileInfo.textColor = ThemeFaint();
    _fileInfo.font = Mono(11);
    NSStackView* names = [NSStackView stackViewWithViews:@[ _fileName, _fileInfo ]];
    names.orientation = NSUserInterfaceLayoutOrientationVertical;
    names.alignment = NSLayoutAttributeLeading;
    names.spacing = 2;

    NSButton* choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(openDocument:)];
    choose.controlSize = NSControlSizeRegular;
    [choose setContentHuggingPriority:NSLayoutPriorityRequired
                       forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* h = [NSStackView stackViewWithViews:@[ _thumb, names, choose ]];
    h.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    h.alignment = NSLayoutAttributeCenterY;
    h.spacing = 12;
    h.translatesAutoresizingMaskIntoConstraints = NO;
    [card addSubview:h];
    [NSLayoutConstraint activateConstraints:@[
        [h.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:10],
        [h.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-10],
        [h.topAnchor constraintEqualToAnchor:card.topAnchor constant:10],
        [h.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-10],
    ]];
    return card;
}

- (NSView*)colorRow:(NSString*)title well:(NSColorWell* __strong*)well hex:(NSTextField* __strong*)hex {
    NSColorWell* w = [[NSColorWell alloc] init];
    if (@available(macOS 13.0, *)) w.colorWellStyle = NSColorWellStyleMinimal;
    w.target = self;
    w.action = @selector(colorChanged:);
    [w.widthAnchor constraintEqualToConstant:56].active = YES;
    [w.heightAnchor constraintEqualToConstant:24].active = YES;
    NSTextField* h = [NSTextField labelWithString:@""];
    h.font = Mono(12);
    h.textColor = ThemeDim();
    NSStackView* s = [NSStackView stackViewWithViews:@[ w, h ]];
    s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    s.spacing = 10;
    *well = w;
    *hex = h;
    return [self row:title control:s];
}

- (NSView*)presetRow {
    NSMutableArray* buttons = [NSMutableArray array];
    NSInteger i = 0;
    for (const ColorPreset& p : kPresets) {
        NSButton* b = [NSButton buttonWithTitle:@"" target:self action:@selector(presetClicked:)];
        b.bordered = NO;
        b.tag = i++;
        b.toolTip = p.name;
        b.wantsLayer = YES;
        b.layer.backgroundColor = Hex(p.background).CGColor;
        b.layer.borderColor = ThemeLine().CGColor;
        b.layer.borderWidth = 1;
        b.layer.cornerRadius = 5;
        NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
        ps.alignment = NSTextAlignmentCenter;
        b.attributedTitle = [[NSAttributedString alloc]
            initWithString:@"A"
                attributes:@{
                    NSFontAttributeName : [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightSemibold],
                    NSForegroundColorAttributeName : Hex(p.glyph),
                    NSParagraphStyleAttributeName : ps,
                }];
        [b.widthAnchor constraintEqualToConstant:28].active = YES;
        [b.heightAnchor constraintEqualToConstant:26].active = YES;
        [buttons addObject:b];
    }
    NSStackView* s = [NSStackView stackViewWithViews:buttons];
    s.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    s.spacing = 4;
    NSView* wrap = [[NSView alloc] init];  // keeps the swatches left-aligned
    s.translatesAutoresizingMaskIntoConstraints = NO;
    [wrap addSubview:s];
    [NSLayoutConstraint activateConstraints:@[
        [s.leadingAnchor constraintEqualToAnchor:wrap.leadingAnchor],
        [s.topAnchor constraintEqualToAnchor:wrap.topAnchor],
        [s.bottomAnchor constraintEqualToAnchor:wrap.bottomAnchor],
        [s.trailingAnchor constraintLessThanOrEqualToAnchor:wrap.trailingAnchor],
    ]];
    return [self row:@"Presets" control:wrap];
}

- (NSView*)textBox {
    NSScrollView* scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSNoBorder;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = ThemePreview();
    scroll.wantsLayer = YES;
    scroll.layer.cornerRadius = 6;
    scroll.layer.borderWidth = 1;
    scroll.layer.borderColor = ThemeLine().CGColor;
    [scroll.heightAnchor constraintEqualToConstant:92].active = YES;

    _textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 280, 92)];
    _textView.font = Mono(13);
    _textView.textColor = ThemeText();
    _textView.backgroundColor = ThemePreview();
    _textView.insertionPointColor = ThemeAccent();
    _textView.textContainerInset = NSMakeSize(8, 8);
    _textView.richText = NO;
    _textView.automaticQuoteSubstitutionEnabled = NO;
    _textView.automaticDashSubstitutionEnabled = NO;
    _textView.automaticTextReplacementEnabled = NO;
    _textView.allowsUndo = YES;
    _textView.verticallyResizable = YES;
    _textView.autoresizingMask = NSViewWidthSizable;
    _textView.delegate = self;
    scroll.documentView = _textView;
    return scroll;
}

- (void)buildUI {
    NSView* content = self.window.contentView;

    // Sidebar: a scrolling column of sections.
    NSScrollView* sideScroll = [[NSScrollView alloc] init];
    sideScroll.hasVerticalScroller = YES;
    sideScroll.drawsBackground = YES;
    sideScroll.backgroundColor = ThemeSidebar();
    sideScroll.translatesAutoresizingMaskIntoConstraints = NO;
    FlippedView* doc = [[FlippedView alloc] init];
    doc.translatesAutoresizingMaskIntoConstraints = NO;
    sideScroll.documentView = doc;

    _stack = [[NSStackView alloc] init];
    _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    _stack.alignment = NSLayoutAttributeLeading;
    _stack.spacing = 12;
    _stack.edgeInsets = NSEdgeInsetsMake(44, 0, 24, 0);  // clear the transparent titlebar
    _stack.translatesAutoresizingMaskIntoConstraints = NO;
    [doc addSubview:_stack];
    [NSLayoutConstraint activateConstraints:@[
        [_stack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
        [_stack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor],
        [_stack.topAnchor constraintEqualToAnchor:doc.topAnchor],
        [_stack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
        [doc.leadingAnchor constraintEqualToAnchor:sideScroll.contentView.leadingAnchor],
        [doc.trailingAnchor constraintEqualToAnchor:sideScroll.contentView.trailingAnchor],
        [doc.topAnchor constraintEqualToAnchor:sideScroll.contentView.topAnchor],
    ]];

    // IMAGE
    [self addSection:@"Image"];
    [self add:[self photoCard]];
    _fitPopup = [self popup:@[ @"Fill Screen", @"Fit to Screen", @"Stretch to Fill", @"Center" ]];
    [self row:@"Fit" control:_fitPopup];
    _brightness = [self slider:-1 max:1 value:nil];
    [self sliderRow:@"Brightness" slider:_brightness value:nil];
    _contrast = [self slider:-1 max:1 value:nil];
    [self sliderRow:@"Contrast" slider:_contrast value:nil];
    _invert = [NSButton checkboxWithTitle:@"Invert" target:self action:@selector(changed:)];
    _invert.contentTintColor = ThemeAccent();
    _invert.attributedTitle = [[NSAttributedString alloc]
        initWithString:@"Invert"
            attributes:@{NSForegroundColorAttributeName : ThemeText(), NSFontAttributeName : [NSFont systemFontOfSize:13]}];
    [self row:@"" control:_invert];

    // ASCII
    [self addSection:@"ASCII"];
    _charsetPopup = [self popup:@[ @"Simple   .:-=+*#%@", @"Detailed   .'`^\",:;Il!i…", @"Blocks   ░▒▓█", @"Custom" ]];
    [self row:@"Characters" control:_charsetPopup];
    _customChars = [NSTextField textFieldWithString:@" .oO0@"];
    _customChars.font = Mono(13);
    _customChars.placeholderString = @"sparse → dense";
    _customChars.delegate = self;
    _customRow = [self row:@"" control:_customChars];
    _fontPopup = [self popup:@[ @"SF Mono", @"Menlo", @"Monaco", @"Courier New", @"Andale Mono" ]];
    [self row:@"Font" control:_fontPopup];
    _fontSize = [self slider:6 max:80 value:&_fontSizeValue];
    [self sliderRow:@"Font size" slider:_fontSize value:_fontSizeValue];

    // TEXT
    [self addSection:@"Text"];
    _modeSeg = [self segments:@[ @"Off", @"Side by side", @"Text as image" ]];
    [self add:_modeSeg];
    _textBox = [self textBox];
    [self add:_textBox];
    _layoutSeg = [self segments:@[ @"Left", @"Right", @"Top", @"Bottom" ]];
    _layoutRow = [self row:@"Image" control:_layoutSeg];
    _alignSeg = [self segments:@[ @"Left", @"Center", @"Right" ]];
    _alignRow = [self row:@"Alignment" control:_alignSeg];
    _textSize = [self slider:16 max:240 value:&_textSizeValue];
    _textSizeRow = [self sliderRow:@"Text size" slider:_textSize value:_textSizeValue];

    // COLORS
    [self addSection:@"Colors"];
    _colorModeSeg = [self segments:@[ @"Mono", @"Original" ]];
    _colorModeSeg.toolTip = @"Original takes the colors of the photo";
    [self row:@"Mode" control:_colorModeSeg];
    _colorTargetSeg = [self segments:@[ @"Glyphs", @"Background", @"Both" ]];
    _colorTargetRow = [self row:@"Paint" control:_colorTargetSeg];
    _strength = [self slider:0 max:1 value:&_strengthValue];
    _strength.toolTip = @"Mix between the colors below and the photo colors";
    _strengthRow = [self sliderRow:@"Strength" slider:_strength value:_strengthValue];
    _saturation = [self slider:0 max:2 value:&_saturationValue];
    _saturationRow = [self sliderRow:@"Saturation" slider:_saturation value:_saturationValue];
    _colorBright = [self slider:-1 max:1 value:&_colorBrightValue];
    _colorBright.toolTip = @"Darken or lift the photo colors";
    _colorBrightRow = [self sliderRow:@"Lift" slider:_colorBright value:_colorBrightValue];
    [self colorRow:@"Background" well:&_bgWell hex:&_bgHex];
    [self colorRow:@"Glyphs" well:&_fgWell hex:&_fgHex];
    [self presetRow];

    // Preview and its caption.
    _preview = [[PreviewView alloc] init];
    _preview.translatesAutoresizingMaskIntoConstraints = NO;
    __weak MainWindowController* weakSelf = self;
    _preview.onDropPath = ^(NSString* path) { [weakSelf openPhotoAtPath:path]; };
    _caption = [NSTextField labelWithString:@""];
    _caption.font = Mono(11);
    _caption.textColor = ThemeFaint();
    _caption.translatesAutoresizingMaskIntoConstraints = NO;

    // Bottom bar: output options and Save.
    NSView* bar = [[NSView alloc] init];
    bar.wantsLayer = YES;
    bar.layer.backgroundColor = ThemeSidebar().CGColor;
    bar.translatesAutoresizingMaskIntoConstraints = NO;
    _resSeg = [self segments:@[ @"Auto", @"Custom" ]];
    _widthField = [self numberField];
    _heightField = [self numberField];
    NSTextField* times = [NSTextField labelWithString:@"×"];
    times.textColor = ThemeFaint();
    _formatSeg = [self segments:@[ @"PNG", @"JPEG" ]];
    NSButton* save = [NSButton buttonWithTitle:@"Save…" target:self action:@selector(saveDocument:)];
    save.bezelColor = ThemeAccent();
    save.controlSize = NSControlSizeLarge;
    NSView* spacer = [[NSView alloc] init];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* barStack = [NSStackView stackViewWithViews:@[
        [self label:@"Resolution"], _resSeg, _widthField, times, _heightField, spacer, [self label:@"Format"],
        _formatSeg, save
    ]];
    barStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    barStack.alignment = NSLayoutAttributeCenterY;
    barStack.spacing = 10;
    [barStack setCustomSpacing:18 afterView:_formatSeg];
    barStack.translatesAutoresizingMaskIntoConstraints = NO;
    [bar addSubview:barStack];

    NSBox* divider = [[NSBox alloc] init];
    divider.boxType = NSBoxCustom;
    divider.fillColor = ThemeLine();
    divider.borderWidth = 0;
    divider.translatesAutoresizingMaskIntoConstraints = NO;
    NSBox* barLine = [[NSBox alloc] init];
    barLine.boxType = NSBoxCustom;
    barLine.fillColor = ThemeLine();
    barLine.borderWidth = 0;
    barLine.translatesAutoresizingMaskIntoConstraints = NO;

    for (NSView* v in @[ sideScroll, divider, _preview, _caption, bar, barLine ]) [content addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [sideScroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [sideScroll.topAnchor constraintEqualToAnchor:content.topAnchor],
        [sideScroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [sideScroll.widthAnchor constraintEqualToConstant:kSidebarWidth],
        [divider.leadingAnchor constraintEqualToAnchor:sideScroll.trailingAnchor],
        [divider.topAnchor constraintEqualToAnchor:content.topAnchor],
        [divider.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [divider.widthAnchor constraintEqualToConstant:1],

        [bar.leadingAnchor constraintEqualToAnchor:divider.trailingAnchor],
        [bar.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [bar.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [bar.heightAnchor constraintEqualToConstant:60],
        [barLine.leadingAnchor constraintEqualToAnchor:bar.leadingAnchor],
        [barLine.trailingAnchor constraintEqualToAnchor:bar.trailingAnchor],
        [barLine.topAnchor constraintEqualToAnchor:bar.topAnchor],
        [barLine.heightAnchor constraintEqualToConstant:1],
        [barStack.leadingAnchor constraintEqualToAnchor:bar.leadingAnchor constant:24],
        [barStack.trailingAnchor constraintEqualToAnchor:bar.trailingAnchor constant:-20],
        [barStack.centerYAnchor constraintEqualToAnchor:bar.centerYAnchor],

        [_preview.leadingAnchor constraintEqualToAnchor:divider.trailingAnchor constant:40],
        [_preview.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-40],
        [_preview.topAnchor constraintEqualToAnchor:content.topAnchor constant:56],
        [_caption.topAnchor constraintEqualToAnchor:_preview.bottomAnchor constant:14],
        [_caption.centerXAnchor constraintEqualToAnchor:_preview.centerXAnchor],
        [_caption.bottomAnchor constraintEqualToAnchor:bar.topAnchor constant:-24],
    ]];
}

- (NSTextField*)numberField {
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.font = Mono(12);
    f.alignment = NSTextAlignmentCenter;
    NSNumberFormatter* fmt = [[NSNumberFormatter alloc] init];
    fmt.numberStyle = NSNumberFormatterNoStyle;
    fmt.minimum = @16;
    fmt.maximum = @16384;
    fmt.allowsFloats = NO;
    f.formatter = fmt;
    f.delegate = self;
    f.target = self;
    f.action = @selector(changed:);
    [f.widthAnchor constraintEqualToConstant:64].active = YES;
    return f;
}

// ---- settings <-> controls ----

// Pushes the current settings into every control.
- (void)syncControls {
    const WallpaperSettings& s = _job.settings;
    [_fitPopup selectItemAtIndex:int(s.grid.fit)];
    _brightness.doubleValue = s.grid.adjust.brightness;
    _contrast.doubleValue = s.grid.adjust.contrast;
    _invert.state = s.grid.adjust.invert ? NSControlStateValueOn : NSControlStateValueOff;
    _fontSize.doubleValue = _job.fontSize;
    _textSize.doubleValue = _job.textSize;
    _bgWell.color = ToNSColor(_job.colors.background);
    _fgWell.color = ToNSColor(_job.colors.glyph);
    _layoutSeg.selectedSegment = int(s.layout);
    _colorModeSeg.selectedSegment = int(s.grid.color.mode);
    _colorTargetSeg.selectedSegment = int(s.grid.color.target);
    _strength.doubleValue = s.grid.color.strength;
    _saturation.doubleValue = s.grid.color.saturation;
    _colorBright.doubleValue = s.grid.color.brightness;
    _alignSeg.selectedSegment = int(s.align);
    if (_modeSeg.selectedSegment < 0) _modeSeg.selectedSegment = kTextOff;
    if (_charsetPopup.indexOfSelectedItem < 0) [_charsetPopup selectItemAtIndex:0];
    _resSeg.selectedSegment = _autoResolution ? 0 : 1;
    _formatSeg.selectedSegment = _format == ImageFormat::Png ? 0 : 1;
    _widthField.integerValue = s.width;
    _heightField.integerValue = s.height;
    [self updateDerivedViews];
}

// Value labels, hex labels, which rows are visible.
- (void)updateDerivedViews {
    _fontSizeValue.stringValue = [NSString stringWithFormat:@"%d", int(std::lround(_job.fontSize))];
    _textSizeValue.stringValue = [NSString stringWithFormat:@"%d", int(std::lround(_job.textSize))];
    _bgHex.stringValue = HexString(_job.colors.background);
    _fgHex.stringValue = HexString(_job.colors.glyph);
    const NSInteger mode = _modeSeg.selectedSegment;
    _textBox.hidden = mode == kTextOff;
    _layoutRow.hidden = _alignRow.hidden = _textSizeRow.hidden = mode != kTextSide;
    _customRow.hidden = _charsetPopup.indexOfSelectedItem != 3;
    const ColorSettings& color = _job.settings.grid.color;
    _colorTargetRow.hidden = _strengthRow.hidden = _saturationRow.hidden = _colorBrightRow.hidden =
        color.mode != ColorMode::Original;
    _strengthValue.stringValue = [NSString stringWithFormat:@"%d%%", int(std::lround(color.strength * 100))];
    _saturationValue.stringValue = [NSString stringWithFormat:@"%d%%", int(std::lround(color.saturation * 100))];
    _colorBrightValue.stringValue = [NSString stringWithFormat:@"%+d", int(std::lround(color.brightness * 100))];
    _widthField.enabled = _heightField.enabled = !_autoResolution;
    _preview.outputSize = NSMakeSize(_job.settings.width, _job.settings.height);
    _preview.showsPlaceholder = _job.photo == nullptr;
    [self updateCaption];
}

- (void)updateCaption {
    const WallpaperSettings& s = _job.settings;
    NSString* mode = @"ASCII";
    if (_modeSeg.selectedSegment == kTextSide) {
        NSArray* where = @[ @"image left", @"image right", @"image top", @"image bottom" ];
        mode = [NSString stringWithFormat:@"Side by side, %@", where[int(s.layout)]];
    } else if (_modeSeg.selectedSegment == kTextImage) {
        mode = @"Text as image";
    }
    if (s.grid.color.mode == ColorMode::Original) mode = [mode stringByAppendingString:@", original colors"];
    NSString* ms = _lastRenderMs > 0 ? [NSString stringWithFormat:@" · %.0f ms", _lastRenderMs] : @"";
    _caption.stringValue =
        [NSString stringWithFormat:@"live preview · %d × %d · %@%@", s.width, s.height, mode, ms];
}

- (Charset)selectedCharset {
    switch (_charsetPopup.indexOfSelectedItem) {
    case 1: return charsetPreset(CharsetPreset::Detailed);
    case 2: return charsetPreset(CharsetPreset::Blocks);
    case 3: {
        Charset c = charsetFromUtf8(_customChars.stringValue.UTF8String ?: "");
        return c.chars.empty() ? charsetPreset(CharsetPreset::Simple) : c;
    }
    default: return charsetPreset(CharsetPreset::Simple);
    }
}

// Reads every control back into the settings.
- (void)readControls {
    WallpaperSettings& s = _job.settings;
    s.grid.fit = FitMode(std::max<NSInteger>(0, _fitPopup.indexOfSelectedItem));
    s.grid.adjust.brightness = float(_brightness.doubleValue);
    s.grid.adjust.contrast = float(_contrast.doubleValue);
    s.grid.adjust.invert = _invert.state == NSControlStateValueOn;
    s.grid.charset = [self selectedCharset];
    _job.font = _fontPopup.titleOfSelectedItem.UTF8String ?: "SF Mono";
    _job.fontSize = std::round(_fontSize.doubleValue);
    _job.textSize = std::round(_textSize.doubleValue);

    const NSInteger mode = _modeSeg.selectedSegment;
    s.grid.mode = mode == kTextImage ? TextMode::TextAsImage : TextMode::Ascii;
    s.sideBySide = mode == kTextSide;
    s.grid.text = _textView.string.UTF8String ?: "";
    s.layout = SideLayout(std::max<NSInteger>(0, _layoutSeg.selectedSegment));
    s.align = TextAlign(std::max<NSInteger>(0, _alignSeg.selectedSegment));
    s.grid.color.mode = _colorModeSeg.selectedSegment == 1 ? ColorMode::Original : ColorMode::Mono;
    s.grid.color.target = ColorTarget(std::clamp<NSInteger>(_colorTargetSeg.selectedSegment, 0, 2));
    s.grid.color.strength = float(_strength.doubleValue);
    s.grid.color.saturation = float(_saturation.doubleValue);
    s.grid.color.brightness = float(_colorBright.doubleValue);

    _autoResolution = _resSeg.selectedSegment == 0;
    if (_autoResolution) {
        [self updateAutoResolution];
    } else {
        s.width = int(std::clamp<NSInteger>(_widthField.integerValue, 16, 16384));
        s.height = int(std::clamp<NSInteger>(_heightField.integerValue, 16, 16384));
    }
    _format = _formatSeg.selectedSegment == 1 ? ImageFormat::Jpeg : ImageFormat::Png;
}

// Auto resolution: the pixel size of the screen the window is on (Retina included).
- (void)updateAutoResolution {
    if (!_autoResolution) return;
    NSScreen* screen = self.window.screen ?: NSScreen.mainScreen;
    const NSRect px = [screen convertRectToBacking:screen.frame];
    _job.settings.width = int(std::lround(px.size.width));
    _job.settings.height = int(std::lround(px.size.height));
    _widthField.integerValue = _job.settings.width;
    _heightField.integerValue = _job.settings.height;
}

- (void)screenChanged:(NSNotification*)note {
    if (!_autoResolution) return;
    [self updateAutoResolution];
    [self updateDerivedViews];
    [self scheduleRender];
}

// ---- actions ----

- (void)changed:(id)sender {
    [self readControls];
    [self updateDerivedViews];
    [self saveDefaults];
    if (sender != _formatSeg) [self scheduleRender];
}

- (void)colorChanged:(NSColorWell*)well {
    if (well == _bgWell) _job.colors.background = ToColor(well.color);
    else _job.colors.glyph = ToColor(well.color);
    [self updateDerivedViews];
    [self saveDefaults];
    [self scheduleRender];
}

// ---- remembered settings ----
// Controls are the source of truth, so they are what gets stored.

- (void)saveDefaults {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    [d setObject:@{
        @"fit" : @(_fitPopup.indexOfSelectedItem),
        @"brightness" : @(_brightness.doubleValue),
        @"contrast" : @(_contrast.doubleValue),
        @"invert" : @(_invert.state == NSControlStateValueOn),
        @"charset" : @(_charsetPopup.indexOfSelectedItem),
        @"customChars" : _customChars.stringValue ?: @"",
        @"font" : _fontPopup.titleOfSelectedItem ?: @"SF Mono",
        @"fontSize" : @(_fontSize.doubleValue),
        @"textMode" : @(_modeSeg.selectedSegment),
        @"text" : _textView.string ?: @"",
        @"layout" : @(_layoutSeg.selectedSegment),
        @"align" : @(_alignSeg.selectedSegment),
        @"textSize" : @(_textSize.doubleValue),
        @"background" : HexString(_job.colors.background),
        @"glyph" : HexString(_job.colors.glyph),
        @"colorMode" : @(_colorModeSeg.selectedSegment),
        @"colorTarget" : @(_colorTargetSeg.selectedSegment),
        @"colorStrength" : @(_strength.doubleValue),
        @"saturation" : @(_saturation.doubleValue),
        @"colorBrightness" : @(_colorBright.doubleValue),
        @"autoResolution" : @(_autoResolution),
        @"width" : @(_job.settings.width),
        @"height" : @(_job.settings.height),
        @"format" : @(_formatSeg.selectedSegment),
    }
          forKey:@"settings"];
}

- (void)loadDefaults {
    NSDictionary* d = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"settings"];
    if (!d) return;
    auto num = [d](NSString* key) -> NSNumber* {
        id v = d[key];
        return [v isKindOfClass:NSNumber.class] ? v : nil;
    };
    auto str = [d](NSString* key) -> NSString* {
        id v = d[key];
        return [v isKindOfClass:NSString.class] ? v : nil;
    };
    auto select = [](NSPopUpButton* p, NSNumber* n) {
        if (n && n.integerValue >= 0 && n.integerValue < p.numberOfItems) [p selectItemAtIndex:n.integerValue];
    };
    auto segment = [](NSSegmentedControl* c, NSNumber* n) {
        if (n && n.integerValue >= 0 && n.integerValue < c.segmentCount) c.selectedSegment = n.integerValue;
    };
    select(_fitPopup, num(@"fit"));
    if (num(@"brightness")) _brightness.doubleValue = num(@"brightness").doubleValue;
    if (num(@"contrast")) _contrast.doubleValue = num(@"contrast").doubleValue;
    if (num(@"invert")) _invert.state = num(@"invert").boolValue ? NSControlStateValueOn : NSControlStateValueOff;
    select(_charsetPopup, num(@"charset"));
    if (str(@"customChars")) _customChars.stringValue = str(@"customChars");
    if (str(@"font") && [_fontPopup itemWithTitle:str(@"font")]) [_fontPopup selectItemWithTitle:str(@"font")];
    if (num(@"fontSize")) _fontSize.doubleValue = num(@"fontSize").doubleValue;
    segment(_modeSeg, num(@"textMode"));
    if (str(@"text")) _textView.string = str(@"text");
    segment(_layoutSeg, num(@"layout"));
    segment(_alignSeg, num(@"align"));
    if (num(@"textSize")) _textSize.doubleValue = num(@"textSize").doubleValue;
    Color c;
    if (str(@"background") && parseHexColor(str(@"background").UTF8String, c)) {
        _job.colors.background = c;
        _bgWell.color = ToNSColor(c);
    }
    if (str(@"glyph") && parseHexColor(str(@"glyph").UTF8String, c)) {
        _job.colors.glyph = c;
        _fgWell.color = ToNSColor(c);
    }
    segment(_colorModeSeg, num(@"colorMode"));
    segment(_colorTargetSeg, num(@"colorTarget"));
    if (num(@"colorStrength")) _strength.doubleValue = num(@"colorStrength").doubleValue;
    if (num(@"saturation")) _saturation.doubleValue = num(@"saturation").doubleValue;
    if (num(@"colorBrightness")) _colorBright.doubleValue = num(@"colorBrightness").doubleValue;
    if (num(@"autoResolution")) _resSeg.selectedSegment = num(@"autoResolution").boolValue ? 0 : 1;
    if (num(@"width")) _widthField.integerValue = num(@"width").integerValue;
    if (num(@"height")) _heightField.integerValue = num(@"height").integerValue;
    segment(_formatSeg, num(@"format"));
    [self readControls];
    [self updateDerivedViews];
}

- (void)presetClicked:(NSButton*)b {
    const ColorPreset& p = kPresets[b.tag];
    _job.colors.background = ToColor(Hex(p.background));
    _job.colors.glyph = ToColor(Hex(p.glyph));
    _bgWell.color = Hex(p.background);
    _fgWell.color = Hex(p.glyph);
    [self updateDerivedViews];
    [self saveDefaults];
    [self scheduleRender];
}

- (void)textDidChange:(NSNotification*)note { [self changed:_textView]; }
- (void)controlTextDidChange:(NSNotification*)note { [self changed:note.object]; }

- (void)openDocument:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[ UTTypeImage ];
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse r) {
                      if (r == NSModalResponseOK) [self openPhotoAtPath:panel.URL.path];
                  }];
}

- (void)openPhotoAtPath:(NSString*)path {
    const std::string p = path.fileSystemRepresentation;
    _fileName.stringValue = path.lastPathComponent;
    _fileInfo.stringValue = @"loading…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string err;
        auto photo = std::make_shared<const GrayImage>(loadPhoto(p, &err));
        NSImage* thumb = [[NSImage alloc] initWithContentsOfFile:path];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (photo->empty()) {
                [self showError:@"Can't open this photo" detail:[NSString stringWithUTF8String:err.c_str()]];
                [self refreshPhotoCard];
                return;
            }
            self->_job.photo = photo;
            self->_photoName = path.lastPathComponent;
            self->_thumb.image = thumb;
            [self refreshPhotoCard];
            [self updateDerivedViews];
            [self scheduleRender];
        });
    });
}

- (void)refreshPhotoCard {
    if (_job.photo) {
        _fileName.stringValue = _photoName;
        _fileInfo.stringValue = [NSString stringWithFormat:@"%d × %d", _job.photo->width, _job.photo->height];
    } else {
        _fileName.stringValue = @"no photo";
        _fileInfo.stringValue = @"jpeg · png · heic";
    }
}

- (void)showError:(NSString*)title detail:(NSString*)detail {
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = title;
    a.informativeText = detail ?: @"";
    [a beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)saveDocument:(id)sender {
    [self readControls];
    const bool jpeg = _format == ImageFormat::Jpeg;
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[ jpeg ? UTTypeJPEG : UTTypePNG ];
    NSString* base = _photoName ? _photoName.stringByDeletingPathExtension : @"wallpaper";
    panel.nameFieldStringValue = [NSString stringWithFormat:@"%@-ascii.%@", base, jpeg ? @"jpg" : @"png"];
    const RenderJob job = _job;
    const ImageFormat format = _format;
    auto renderer = _renderer;
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse r) {
                      if (r != NSModalResponseOK) return;
                      const std::string path = panel.URL.fileSystemRepresentation;
                      dispatch_async(self->_queue, ^{
                          const RgbImage img = renderer->render(job);
                          std::string err;
                          const bool ok = saveImage(img, path, format, 92, &err);
                          dispatch_async(dispatch_get_main_queue(), ^{
                              if (!ok)
                                  [self showError:@"Couldn't save the wallpaper"
                                           detail:[NSString stringWithUTF8String:err.c_str()]];
                          });
                      });
                  }];
}

// ---- rendering ----

// Development aid: WALLPASCII_SNAPSHOT=/path.png saves the window contents
// once the preview has rendered with a photo, then quits.
- (void)writeSnapshotIfRequested {
    NSString* path = NSProcessInfo.processInfo.environment[@"WALLPASCII_SNAPSHOT"];
    if (!path || !_job.photo) return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{
        NSView* v = self.window.contentView.superview ?: self.window.contentView;
        NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
        [NSApp terminate:nil];
    });
}

- (void)scheduleRender {
    if (_rendering) {
        _pending = YES;
        return;
    }
    _rendering = YES;
    _pending = NO;
    const RenderJob job = _job;
    auto renderer = _renderer;
    dispatch_async(_queue, ^{
        const auto t0 = std::chrono::steady_clock::now();
        const RgbImage img = renderer->render(job);
        CGImageRef cg = createCGImage(img);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        NSImage* ns = cg ? [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(img.width, img.height)] : nil;
        if (cg) CGImageRelease(cg);
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_preview.image = ns;
            self->_lastRenderMs = ms;
            [self updateCaption];
            self->_rendering = NO;
            if (self->_pending) [self scheduleRender];
            else [self writeSnapshotIfRequested];
        });
    });
}

@end
