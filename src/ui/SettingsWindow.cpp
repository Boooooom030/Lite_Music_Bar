#include "SettingsWindow.hpp"

#include "qt/SettingsQtWindow.hpp"

namespace MusicBar {

struct SettingsWindow::Impl {
    SettingsQtWindow window;
};

SettingsWindow::SettingsWindow() : m_impl(std::make_unique<Impl>()) {}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::Show(HINSTANCE /*instance*/, const AppSettings& settings,
    std::function<void(const AppSettings&)> onChanged) {
    // The Qt window owns its own HINSTANCE: Qt's Windows integration reads the
    // module handle itself, so the facade keeps the instance parameter only for
    // the calling convention.
    m_impl->window.Show(settings, std::move(onChanged));
}

void SettingsWindow::SetSettings(const AppSettings& settings) {
    m_impl->window.SetSettings(settings);
}

bool SettingsWindow::IsOpen() const {
    return m_impl->window.IsOpen();
}

void SettingsWindow::OnSystemThemeChanged() {
    m_impl->window.OnSystemThemeChanged();
}

void SettingsWindow::Shutdown() {
    m_impl->window.Shutdown();
}

} // namespace MusicBar
