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
    void SetBarOffset(int offsetX, int offsetY);
    void MoveBarDelta(int dx, int dy);

    HWND GetTaskbarHwnd() const { return m_hTaskbar; }
    RECT GetTaskbarRect() const { return m_rcTaskbar; }
    RECT GetBarRect() const { return m_rcBar; }
    int GetDpi() const { return m_dpi; }
    float GetDpiScale() const { return m_dpi / 96.0f; }
    bool IsDocked() const { return m_isDocked; }

private:
    HWND m_hAppWnd = nullptr;
    HWND m_hTaskbar = nullptr;
    HWND m_hTrayNotify = nullptr;
    RECT m_rcTaskbar{};
    RECT m_rcBar{};
    int m_offsetX = -1; // -1 means auto-calculate default (left of tray)
    int m_offsetY = 0;
    int m_barWidth = 280;
    int m_barHeight = 36;
    int m_dpi = 96;
    bool m_isDocked = false;

    void RefreshTaskbarInfo();
    void ApplyWindowRegion();
};

} // namespace MusicBar
