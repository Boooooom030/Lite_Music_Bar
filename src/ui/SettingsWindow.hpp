#pragma once

#include "core/SettingsStore.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite_2.h>
#include <wrl/client.h>
#include <functional>
#include <string>


namespace MusicBar {

// Modeless, native settings window owned by the main message loop.
class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow();

    void Show(HINSTANCE instance, const AppSettings& settings,
        std::function<void(const AppSettings&)> onChanged);
    void SetSettings(const AppSettings& settings);
    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    enum class Page { Taskbar, Lyrics, Audio };
    enum class Dropdown { None, LyricsSource, CaptureMode };
    enum class Hit { None, NavTaskbar, NavLyrics, NavAudio,
        LockPosition, ShowLyrics, LyricsSource, CaptureMode,
        Reload, Reset, DropdownItem };

    struct HitResult {
        Hit type = Hit::None;
        int index = -1;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void UpdateTheme();
    void ResizeTarget();
    bool EnsureResources();
    void Paint();
    HitResult HitTest(float x, float y) const;
    void Activate(HitResult hit);
    void Commit();
    void LoadFromFile();
    void DrawText(const wchar_t* text, const D2D1_RECT_F& rect,
        IDWriteTextFormat* format, D2D1_COLOR_F color);
    void DrawCard(float top, const wchar_t* title, bool toggle,
        bool checked, const wchar_t* value, bool hovered);
    D2D1_RECT_F DropdownRect() const;

    HWND m_hwnd = nullptr;
    std::function<void(const AppSettings&)> m_onChanged;
    AppSettings m_settings{};
    Page m_page = Page::Taskbar;
    Dropdown m_dropdown = Dropdown::None;
    HitResult m_hovered{};
    int m_dropdownSelection = 0;
    bool m_light = true;
    bool m_trackingMouse = false;
    float m_scale = 1.0f;
    float m_width = 960.0f;
    float m_height = 620.0f;
    std::wstring m_error;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_factory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_writeFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_brandFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_sectionFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_bodyFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_smallFormat;
};

} // namespace MusicBar
