#import "AppDelegate.h"

#import "MainWindowController.h"

@implementation AppDelegate {
    MainWindowController* _window;
    NSString* _pendingPath;  // a file opened before the window exists
}

- (void)applicationWillFinishLaunching:(NSNotification*)note {
    NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [self buildMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    _window = [[MainWindowController alloc] init];
    [_window showWindow:nil];
    // `wallpascii photo.jpg` from a terminal; flags like -NSDocumentRevisionsDebugMode are skipped.
    NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
    for (NSUInteger i = 1; i < args.count && !_pendingPath; ++i) {
        if (![args[i] hasPrefix:@"-"] && [NSFileManager.defaultManager fileExistsAtPath:args[i]])
            _pendingPath = args[i];
        else if ([args[i] hasPrefix:@"-"])
            ++i;  // AppKit flags take a value
    }
    if (_pendingPath) [_window openPhotoAtPath:_pendingPath];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

// Photos dropped on the Dock icon or opened with "Open With".
- (void)application:(NSApplication*)sender openURLs:(NSArray<NSURL*>*)urls {
    NSString* path = urls.firstObject.path;
    if (!path) return;
    if (_window) [_window openPhotoAtPath:path];
    else _pendingPath = path;
}

- (void)openDocument:(id)sender { [_window openDocument:sender]; }
- (void)saveDocument:(id)sender { [_window saveDocument:sender]; }

- (void)buildMenu {
    NSMenu* bar = [[NSMenu alloc] init];

    NSMenu* app = [[NSMenu alloc] initWithTitle:@"wallpascii"];
    [app addItemWithTitle:@"About wallpascii" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"Hide wallpascii" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:)
                                 keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"Quit wallpascii" action:@selector(terminate:) keyEquivalent:@"q"];

    NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItemWithTitle:@"Choose Photo…" action:@selector(openDocument:) keyEquivalent:@"o"];
    [file addItemWithTitle:@"Save Wallpaper…" action:@selector(saveDocument:) keyEquivalent:@"s"];
    [file addItem:[NSMenuItem separatorItem]];
    [file addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];

    // Needed for copy/paste/undo in the text fields.
    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    NSMenuItem* redo = [edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"];
    redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];

    NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
    [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [window addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];

    for (NSMenu* m in @[ app, file, edit, window ]) {
        NSMenuItem* item = [[NSMenuItem alloc] init];
        item.submenu = m;
        [bar addItem:item];
    }
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = window;
}

@end
