#include "TrayIcon.hpp"

namespace MusicBar {

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Create(HWND hWnd, HINSTANCE hInstance, UINT uID) {
    if (m_isCreated) return true;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hWnd;
    m_nid.uID = uID;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAYICON;

    // Load application icon (resource ID 101) or standard note icon
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!hIcon) {
        hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    m_nid.hIcon = hIcon;

    wcscpy_s(m_nid.szTip, L"MusicBar (C++)");

    m_isCreated = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_isCreated;
}

void TrayIcon::Remove() {
    if (m_isCreated) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_isCreated = false;
    }
}

void TrayIcon::SetTooltip(const std::wstring& tip) {
    if (!m_isCreated) return;
    wcsncpy_s(m_nid.szTip, tip.c_str(), _countof(m_nid.szTip) - 1);
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowContextMenu(
    HWND hWnd,
    int x,
    int y,
    int currentSource,
    bool showLyrics,
    bool isVisible,
    bool positionLocked
) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // 1. Show / Hide
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW_HIDE, isVisible ? L"隐藏任务栏栏目 (Hide)" : L"显示任务栏栏目 (Show)");
    AppendMenuW(hMenu, MF_STRING | (showLyrics ? MF_CHECKED : MF_UNCHECKED), IDM_TOGGLE_LYRICS, L"开启歌词显示 (Show Lyrics)");
    AppendMenuW(hMenu, MF_STRING, IDM_REDOCK, L"重新停靠任务栏 (Re-dock Taskbar)");
    AppendMenuW(hMenu, MF_STRING | (positionLocked ? MF_CHECKED : MF_UNCHECKED),
        IDM_LOCK_POSITION, L"锁定位置 (Lock Position)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. Lyrics Source Submenu
    HMENU hSourceSubMenu = CreatePopupMenu();
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 0 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_AUTO, L"自动切换 (Auto)");
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 1 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_NETEASE, L"网易云音乐 (NetEase)");
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 2 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_QQ, L"QQ音乐 (QQ Music)");
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 3 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_KUGOU, L"酷狗音乐 (Kugou)");
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 4 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_SODA, L"汽水音乐 (Soda)");
    AppendMenuW(hSourceSubMenu, MF_STRING | (currentSource == 5 ? MF_CHECKED : MF_UNCHECKED), IDM_SOURCE_LRCLIB, L"LRCLIB (欧美歌词)");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSourceSubMenu), L"歌词源选择 (Lyrics Source)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"退出 (Exit)");

    // Ensure menu closes if user clicks outside
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

} // namespace MusicBar
