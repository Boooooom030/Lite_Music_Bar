#include "App.hpp"
#include "lyrics/LyricsSourceSelector.hpp"
#include "audio/OutputDeviceService.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <shellapi.h>
#include <vector>

namespace MusicBar {

constexpr UINT WM_APP_TRACK_CHANGED = WM_APP + 102;
constexpr UINT WM_APP_LYRICS_LOADED = WM_APP + 103;

namespace {
bool PassedDragThreshold(const POINT& start, const POINT& current) {
    return std::abs(current.x - start.x) >=
            std::max(1, GetSystemMetrics(SM_CXDRAG)) ||
        std::abs(current.y - start.y) >=
            std::max(1, GetSystemMetrics(SM_CYDRAG));
}
} // namespace

App::App() {
    s_pInstance = this;
}

LyricsSource App::EffectiveLyricsSource() const {
    const auto selected = static_cast<LyricsSource>(m_currentLyricsSource);
    return selected == LyricsSource::Auto
        ? PreferredLyricsSource(m_currentTrack.sourceAppId) : selected;
}

AppSettings App::CurrentSettings() const {
    AppSettings settings{};
    settings.showLyrics = m_showLyrics;
    settings.positionLocked = m_positionLocked;
    settings.lyricsSource = static_cast<LyricsSource>(m_currentLyricsSource);
    settings.fftCaptureMode = m_audio.GetCaptureMode() == AudioCaptureMode::SystemMix
        ? SpectrumCaptureMode::System : SpectrumCaptureMode::Player;
    return settings;
}

void App::ApplySettings(const AppSettings& settings) {
    const bool sourceChanged =
        m_currentLyricsSource != static_cast<int>(settings.lyricsSource);
    const AudioCaptureMode captureMode =
        settings.fftCaptureMode == SpectrumCaptureMode::System
            ? AudioCaptureMode::SystemMix : AudioCaptureMode::PlayerProcess;
    if (m_audio.GetCaptureMode() != captureMode) {
        m_audio.SetCaptureMode(captureMode);
        m_lastAudioActivity = {};
        m_currentSpectrum = {};
    }
    m_showLyrics = settings.showLyrics;
    m_currentLyricsSource = static_cast<int>(settings.lyricsSource);
    m_positionLocked = settings.positionLocked;
    if (m_positionLocked && m_isDragging) {
        m_pressTarget = PressTarget::None;
        m_isDragging = false;
        m_suppressClickOnRelease = false;
        if (GetCapture() == m_hWnd) ReleaseCapture();
    }
    if (sourceChanged && !m_currentTrack.title.empty()) {
        const LyricsSource source = EffectiveLyricsSource();
        m_lastRequestedLyricsSource = source;
        m_lyrics.RequestLyrics(m_currentTrack.title, m_currentTrack.artist,
            m_currentTrack.durationMs, source);
    }
    RepaintBar();
}

void App::PersistSettings() {
    if (SettingsStore::Save(CurrentSettings())) {
        m_settingsFileStamp = SettingsStore::GetFileStamp();
    } else if (m_hWnd) {
        MessageBoxW(m_hWnd, L"无法保存设置文件。", L"MusicBar",
            MB_OK | MB_ICONWARNING);
    }
}

void App::ReloadSettingsIfChanged() {
    const uint64_t stamp = SettingsStore::GetFileStamp();
    if (stamp == m_settingsFileStamp) return;
    m_settingsFileStamp = stamp;
    AppSettings settings{};
    if (stamp == 0 || SettingsStore::Load(settings)) {
        ApplySettings(settings);
    }
}

std::wstring App::SettingsExecutablePath() const {
    std::wstring modulePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) return {};
    modulePath.resize(length);
    return (std::filesystem::path(modulePath).parent_path() /
        L"MusicBar.Settings.exe").wstring();
}

void App::OpenSettings() {
    const std::wstring path = SettingsExecutablePath();
    if (path.empty() ||
        reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(),
            nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        MessageBoxW(m_hWnd, L"无法启动设置程序。请将 MusicBar.Settings.exe 放在 MusicBar.exe 旁边。",
            L"MusicBar", MB_OK | MB_ICONWARNING);
    }
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
    m_settingsFileStamp = SettingsStore::GetFileStamp();
    AppSettings savedSettings{};
    if (SettingsStore::Load(savedSettings)) {
        ApplySettings(savedSettings);
    }

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
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(101),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_SHARED));
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
        WS_POPUP | WS_CLIPSIBLINGS,
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
    if (!m_volumePopup.Initialize(hInstance)) return false;
    m_volumePopup.SetLightTheme(m_renderer.IsLightTheme());

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
        m_isHovered,
        m_isCoverHovered
    );
}

RECT App::GetSpectrumScreenRect() const {
    RECT spectrum = m_renderer.GetSpectrumRect();
    const POINT position = m_dock.GetScreenPos();
    OffsetRect(&spectrum, position.x, position.y);
    return spectrum;
}

void App::ShowOutputDeviceMenu() {
    std::vector<OutputDevice> devices;
    const HRESULT listResult = OutputDeviceService::List(devices);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    constexpr UINT firstDeviceCommand = 2000;
    if (FAILED(listResult) || devices.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
            FAILED(listResult) ? L"无法读取输出设备" : L"没有可用的输出设备");
    } else {
        for (size_t index = 0; index < devices.size(); ++index) {
            std::wstring label;
            for (wchar_t character : devices[index].name) {
                label += character;
                if (character == L'&') label += L'&';
            }
            AppendMenuW(menu, MF_STRING |
                (devices[index].isDefault ? MF_CHECKED : MF_UNCHECKED),
                firstDeviceCommand + static_cast<UINT>(index), label.c_str());
        }
    }

    RECT button = m_renderer.GetOutputDeviceRect();
    const POINT barPos = m_dock.GetScreenPos();
    OffsetRect(&button, barPos.x, barPos.y);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const bool haveMonitor = GetMonitorInfoW(
        MonitorFromRect(&button, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
    const bool openAbove = !haveMonitor ||
        button.top - monitor.rcMonitor.top >=
            monitor.rcMonitor.bottom - button.bottom;
    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN |
        (openAbove ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
    m_outputMenuOpen = true;
    m_isHovered = true;
    m_hoveredBtn = ButtonType::OutputDevice;
    RepaintBar();
    SetForegroundWindow(m_hWnd);
    const UINT command = TrackPopupMenuEx(menu, flags, button.left,
        openAbove ? button.top : button.bottom, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    m_outputMenuOpen = false;
    m_isHovered = false;
    m_hoveredBtn = ButtonType::None;
    RepaintBar();

    if (command >= firstDeviceCommand &&
        command - firstDeviceCommand < devices.size()) {
        const auto& chosen = devices[command - firstDeviceCommand];
        if (!chosen.isDefault &&
            FAILED(OutputDeviceService::SetDefault(chosen.id))) {
            MessageBoxW(m_hWnd,
                L"切换输出设备失败，请确认该设备仍然连接。",
                L"MusicBar", MB_OK | MB_ICONWARNING);
        }
    }
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
        m_volumePopup.Hide();
        m_dock.Attach(hWnd);
        RepaintBar();
        return 0;
    }

    switch (msg) {
    case WM_TIMER: {
        if (wParam == 1) {
            // 60 FPS tick: update spectrum & lyric timeline
            if (!m_currentTrack.title.empty()) {
                int64_t posMs = m_currentTrack.GetCurrentInterpolatedPositionMs();
                m_lyrics.UpdatePosition(posMs);
                m_currentLyrics = m_lyrics.GetCurrentLyrics();
            }

            m_currentSpectrum = m_audio.GetSpectrum();
            if (m_audio.GetCaptureMode() == AudioCaptureMode::PlayerProcess &&
                (m_currentTrack.title.empty() || !m_currentTrack.isPlaying)) {
                m_currentSpectrum = {};
            }
            const auto now = std::chrono::steady_clock::now();
            if (m_currentSpectrum.hasAudio) m_lastAudioActivity = now;
            m_currentSpectrum.hasAudio = m_lastAudioActivity.time_since_epoch().count() != 0 &&
                now - m_lastAudioActivity < std::chrono::milliseconds(700);
            const bool spectrumIsMoving = std::any_of(
                m_currentSpectrum.bands.begin(), m_currentSpectrum.bands.end(),
                [](float level) { return level > 0.01f; });
            if (m_currentTrack.isPlaying || m_currentSpectrum.hasAudio ||
                spectrumIsMoving || m_isHovered ||
                (m_isCoverHovered && !m_currentTrack.title.empty()) ||
                m_renderer.HasPendingAnimation()) {
                RepaintBar();
            }
        } else if (wParam == 2) {
            // 1s watchdog timer: re-dock or update layout
            ReloadSettingsIfChanged();
            if (!m_dock.IsDocked()) {
                m_dock.Attach(hWnd);
            } else {
                const int previousDpi = m_dock.GetDpi();
                const RECT previousBar = m_dock.GetBarRect();
                m_dock.UpdateLayout();
                const RECT currentBar = m_dock.GetBarRect();
                if (previousDpi != m_dock.GetDpi() ||
                    !EqualRect(&previousBar, &currentBar)) {
                    RepaintBar();
                }
            }
            if (m_volumePopup.IsVisible()) {
                m_volumePopup.Reposition(GetSpectrumScreenRect(), m_dock.GetDpi());
            }
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED: {
        // Windows theme (Dark / Light) or personalization changed
        m_renderer.UpdateTheme();
        m_volumePopup.SetLightTheme(m_renderer.IsLightTheme());
        RepaintBar();
        return 0;
    }

    case WM_APP_TRACK_CHANGED: {
        auto prevTitle = m_currentTrack.title;
        auto prevArtist = m_currentTrack.artist;
        auto prevSource = m_currentTrack.sourceAppId;
        const DWORD previousProcess = m_currentTrack.sourceProcessId;
        const bool wasPlaying = m_currentTrack.isPlaying;
        auto prevArt = m_currentTrack.thumbnailBytes;
        m_currentTrack = m_mediaSession.GetCurrentTrack();

        m_audio.SetTargetProcess(m_currentTrack.title.empty() ||
            !m_currentTrack.isPlaying ? 0 : m_currentTrack.sourceProcessId);
        if (previousProcess != m_currentTrack.sourceProcessId ||
            (wasPlaying && !m_currentTrack.isPlaying)) {
            m_lastAudioActivity = {};
            m_currentSpectrum = {};
        }

        if (m_currentTrack.thumbnailBytes != prevArt) {
            m_renderer.UpdateAlbumArt(m_currentTrack.thumbnailBytes);
        }

        // Query lyrics if track changed
        const LyricsSource src = EffectiveLyricsSource();
        if (m_currentTrack.title != prevTitle ||
            m_currentTrack.artist != prevArtist ||
            m_currentTrack.sourceAppId != prevSource ||
            src != m_lastRequestedLyricsSource) {
            m_lastRequestedLyricsSource = src;
            m_lyrics.RequestLyrics(
                m_currentTrack.title,
                m_currentTrack.artist,
                m_currentTrack.durationMs,
                src
            );
            m_currentLyrics = m_lyrics.GetCurrentLyrics();
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

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int mx = GET_X_LPARAM(lParam);
        const int my = GET_Y_LPARAM(lParam);
        const RECT bar = m_dock.GetBarRect();
        if (!PtInRect(&bar, POINT{mx, my})) return 0;

        const int localX = mx - bar.left;
        const int localY = my - bar.top;
        const ButtonType btn = m_renderer.HitTestButtons(localX, localY);
        m_pressTarget = PressTarget::Background;
        if (m_renderer.HitTestSpectrum(localX, localY)) {
            m_pressTarget = PressTarget::Spectrum;
        } else if (btn == ButtonType::OutputDevice) {
            m_pressTarget = PressTarget::OutputDevice;
        } else if (!m_currentTrack.title.empty() &&
            m_renderer.HitTestCover(localX, localY)) {
            m_pressTarget = PressTarget::Cover;
        } else if (btn == ButtonType::Prev) {
            m_pressTarget = PressTarget::Previous;
        } else if (btn == ButtonType::Next) {
            m_pressTarget = PressTarget::Next;
        }

        m_isDragging = false;
        m_suppressClickOnRelease = msg == WM_LBUTTONDBLCLK;
        if (!GetCursorPos(&m_dragStartPt)) {
            m_pressTarget = PressTarget::None;
            m_suppressClickOnRelease = false;
            return 0;
        }
        m_dragStartOffsetX = m_dock.GetRelativePos().x;
        SetCapture(hWnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (m_outputMenuOpen) return 0;
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (m_pressTarget != PressTarget::None && !(wParam & MK_LBUTTON)) {
            m_pressTarget = PressTarget::None;
            m_isDragging = false;
            m_suppressClickOnRelease = false;
            if (GetCapture() == hWnd) ReleaseCapture();
        }
        if (m_pressTarget != PressTarget::None) {
            POINT pt{};
            if (GetCursorPos(&pt)) {
                const int dx = pt.x - m_dragStartPt.x;
                if (!m_isDragging && PassedDragThreshold(m_dragStartPt, pt)) {
                    m_isDragging = true;
                    m_volumePopup.Hide();
                }
                if (m_isDragging && !m_positionLocked) {
                    m_dock.SetBarOffset(m_dragStartOffsetX + dx);
                }
            }
        }
        if (m_isDragging) {
            if (!m_positionLocked) RepaintBar();
        } else {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);

            RECT bar = m_dock.GetBarRect();
            bool overMiddle = m_renderer.HitTestMiddle(mx - bar.left, my - bar.top);
            bool overCover = m_renderer.HitTestCover(mx - bar.left, my - bar.top);
            bool needRepaint = false;
            if (m_isHovered != overMiddle) {
                m_isHovered = overMiddle;
                needRepaint = true;
            }
            if (m_isCoverHovered != overCover) {
                m_isCoverHovered = overCover;
                needRepaint = true;
            }

            ButtonType btn = m_renderer.HitTestButtons(mx - bar.left, my - bar.top);
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
        if (m_outputMenuOpen) return 0;
        bool needRepaint = false;
        if (m_isHovered) {
            m_isHovered = false;
            needRepaint = true;
        }
        if (m_isCoverHovered) {
            m_isCoverHovered = false;
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
        const PressTarget target = m_pressTarget;
        bool dragged = m_isDragging;
        const bool suppressClick = m_suppressClickOnRelease;
        POINT releasePoint{};
        if (target != PressTarget::None && GetCursorPos(&releasePoint) &&
            (dragged || PassedDragThreshold(m_dragStartPt, releasePoint))) {
            dragged = true;
            if (!m_positionLocked) {
                m_dock.SetBarOffset(m_dragStartOffsetX +
                    releasePoint.x - m_dragStartPt.x);
                RepaintBar();
            }
            m_volumePopup.Hide();
        }
        m_pressTarget = PressTarget::None;
        m_isDragging = false;
        m_suppressClickOnRelease = false;
        if (GetCapture() == hWnd) ReleaseCapture();
        if (target == PressTarget::None || dragged || suppressClick) return 0;

        const RECT bar = m_dock.GetBarRect();
        const int localX = GET_X_LPARAM(lParam) - bar.left;
        const int localY = GET_Y_LPARAM(lParam) - bar.top;
        if (target == PressTarget::Spectrum) {
            if (m_renderer.HitTestSpectrum(localX, localY)) {
                m_renderer.TriggerSpectrumClick();
                m_volumePopup.Toggle(GetSpectrumScreenRect(), m_dock.GetDpi());
                RepaintBar();
            }
            return 0;
        }

        m_volumePopup.Hide();
        if (target == PressTarget::Cover && !m_currentTrack.title.empty() &&
            m_renderer.HitTestCover(localX, localY)) {
            m_mediaSession.PlayPause();
        } else {
            const ButtonType released = m_renderer.HitTestButtons(localX, localY);
            if (target == PressTarget::OutputDevice &&
                released == ButtonType::OutputDevice) {
                ShowOutputDeviceMenu();
            } else if (target == PressTarget::Previous &&
                released == ButtonType::Prev) {
                m_mediaSession.PreviousTrack();
            } else if (target == PressTarget::Next &&
                released == ButtonType::Next) {
                m_mediaSession.NextTrack();
            }
        }
        return 0;
    }

    case WM_CANCELMODE:
        m_pressTarget = PressTarget::None;
        m_isDragging = false;
        m_suppressClickOnRelease = false;
        if (GetCapture() == hWnd) ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        m_pressTarget = PressTarget::None;
        m_isDragging = false;
        m_suppressClickOnRelease = false;
        return 0;

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
        const std::wstring settingsPath = SettingsExecutablePath();
        const DWORD settingsAttributes = settingsPath.empty()
            ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(settingsPath.c_str());
        const bool settingsAvailable = !settingsPath.empty() &&
            settingsAttributes != INVALID_FILE_ATTRIBUTES &&
            !(settingsAttributes & FILE_ATTRIBUTE_DIRECTORY);
        m_trayIcon.ShowContextMenu(hWnd, pt.x, pt.y, m_currentLyricsSource,
            m_showLyrics, m_isVisible, m_positionLocked, settingsAvailable);
        return 0;
    }

    case WM_APP_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            const std::wstring settingsPath = SettingsExecutablePath();
            const DWORD settingsAttributes = settingsPath.empty()
                ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(settingsPath.c_str());
            const bool settingsAvailable = !settingsPath.empty() &&
                settingsAttributes != INVALID_FILE_ATTRIBUTES &&
                !(settingsAttributes & FILE_ATTRIBUTE_DIRECTORY);
            m_trayIcon.ShowContextMenu(hWnd, pt.x, pt.y, m_currentLyricsSource,
                m_showLyrics, m_isVisible, m_positionLocked, settingsAvailable);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            m_isVisible = !m_isVisible;
            if (!m_isVisible) m_volumePopup.Hide();
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
            if (!m_isVisible) m_volumePopup.Hide();
            ShowWindow(hWnd, m_isVisible ? SW_SHOW : SW_HIDE);
            if (m_isVisible) RepaintBar();
            break;

        case IDM_TOGGLE_LYRICS:
            m_showLyrics = !m_showLyrics;
            PersistSettings();
            RepaintBar();
            break;

        case IDM_LOCK_POSITION:
            m_positionLocked = !m_positionLocked;
            m_pressTarget = PressTarget::None;
            m_isDragging = false;
            m_suppressClickOnRelease = false;
            if (GetCapture() == hWnd) ReleaseCapture();
            PersistSettings();
            break;

        case IDM_SOURCE_AUTO:
        case IDM_SOURCE_NETEASE:
        case IDM_SOURCE_QQ:
        case IDM_SOURCE_KUGOU:
        case IDM_SOURCE_SODA:
        case IDM_SOURCE_LRCLIB: {
            LyricsSource selected = LyricsSource::Auto;
            if (cmdId == IDM_SOURCE_NETEASE) selected = LyricsSource::NetEase;
            else if (cmdId == IDM_SOURCE_QQ) selected = LyricsSource::QQMusic;
            else if (cmdId == IDM_SOURCE_KUGOU) selected = LyricsSource::Kugou;
            else if (cmdId == IDM_SOURCE_SODA) selected = LyricsSource::Soda;
            else if (cmdId == IDM_SOURCE_LRCLIB) selected = LyricsSource::Lrclib;
            m_currentLyricsSource = static_cast<int>(selected);
            PersistSettings();
            const LyricsSource src = EffectiveLyricsSource();
            m_lastRequestedLyricsSource = src;
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

        case IDM_SETTINGS:
            OpenSettings();
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_DESTROY: {
        m_volumePopup.Hide();
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        m_trayIcon.Remove();
        m_mediaSession.Shutdown();
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
