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
        if (pGetDpiForWindow) {
            UINT dpi = pGetDpiForWindow(m_hTaskbar);
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

    // DirectComposition supplies the pixels. Skip the GDI redirection surface;
    // input is bounded by SetWindowRgn rather than a layered bitmap's alpha.
    LONG_PTR exStyle = GetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE);
    exStyle |= WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW;
    exStyle &= ~(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_APPWINDOW);
    SetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE, exStyle);

    LONG_PTR style = GetWindowLongPtrW(m_hAppWnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(m_hAppWnd, GWL_STYLE, style);

    // Dock as direct child of the taskbar (Shell_TrayWnd)
    SetLastError(0);
    HWND previousParent = SetParent(m_hAppWnd, m_hTaskbar);
    if (!previousParent && GetLastError() != 0) return false;

    m_isDocked = true;
    m_canvasWidth = 0;
    m_canvasHeight = 0;
    m_regionApplied = false;
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
    int scaledHeight = tbHeight;
    m_relY = 0;

    // Keep the bar before the notification area on narrow or high-DPI taskbars.
    int trayRelLeft = tbWidth;
    if (m_hTrayNotify && IsWindow(m_hTrayNotify)) {
        RECT rcTray{};
        if (GetWindowRect(m_hTrayNotify, &rcTray)) {
            trayRelLeft = rcTray.left - m_rcTaskbar.left;
        }
    }
    int desiredWidth = static_cast<int>(m_barWidth * scale);
    int trayGap = static_cast<int>(12 * scale);
    int leftMargin = static_cast<int>(8 * scale);
    int availableWidth = std::max(1, trayRelLeft - trayGap - leftMargin);
    int screenShareLimit = std::max(1, static_cast<int>(tbWidth * 0.34f));
    int scaledWidth = std::min(desiredWidth, std::min(screenShareLimit, availableWidth));
    m_effectiveWidth = scaledWidth;

    int posX = 0;
    if (m_offsetX >= 0) {
        posX = m_offsetX;
    } else {
        posX = trayRelLeft - scaledWidth - trayGap;
    }

    // Clamp horizontally strictly inside taskbar boundaries
    if (posX < 0) posX = 0;
    if (posX + scaledWidth > tbWidth) posX = std::max(0, tbWidth - scaledWidth);

    m_relX = posX;

    // The child covers the taskbar canvas. Only the rounded bar region receives input.
    if (m_canvasWidth != tbWidth || m_canvasHeight != tbHeight) {
        SetWindowPos(m_hAppWnd, HWND_TOP, 0, 0, tbWidth, tbHeight,
            SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        m_canvasWidth = tbWidth;
        m_canvasHeight = tbHeight;
        m_regionApplied = false;
    }

    RECT nextRegion{ m_relX, m_relY, m_relX + scaledWidth, m_relY + scaledHeight };
    if (!m_regionApplied || !EqualRect(&m_appliedRegion, &nextRegion)) {
        const int radius = std::max(1, static_cast<int>(6 * scale));
        // GDI's rounded region excludes its right and bottom edge pixels.
        HRGN region = CreateRoundRectRgn(nextRegion.left, nextRegion.top,
            nextRegion.right + 1, nextRegion.bottom + 1, radius * 2, radius * 2);
        if (region) {
            if (SetWindowRgn(m_hAppWnd, region, TRUE)) {
                m_appliedRegion = nextRegion;
                m_regionApplied = true;
            } else {
                DeleteObject(region);
            }
        }
    }
}

void TaskbarDock::SetBarOffset(int relX) {
    m_offsetX = relX;
    UpdateLayout();
}

void TaskbarDock::MoveBarDelta(int dx) {
    if (m_offsetX < 0) {
        m_offsetX = m_relX;
    }
    m_offsetX += dx;
    UpdateLayout();
}

} // namespace MusicBar
