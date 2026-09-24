#include "App.hpp"
#include <windowsx.h>

namespace MusicBar {

constexpr UINT WM_APP_TRACK_CHANGED = WM_APP + 102;
constexpr UINT WM_APP_LYRICS_LOADED = WM_APP + 103;

App::App() {
    s_pInstance = this;
}

App::~App() {
    if (m_hTaskbarHook) {
        UnhookWinEvent(m_hTaskbarHook);
        m_hTaskbarHook = nullptr;
    }
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }
    if (m_hSingleInstanceMutex) {
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = nullptr;
    }
}

void CALLBACK App::StaticWinEventProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG /*idChild*/,
    DWORD /*idEventThread*/,
    DWORD /*dwmsEventTime*/
) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE && idObject == OBJID_WINDOW) {
        if (s_pInstance && s_pInstance->m_dock.GetTaskbarHwnd() == hwnd) {
            s_pInstance->m_dock.UpdateLayout();
            s_pInstance->RepaintBar();
        }
    }
}

LRESULT CALLBACK App::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* pApp = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pApp = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pApp));
    } else {
        pApp = reinterpret_cast<App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pApp) {
        return pApp->HandleMessage(hWnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool App::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    // Single-instance enforcement
    m_hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Global\\MusicBar_Cpp_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"MusicBar_Taskbar_Host", nullptr);
        if (hExisting) {
            ShowWindow(hExisting, SW_SHOW);
            SetForegroundWindow(hExisting);
        }
        return false;
    }

    // Register Window Class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MusicBar_Taskbar_Host";

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    m_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Create host window with Layered child styles
    m_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"MusicBar_Taskbar_Host",
        L"MusicBar",
        WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 280, 36,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hWnd) return false;

    // Dock as direct child of the Windows taskbar
    m_dock.Attach(m_hWnd);

    // Initialize Direct2D with layered DC render target
    if (!m_renderer.Initialize(m_hWnd)) {
        return false;
    }

    // Start WASAPI audio capture
    m_audio.Start();

    // Setup lyrics service
    m_lyrics.SetCallback([this](const LyricDocument& doc) {
        OnLyricsLoaded(doc);
    });

    // Setup media session
    m_mediaSession.Initialize(m_hWnd, [this](const MediaTrackInfo& track) {
        OnTrackChanged(track);
    });

    // Create system tray icon
    m_trayIcon.Create(m_hWnd, hInstance, 1);

    // Hook taskbar location changes for real-time auto-hide & collapse tracking
    m_hTaskbarHook = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        StaticWinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    // Setup timers: 60 FPS for smooth spectrum / lyrics sync, 1s for taskbar watchdog
    SetTimer(m_hWnd, 1, 16, nullptr);
    SetTimer(m_hWnd, 2, 1000, nullptr);

    RepaintBar();
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::RepaintBar() {
    if (!m_isVisible || !m_hWnd) return;

    RECT rcBar = m_dock.GetBarRect();
    POINT screenPos = m_dock.GetScreenPos();
    float scale = m_dock.GetDpiScale();

    m_renderer.Render(
        screenPos,
        rcBar,
        scale,
        m_currentTrack,
        m_currentLyrics,
        m_currentSpectrum,
        m_hoveredBtn,
        m_showLyrics,
        m_isHovered
    );
}

void App::OnTrackChanged(const MediaTrackInfo& /*track*/) {
    if (m_hWnd) {
        PostMessageW(m_hWnd, WM_APP_TRACK_CHANGED, 0, 0);
    }
}

void App::OnLyricsLoaded(const LyricDocument& /*doc*/) {
    if (m_hWnd) {
        PostMessageW(m_hWnd, WM_APP_LYRICS_LOADED, 0, 0);
    }
}

LRESULT App::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == m_msgTaskbarCreated) {
        // Explorer restarted; re-dock into new taskbar instance
        m_dock.Attach(hWnd);
        RepaintBar();
        return 0;
    }

    switch (msg) {
    case WM_TIMER: {
        if (wParam == 1) {
            // 60 FPS tick: update spectrum & lyric timeline
            if (m_currentTrack.isPlaying) {
                int64_t posMs = m_currentTrack.GetCurrentInterpolatedPositionMs();
                m_lyrics.UpdatePosition(posMs);
                m_currentLyrics = m_lyrics.GetCurrentLyrics();
            }

            m_currentSpectrum = m_audio.GetSpectrum();
            RepaintBar();
        } else if (wParam == 2) {
            // 1s watchdog timer: re-dock or update layout
            if (!IsWindow(m_dock.GetTaskbarHwnd())) {
                m_dock.Attach(hWnd);
            } else {
                m_dock.UpdateLayout();
            }
        }
        return 0;
    }

    case WM_SETTINGCHANGE: {
        // Windows theme (Dark / Light) or personalization changed
        m_renderer.UpdateTheme();
        RepaintBar();
        return 0;
    }

    case WM_APP_TRACK_CHANGED: {
        auto prevTitle = m_currentTrack.title;
        auto prevArtist = m_currentTrack.artist;
        m_currentTrack = m_mediaSession.GetCurrentTrack();

        // Update album art
        if (!m_currentTrack.thumbnailBytes.empty()) {
            m_renderer.UpdateAlbumArt(m_currentTrack.thumbnailBytes);
        }

        // Query lyrics if track changed
        if (m_currentTrack.title != prevTitle || m_currentTrack.artist != prevArtist) {
            LyricsSource src = static_cast<LyricsSource>(m_currentLyricsSource);
            m_lyrics.RequestLyrics(
                m_currentTrack.title,
                m_currentTrack.artist,
                m_currentTrack.durationMs,
                src
            );
        }

        std::wstring tip = L"MusicBar: " + (m_currentTrack.title.empty() ? L"未播放" : m_currentTrack.title);
        m_trayIcon.SetTooltip(tip);

        RepaintBar();
        return 0;
    }

    case WM_APP_LYRICS_LOADED: {
        m_currentLyrics = m_lyrics.GetCurrentLyrics();
        RepaintBar();
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        ButtonType btn = m_isHovered ? m_renderer.HitTestButtons(mx, my) : ButtonType::None;
        if (btn == ButtonType::Prev) {
            m_mediaSession.PreviousTrack();
        } else if (btn == ButtonType::PlayPause) {
            m_mediaSession.PlayPause();
        } else if (btn == ButtonType::Next) {
            m_mediaSession.NextTrack();
        } else {
            // Start dragging along the taskbar
            m_isDragging = true;
            SetCapture(hWnd);
            GetCursorPos(&m_dragStartPt);
            POINT cur = m_dock.GetRelativePos();
            m_dragStartOffsetX = cur.x;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (m_isDragging) {
            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - m_dragStartPt.x;
            m_dock.SetBarOffset(m_dragStartOffsetX + dx);
            RepaintBar();
        } else {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);

            bool needRepaint = false;
            if (!m_isHovered) {
                m_isHovered = true;
                needRepaint = true;
            }

            ButtonType btn = m_renderer.HitTestButtons(mx, my);
            if (btn != m_hoveredBtn) {
                m_hoveredBtn = btn;
                needRepaint = true;
            }

            if (needRepaint) {
                RepaintBar();
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        bool needRepaint = false;
        if (m_isHovered) {
            m_isHovered = false;
            needRepaint = true;
        }
        if (m_hoveredBtn != ButtonType::None) {
            m_hoveredBtn = ButtonType::None;
            needRepaint = true;
        }
        if (needRepaint) {
            RepaintBar();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_isDragging) {
            m_isDragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) {
            m_mediaSession.PreviousTrack();
        } else if (delta < 0) {
            m_mediaSession.NextTrack();
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        m_trayIcon.ShowContextMenu(hWnd, pt.x, pt.y, m_currentLyricsSource, m_showLyrics, m_isVisible);
        return 0;
    }

    case WM_APP_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            m_trayIcon.ShowContextMenu(hWnd, pt.x, pt.y, m_currentLyricsSource, m_showLyrics, m_isVisible);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            m_isVisible = !m_isVisible;
            ShowWindow(hWnd, m_isVisible ? SW_SHOW : SW_HIDE);
            if (m_isVisible) RepaintBar();
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT cmdId = LOWORD(wParam);
        switch (cmdId) {
        case IDM_SHOW_HIDE:
            m_isVisible = !m_isVisible;
            ShowWindow(hWnd, m_isVisible ? SW_SHOW : SW_HIDE);
            if (m_isVisible) RepaintBar();
            break;

        case IDM_TOGGLE_LYRICS:
            m_showLyrics = !m_showLyrics;
            RepaintBar();
            break;

        case IDM_SOURCE_AUTO:
        case IDM_SOURCE_NETEASE:
        case IDM_SOURCE_QQ:
        case IDM_SOURCE_KUGOU:
        case IDM_SOURCE_LRCLIB: {
            m_currentLyricsSource = cmdId - IDM_SOURCE_AUTO;
            LyricsSource src = static_cast<LyricsSource>(m_currentLyricsSource);
            m_lyrics.RequestLyrics(
                m_currentTrack.title,
                m_currentTrack.artist,
                m_currentTrack.durationMs,
                src
            );
            break;
        }

        case IDM_REDOCK:
            m_dock.SetBarOffset(-1); // Auto reset to default left-of-tray position
            RepaintBar();
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        m_trayIcon.Remove();
        m_audio.Stop();
        m_dock.Detach();
        PostQuitMessage(0);
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace MusicBar
