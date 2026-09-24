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

    // Set owner to Shell_TrayWnd so it minimizes/hides with taskbar
    SetWindowLongPtrW(m_hAppWnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_hTaskbar));

    // Ensure Topmost popup styles
    LONG_PTR style = GetWindowLongPtrW(m_hAppWnd, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(m_hAppWnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE, exStyle);

    m_isDocked = true;
    UpdateLayout();
    return true;
}

void TaskbarDock::Detach() {
    m_isDocked = false;
}

void TaskbarDock::UpdateLayout() {
    if (!m_hAppWnd || !m_isDocked) return;

    RefreshTaskbarInfo();
    if (!m_hTaskbar) return;

    int tbLeft = m_rcTaskbar.left;
    int tbTop = m_rcTaskbar.top;
    int tbWidth = m_rcTaskbar.right - m_rcTaskbar.left;
    int tbHeight = m_rcTaskbar.bottom - m_rcTaskbar.top;

    float scale = GetDpiScale();
    int scaledWidth = static_cast<int>(m_barWidth * scale);
    int scaledHeight = static_cast<int>(m_barHeight * scale);

    int posX = 0;
    int posY = tbTop + (tbHeight - scaledHeight) / 2;

    if (m_offsetX >= 0) {
        posX = m_offsetX;
        posY = (m_offsetY >= 0) ? m_offsetY : (tbTop + (tbHeight - scaledHeight) / 2);
    } else {
        if (m_hTrayNotify && IsWindow(m_hTrayNotify)) {
            RECT rcTray{};
            GetWindowRect(m_hTrayNotify, &rcTray);
            posX = rcTray.left - scaledWidth - static_cast<int>(14 * scale);
        } else {
            posX = tbLeft + tbWidth - scaledWidth - static_cast<int>(200 * scale);
        }
    }

    // Clamp coordinates within taskbar screen bounds
    if (posX < tbLeft) posX = tbLeft;
    if (posX + scaledWidth > tbLeft + tbWidth) posX = tbLeft + tbWidth - scaledWidth;

    m_screenX = posX;
    m_screenY = posY;

    // Set position and size of the MusicBar window
    SetWindowPos(
        m_hAppWnd,
        HWND_TOPMOST,
        m_screenX,
        m_screenY,
        scaledWidth,
        scaledHeight,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );

    // Apply smooth rounded corner region
    ApplyWindowRegion();

    // Trigger repaint
    InvalidateRect(m_hAppWnd, nullptr, FALSE);
}

void TaskbarDock::SetBarOffset(int screenX, int screenY) {
    m_offsetX = screenX;
    m_offsetY = screenY;
    UpdateLayout();
}

void TaskbarDock::MoveBarDelta(int dx, int dy) {
    if (m_offsetX < 0) {
        m_offsetX = m_screenX;
        m_offsetY = m_screenY;
    }
    m_offsetX += dx;
    m_offsetY += dy;
    UpdateLayout();
}

void TaskbarDock::ApplyWindowRegion() {
    if (!m_hAppWnd) return;

    int w = GetScaledWidth();
    int h = GetScaledHeight();
    float scale = GetDpiScale();

    HRGN hRgn = CreateRoundRectRgn(
        0,
        0,
        w + 1,
        h + 1,
        static_cast<int>(8 * scale),
        static_cast<int>(8 * scale)
    );

    if (!SetWindowRgn(m_hAppWnd, hRgn, TRUE)) {
        DeleteObject(hRgn);
    }
}

} // namespace MusicBar
