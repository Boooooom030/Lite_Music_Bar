#include "TaskbarDock.hpp"
#include <shellapi.h>
#include <algorithm>

namespace MusicBar {

TaskbarDock::TaskbarDock() = default;

TaskbarDock::~TaskbarDock() {
    Detach();
}

void TaskbarDock::RefreshTaskbarInfo() {
    m_hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!m_hTaskbar) return;

    m_hTrayNotify = FindWindowExW(m_hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
    GetWindowRect(m_hTaskbar, &m_rcTaskbar);

    m_dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);
        auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowFunc>(GetProcAddress(hUser32, "GetDpiForWindow"));
        if (pGetDpiForWindow && m_hAppWnd) {
            UINT dpi = pGetDpiForWindow(m_hAppWnd);
            if (dpi > 0) m_dpi = dpi;
        }
    }
}

bool TaskbarDock::Attach(HWND hAppWnd) {
    m_hAppWnd = hAppWnd;
    RefreshTaskbarInfo();

    if (!m_hTaskbar) {
        return false;
    }

    // Set as child of Shell_TrayWnd
    LONG_PTR style = GetWindowLongPtrW(m_hAppWnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(m_hAppWnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE, exStyle);

    SetParent(m_hAppWnd, m_hTaskbar);
    m_isDocked = true;

    UpdateLayout();
    return true;
}

void TaskbarDock::Detach() {
    if (m_isDocked && m_hAppWnd && IsWindow(m_hAppWnd)) {
        SetParent(m_hAppWnd, nullptr);
        LONG_PTR style = GetWindowLongPtrW(m_hAppWnd, GWL_STYLE);
        style &= ~WS_CHILD;
        style |= WS_POPUP;
        SetWindowLongPtrW(m_hAppWnd, GWL_STYLE, style);
        m_isDocked = false;
    }
}

void TaskbarDock::UpdateLayout() {
    if (!m_hAppWnd || !m_isDocked) return;

    RefreshTaskbarInfo();
    if (!m_hTaskbar) return;

    int tbWidth = m_rcTaskbar.right - m_rcTaskbar.left;
    int tbHeight = m_rcTaskbar.bottom - m_rcTaskbar.top;

    float scale = GetDpiScale();
    int scaledWidth = static_cast<int>(m_barWidth * scale);
    int scaledHeight = static_cast<int>(m_barHeight * scale);

    // Calculate default X position (left of TrayNotifyWnd)
    int posX = 0;
    int posY = (tbHeight - scaledHeight) / 2;
    if (posY < 2) posY = 2;

    if (m_offsetX >= 0) {
        posX = m_offsetX;
        posY = m_offsetY;
    } else {
        if (m_hTrayNotify && IsWindow(m_hTrayNotify)) {
            RECT rcTray{};
            GetWindowRect(m_hTrayNotify, &rcTray);
            int trayRelLeft = rcTray.left - m_rcTaskbar.left;
            posX = trayRelLeft - scaledWidth - static_cast<int>(12 * scale);
        } else {
            posX = tbWidth - scaledWidth - static_cast<int>(120 * scale);
        }
    }

    // Clamp coordinates within taskbar bounds
    if (posX < 0) posX = 0;
    if (posX + scaledWidth > tbWidth) posX = tbWidth - scaledWidth;
    if (posY < 0) posY = 0;
    if (posY + scaledHeight > tbHeight) posY = tbHeight - scaledHeight;

    m_rcBar.left = posX;
    m_rcBar.top = posY;
    m_rcBar.right = posX + scaledWidth;
    m_rcBar.bottom = posY + scaledHeight;

    // Window must cover entire taskbar canvas for DWM / DirectComposition
    SetWindowPos(m_hAppWnd, HWND_TOP, 0, 0, tbWidth, tbHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // Apply region clipping strictly to the bar area so outside areas pass-through to taskbar
    ApplyWindowRegion();

    // Trigger repaint
    InvalidateRect(m_hAppWnd, nullptr, FALSE);
}

void TaskbarDock::SetBarOffset(int offsetX, int offsetY) {
    m_offsetX = offsetX;
    m_offsetY = offsetY;
    UpdateLayout();
}

void TaskbarDock::MoveBarDelta(int dx, int dy) {
    if (m_offsetX < 0) {
        m_offsetX = m_rcBar.left;
        m_offsetY = m_rcBar.top;
    }
    m_offsetX += dx;
    m_offsetY += dy;
    UpdateLayout();
}

void TaskbarDock::ApplyWindowRegion() {
    if (!m_hAppWnd) return;

    // Direct2D renders rounded corners smoothly, but Win32 region provides hit-testing boundary
    HRGN hRgn = CreateRoundRectRgn(
        m_rcBar.left,
        m_rcBar.top,
        m_rcBar.right + 1,
        m_rcBar.bottom + 1,
        static_cast<int>(10 * GetDpiScale()),
        static_cast<int>(10 * GetDpiScale())
    );

    // SetWindowRgn takes ownership of hRgn on success
    if (!SetWindowRgn(m_hAppWnd, hRgn, TRUE)) {
        DeleteObject(hRgn);
    }
}

} // namespace MusicBar
