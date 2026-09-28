#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace MusicBar {

constexpr UINT WM_APP_TRAYICON = WM_APP + 101;

constexpr UINT IDM_SHOW_HIDE     = 1001;
constexpr UINT IDM_TOGGLE_LYRICS = 1002;
constexpr UINT IDM_REDOCK        = 1003;
constexpr UINT IDM_LOCK_POSITION = 1004;
constexpr UINT IDM_SETTINGS      = 1005;
constexpr UINT IDM_SOURCE_AUTO   = 1010;
constexpr UINT IDM_SOURCE_NETEASE= 1011;
constexpr UINT IDM_SOURCE_QQ     = 1012;
constexpr UINT IDM_SOURCE_KUGOU  = 1013;
constexpr UINT IDM_SOURCE_LRCLIB = 1014;
constexpr UINT IDM_SOURCE_SODA   = 1015;
constexpr UINT IDM_EXIT          = 1099;

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool Create(HWND hWnd, HINSTANCE hInstance, UINT uID);
    void Remove();
    void SetTooltip(const std::wstring& tip);
    void ShowContextMenu(HWND hWnd, int x, int y, int currentSource,
        bool showLyrics, bool isVisible, bool positionLocked);

private:
    NOTIFYICONDATAW m_nid{};
    bool m_isCreated = false;
};

} // namespace MusicBar
