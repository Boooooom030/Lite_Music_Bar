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

    // Set styles for a genuine layered taskbar child window
    LONG_PTR exStyle = GetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    exStyle &= ~(WS_EX_TOPMOST | WS_EX_APPWINDOW);
    SetWindowLongPtrW(m_hAppWnd, GWL_EXSTYLE, exStyle);

    LONG_PTR style = GetWindowLongPtrW(m_hAppWnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(m_hAppWnd, GWL_STYLE, style);

    // Dock as direct child of the taskbar (Shell_TrayWnd)
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
    int scaledHeight = tbHeight;

    // Fill the full vertical height of the taskbar: zero top/bottom gaps!
    m_relY = 0;

    int posX = 0;
    if (m_offsetX >= 0) {
        posX = m_offsetX;
    } else {
        if (m_hTrayNotify && IsWindow(m_hTrayNotify)) {
            RECT rcTray{};
            GetWindowRect(m_hTrayNotify, &rcTray);
            int trayRelLeft = rcTray.left - m_rcTaskbar.left;
            posX = trayRelLeft - scaledWidth - static_cast<int>(12 * scale);
        } else {
            posX = tbWidth - scaledWidth - static_cast<int>(200 * scale);
        }
    }

    // Clamp horizontally strictly inside taskbar boundaries
    if (posX < 0) posX = 0;
    if (posX + scaledWidth > tbWidth) posX = tbWidth - scaledWidth;

    m_relX = posX;

    // Set position within Shell_TrayWnd client coordinates
    SetWindowPos(
        m_hAppWnd,
        HWND_TOP,
        m_relX,
        m_relY,
        scaledWidth,
        scaledHeight,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );
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
