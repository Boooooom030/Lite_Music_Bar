#pragma once

#include "core/SettingsStore.hpp"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QEvent;
class QWidget;
namespace fluent::collections { class ListView; }
namespace fluent::dialogs_flyouts { class Flyout; }
namespace fluent::navigation { class NavigationView; }
namespace fluent::basicinput {
class Button;
class ColorPicker;
class ComboBox;
class ToggleSwitch;
}
namespace fluent::textfields { class Label; }
namespace fluent::windowing { class Window; }

namespace MusicBar {

// One modeless, lazily created surface with category navigation. Hiding it
// leaves the native taskbar runtime untouched.
class SettingsQtWindow : public QObject {
    Q_OBJECT
public:
    using ChangeHandler = std::function<void(const AppSettings&)>;
    explicit SettingsQtWindow(QObject* parent = nullptr);
    ~SettingsQtWindow() override;
    void Show(const AppSettings& settings, ChangeHandler handler);
    void SetSettings(const AppSettings& settings);
    bool IsOpen() const;
    void OnSystemThemeChanged();
    void Shutdown();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void BuildWindow();
    void ConnectSignals();
    void SyncFromSettings(const AppSettings& settings);
    bool ReadControls(AppSettings& settings) const;
    void CommitFromControls();
    void ResetLyricColor();
    void ReloadFromFile();
    void RestoreDefaults();
    void UpdateThemeFromSystem();
    void ApplyCaptionTheme();
    void UpdateBackdrop();
    void UpdateWindowIcon();
    void UpdateNavigationIcons();
    void SelectCategory(int index);
    void Hide();
    void PlaceOnActiveScreen();
    void CloseOpenPopups();
    void PublishStatus(const QString& message);

    std::unique_ptr<fluent::windowing::Window> m_window;
    fluent::navigation::NavigationView* m_navigation = nullptr;
    fluent::collections::ListView* m_navigationList = nullptr;
    fluent::textfields::Label* m_pageHeading = nullptr;
    fluent::textfields::Label* m_pageDescription = nullptr;
    QWidget* m_sections[3]{};
    fluent::basicinput::ToggleSwitch* m_lockSwitch = nullptr;
    fluent::basicinput::ToggleSwitch* m_showLyricsSwitch = nullptr;
    fluent::basicinput::ComboBox* m_sourceCombo = nullptr;
    fluent::basicinput::ColorPicker* m_lyricColorPicker = nullptr;
    fluent::dialogs_flyouts::Flyout* m_lyricColorFlyout = nullptr;
    fluent::basicinput::Button* m_lyricColorButton = nullptr;
    fluent::basicinput::Button* m_lyricColorAutoButton = nullptr;
    fluent::basicinput::ComboBox* m_captureCombo = nullptr;
    fluent::basicinput::Button* m_reloadButton = nullptr;
    fluent::basicinput::Button* m_resetButton = nullptr;
    fluent::textfields::Label* m_statusLabel = nullptr;
    ChangeHandler m_onChanged;
    AppSettings m_settings{};
    int m_appliedTheme = -1;
    bool m_placed = false;
    // True once the user picks a colour; keeps "follow the theme" (0) as the
    // stored value until then.
    bool m_lyricColorEdited = false;
};
} // namespace MusicBar
