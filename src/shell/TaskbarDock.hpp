#pragma once

#include <windows.h>
#include <cstdint>

namespace MusicBar {

class TaskbarDock {
public:
    TaskbarDock();
    ~TaskbarDock();

    bool Attach(HWND hAppWnd);
    void Detach();
    void UpdateLayout();
    void SetBarOffset(int relX);
    void MoveBarDelta(int dx);

    HWND GetTaskbarHwnd() const { return m_hTaskbar; }
    RECT GetTaskbarRect() const { return m_rcTaskbar; }
    RECT GetBarRect() const { return RECT{ 0, 0, GetScaledWidth(), GetScaledHeight() }; }
    POINT GetScreenPos() const { return POINT{ m_rcTaskbar.left + m_relX, m_rcTaskbar.top + m_relY }; }
    POINT GetRelativePos() const { return POINT{ m_relX, m_relY }; }
    int GetScaledWidth() const { return static_cast<int>(m_barWidth * GetDpiScale()); }
    int GetScaledHeight() const {
        int h = m_rcTaskbar.bottom - m_rcTaskbar.top;
        return (h > 0) ? h : static_cast<int>(m_barHeight * GetDpiScale());
    }
    int GetDpi() const { return m_dpi; }
    float GetDpiScale() const { return m_dpi / 96.0f; }
    bool IsDocked() const { return m_isDocked; }

private:
    HWND m_hAppWnd = nullptr;
    HWND m_hTaskbar = nullptr;
    HWND m_hTrayNotify = nullptr;
    RECT m_rcTaskbar{};
    int m_offsetX = -1; // -1 means auto-calculate default (left of tray)
    int m_relX = 0;
    int m_relY = 0;
    int m_barWidth = 300;
    int m_barHeight = 48;
    int m_dpi = 96;
    bool m_isDocked = false;

    void RefreshTaskbarInfo();
};

} // namespace MusicBar
