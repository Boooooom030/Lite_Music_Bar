#pragma once

#include "core/SettingsStore.hpp"

#include <windows.h>

#include <functional>
#include <memory>

namespace MusicBar {

// Settings window facade for the rest of the application. The visible window is
// a Fluent-Qt (Qt Widgets) top-level window created on first use, hidden when it
// is closed and reused afterwards; the taskbar bar, tray, media and spectrum
// keep running while it is hidden.
//
// The Qt implementation lives behind a pimpl so this header stays free of Qt
// types.
class SettingsWindow {
public:
    SettingsWindow();
    ~SettingsWindow();

    // Creates the window on first use, otherwise restores and activates the one
    // already owned by this process. `onChanged` is applied through App's
    // ApplySettings/PersistSettings pair.
    void Show(HINSTANCE instance, const AppSettings& settings,
        std::function<void(const AppSettings&)> onChanged);
    // Pushes externally changed settings (tray menu, settings file reload) into
    // the controls without emitting onChanged again.
    void SetSettings(const AppSettings& settings);
    // True while the window is actually visible, not merely allocated.
    bool IsOpen() const;

    // Re-reads the Windows light/dark/high-contrast state and reapplies it.
    void OnSystemThemeChanged();

    // Releases the Qt window. Must run on the UI thread before the QApplication
    // is destroyed, and is safe to call more than once.
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MusicBar
