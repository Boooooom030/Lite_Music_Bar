#include "core/App.hpp"
#include <windows.h>

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPWSTR /*lpCmdLine*/,
    int /*nCmdShow*/
) {
    // Ensure attaching to default interactive desktop
    HWINSTA hWinsta0 = OpenWindowStationW(L"WinSta0", FALSE, MAXIMUM_ALLOWED);
    if (hWinsta0) {
        SetProcessWindowStation(hWinsta0);
        HDESK hDesk = OpenDesktopW(L"default", 0, FALSE, MAXIMUM_ALLOWED);
        if (hDesk) {
            SetThreadDesktop(hDesk);
        }
    }

    // Initialize COM for the main UI thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return -1;

    MusicBar::App app;
    if (!app.Initialize(hInstance)) {
        CoUninitialize();
        return 0;
    }

    int exitCode = app.Run();
    CoUninitialize();
    return exitCode;
}
