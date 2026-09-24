#include "App.hpp"
#include <windowsx.h>

namespace MusicBar {

constexpr UINT WM_APP_TRACK_CHANGED = WM_APP + 102;
constexpr UINT WM_APP_LYRICS_LOADED = WM_APP + 103;

App::App() = default;

App::~App() {
    if (m_hSingleInstanceMutex) {
        CloseHandle(m_hSingleInstanceMutex);
        m_hSingleInstanceMutex = nullptr;
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

    // Create host window with Topmost popup styles
    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
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

    // Dock to taskbar to compute initial layout & position
    m_dock.Attach(m_hWnd);

    // Initialize Direct2D with docked geometry
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

    // Setup 60 FPS animation timer (16ms) and 1s health check timer
    SetTimer(m_hWnd, 1, 16, nullptr);
    SetTimer(m_hWnd, 2, 1000, nullptr);

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);

    return true;
}

int App::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
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
    if (msg == m_msgTaskbarCreated && m_msgTaskbarCreated != 0) {
        m_dock.Attach(hWnd);
        return 0;
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        if (m_isVisible) {
            RECT rcBar = m_dock.GetBarRect();
            m_renderer.Render(
                rcBar,
                m_dock.GetDpiScale(),
                m_currentTrack,
                m_currentLyrics,
                m_currentSpectrum,
                m_hoveredBtn,
                m_showLyrics
            );
        }
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent background flicker

    case WM_TIMER: {
        if (wParam == 1) {
            // 60 FPS tick: update spectrum & lyric position
            if (m_currentTrack.isPlaying) {
                int64_t posMs = m_currentTrack.GetCurrentInterpolatedPositionMs();
                m_lyrics.UpdatePosition(posMs);
                m_currentLyrics = m_lyrics.GetCurrentLyrics();
            }

            m_currentSpectrum = m_audio.GetSpectrum();

            RECT rcBar = m_dock.GetBarRect();
            InvalidateRect(hWnd, &rcBar, FALSE);
        } else if (wParam == 2) {
            // 1s timer: check taskbar health & layout
            if (!IsWindow(m_dock.GetTaskbarHwnd())) {
                m_dock.Attach(hWnd);
            } else {
                m_dock.UpdateLayout();
            }
        }
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

        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_LYRICS_LOADED: {
        m_currentLyrics = m_lyrics.GetCurrentLyrics();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        ButtonType btn = m_renderer.HitTestButtons(mx, my);
        if (btn == ButtonType::Prev) {
            m_mediaSession.PreviousTrack();
        } else if (btn == ButtonType::PlayPause) {
            m_mediaSession.PlayPause();
        } else if (btn == ButtonType::Next) {
            m_mediaSession.NextTrack();
        } else {
            // Start dragging along taskbar
            m_isDragging = true;
            SetCapture(hWnd);
            GetCursorPos(&m_dragStartPt);
            POINT cur = m_dock.GetScreenPos();
            m_dragStartOffsetX = cur.x;
            m_dragStartOffsetY = cur.y;
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
            int dy = pt.y - m_dragStartPt.y;
            m_dock.SetBarOffset(m_dragStartOffsetX + dx, m_dragStartOffsetY + dy);
        } else {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);

            ButtonType btn = m_renderer.HitTestButtons(mx, my);
            if (btn != m_hoveredBtn) {
                m_hoveredBtn = btn;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (m_hoveredBtn != ButtonType::None) {
            m_hoveredBtn = ButtonType::None;
            InvalidateRect(hWnd, nullptr, FALSE);
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
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT cmdId = LOWORD(wParam);
        switch (cmdId) {
        case IDM_SHOW_HIDE:
            m_isVisible = !m_isVisible;
            ShowWindow(hWnd, m_isVisible ? SW_SHOW : SW_HIDE);
            break;
        case IDM_TOGGLE_LYRICS:
            m_showLyrics = !m_showLyrics;
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        case IDM_REDOCK:
            m_dock.Detach();
            m_dock.Attach(hWnd);
            break;
        case IDM_SOURCE_AUTO:
            m_currentLyricsSource = 0;
            m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist, m_currentTrack.durationMs, LyricsSource::Auto);
            break;
        case IDM_SOURCE_NETEASE:
            m_currentLyricsSource = 1;
            m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist, m_currentTrack.durationMs, LyricsSource::NetEase);
            break;
        case IDM_SOURCE_QQ:
            m_currentLyricsSource = 2;
            m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist, m_currentTrack.durationMs, LyricsSource::QQMusic);
            break;
        case IDM_SOURCE_KUGOU:
            m_currentLyricsSource = 3;
            m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist, m_currentTrack.durationMs, LyricsSource::Kugou);
            break;
        case IDM_SOURCE_LRCLIB:
            m_currentLyricsSource = 4;
            m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist, m_currentTrack.durationMs, LyricsSource::Lrclib);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_DPICHANGED: {
        m_dock.UpdateLayout();
        RECT* prc = reinterpret_cast<RECT*>(lParam);
        if (prc) {
            m_renderer.Resize(prc->right - prc->left, prc->bottom - prc->top);
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        m_audio.Stop();
        m_mediaSession.Shutdown();
        m_dock.Detach();
        m_trayIcon.Remove();
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace MusicBar
