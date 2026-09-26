#pragma once

#include <windows.h>
#include <memory>
#include "Types.hpp"
#include "SettingsStore.hpp"
#include "shell/TaskbarDock.hpp"
#include "shell/TrayIcon.hpp"
#include "media/MediaSession.hpp"
#include "audio/WasapiLoopback.hpp"
#include "lyrics/LyricsService.hpp"
#include "ui/Direct2DRenderer.hpp"
#include "ui/VolumePopup.hpp"

namespace MusicBar {

class App {
public:
    App();
    ~App();

    bool Initialize(HINSTANCE hInstance);
    int Run();

private:
    enum class PressTarget {
        None,
        Background,
        Cover,
        OutputDevice,
        Previous,
        Next,
        Spectrum
    };

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK StaticWinEventProc(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime
    );

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RepaintBar();
    RECT GetSpectrumScreenRect() const;
    void ShowOutputDeviceMenu();
    AppSettings CurrentSettings() const;
    void ApplySettings(const AppSettings& settings);
    void PersistSettings();
    void ReloadSettingsIfChanged();
    std::wstring SettingsExecutablePath() const;
    void OpenSettings();

    static inline App* s_pInstance = nullptr;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    HANDLE m_hSingleInstanceMutex = nullptr;
    HWINEVENTHOOK m_hTaskbarHook = nullptr;
    UINT m_msgTaskbarCreated = 0;

    TaskbarDock m_dock;
    Direct2DRenderer m_renderer;
    VolumePopup m_volumePopup;
    MediaSession m_mediaSession;
    WasapiLoopback m_audio;
    LyricsService m_lyrics;
    TrayIcon m_trayIcon;

    MediaTrackInfo m_currentTrack;
    LyricDocument m_currentLyrics;
    SpectrumData m_currentSpectrum;
    std::chrono::steady_clock::time_point m_lastAudioActivity{};

    ButtonType m_hoveredBtn = ButtonType::None;
    bool m_isHovered = false;
    bool m_isCoverHovered = false;
    bool m_isDragging = false;
    PressTarget m_pressTarget = PressTarget::None;
    bool m_suppressClickOnRelease = false;
    bool m_outputMenuOpen = false;
    POINT m_dragStartPt{};
    int m_dragStartOffsetX = 0;

    bool m_isVisible = true;
    bool m_positionLocked = false;
    bool m_showLyrics = true;
    int m_currentLyricsSource = 0;
    LyricsSource m_lastRequestedLyricsSource = LyricsSource::Auto;
    uint64_t m_settingsFileStamp = 0;

    LyricsSource EffectiveLyricsSource() const;

    void OnTrackChanged(const MediaTrackInfo& track);
    void OnLyricsLoaded(const LyricDocument& doc);
};

} // namespace MusicBar
