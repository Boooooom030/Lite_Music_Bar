// FluentQt is included before windows.h: rpcndr.h (pulled in by windows.h)
// defines `small` as a macro, which collides with FluentElement's spacing
// tokens.
#include <FluentQt/FluentQt.h>

#include "core/App.hpp"

#include <windows.h>

#include <QApplication>
#include <QIcon>

// MusicBar runs one QApplication on the UI thread that also owns the taskbar
// HWND and its Direct2D/DirectComposition drawing. Qt's Windows entry point
// library supplies WinMain and forwards to this standard main().
int main(int argc, char** argv) {
    // Ensure attaching to the default interactive desktop.
    HWINSTA hWinsta0 = OpenWindowStationW(L"WinSta0", FALSE, MAXIMUM_ALLOWED);
    if (hWinsta0) {
        SetProcessWindowStation(hWinsta0);
        HDESK hDesk = OpenDesktopW(L"default", 0, FALSE, MAXIMUM_ALLOWED);
        if (hDesk) {
            SetThreadDesktop(hDesk);
        }
    }

    // Initialize COM for the main UI thread. Qt pairs its own OLE
    // initialization; this apartment is paired with the CoUninitialize below.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return -1;

    int exitCode = 0;
    {
        // The High-DPI policy has to be set before QApplication exists; the
        // manifest keeps PerMonitorV2 for the taskbar window.
        fluent::prepareHighDpiApplication();

        QApplication application(argc, argv);
        // Closing the settings window must not end the process: the taskbar
        // bar, tray icon, media session and spectrum keep running.
        application.setQuitOnLastWindowClosed(false);
        application.setApplicationName(QStringLiteral("MusicBar"));
        application.setApplicationDisplayName(QStringLiteral("MusicBar"));
        application.setWindowIcon(QIcon(QStringLiteral(":/assets/icon.ico")));

        if (!fluent::initializeResources()) {
            OutputDebugStringW(L"MusicBar: FluentQt resources did not initialize.\n");
        }
        application.setFont(
            Typography::fontStyle(Typography::FontRole::Body).toQFont());

        if (application.arguments().contains(QStringLiteral("--quit"))) {
            exitCode = MusicBar::App::RequestExitForExistingInstance() ? 0 : 1;
        } else {
            const bool openSettings = application.arguments().contains(QStringLiteral("--settings"));
            MusicBar::App app([&application] { application.exit(0); });
            if (app.Initialize(GetModuleHandleW(nullptr), openSettings)) {
                if (openSettings) app.OpenSettings();
                exitCode = app.Run([&application] { return application.exec(); });
            }
        }

        // Every QWidget is destroyed before the QApplication leaves this scope:
        // App releases the settings window in Run(), and App itself dies here.
    }

    CoUninitialize();
    return exitCode;
}
