#include "core/App.hpp"
#include <windows.h>

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    LPWSTR /*lpCmdLine*/,
    int /*nCmdShow*/
) {
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
