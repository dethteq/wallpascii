#include <windows.h>

#include <ole2.h>
#include <shellapi.h>

#include <string>

#include "MainWindow.h"

namespace {

// Dark popup menus (Windows 10 1903+), through uxtheme's unnamed exports.
void allowDarkMenus() {
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    using FlushMenuThemes = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppMode>(
        reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FlushMenuThemes>(
        reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))));
    if (setMode) setMode(2 /* ForceDark */);
    if (flush) flush();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int cmdShow) {
    // Per-monitor DPI awareness also comes from the manifest; this covers
    // builds without it.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);
    allowDarkMenus();
    if (!MainWindow::registerClass(instance)) return 1;

    MainWindow window;
    if (!window.hwnd()) return 1;
    window.show(cmdShow);

    // `wallpascii photo.jpg`, or a photo dropped on the exe.
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const DWORD attrs = GetFileAttributesW(argv[i]);
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                window.openPhoto(argv[i]);
                break;
            }
        }
        LocalFree(argv);
    }

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'O', WORD(kCmdOpen)},
        {FCONTROL | FVIRTKEY, 'S', WORD(kCmdSave)},
    };
    HACCEL accel = CreateAcceleratorTableW(keys, 2);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Ctrl+A selects all in text fields (multi-line EDITs lack it).
        if (msg.message == WM_KEYDOWN && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            msg.hwnd != window.hwnd()) {
            SendMessageW(msg.hwnd, EM_SETSEL, 0, -1);
            continue;
        }
        if (TranslateAcceleratorW(window.hwnd(), accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyAcceleratorTable(accel);
    OleUninitialize();
    return int(msg.wParam);
}
