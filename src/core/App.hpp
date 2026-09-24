#pragma once

#include <windows.h>
#include <memory>
#include "Types.hpp"
#include "shell/TaskbarDock.hpp"
#include "shell/TrayIcon.hpp"
#include "media/MediaSession.hpp"
#include "audio/WasapiLoopback.hpp"
#include "lyrics/LyricsService.hpp"
#include "ui/Direct2DRenderer.hpp"

namespace MusicBar {

class App {
public:
    App();
    ~App();

    bool Initialize(HINSTANCE hInstance);
    int Run();

private:
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

    static inline App* s_pInstance = nullptr;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    HANDLE m_hSingleInstanceMutex = nullptr;
    HWINEVENTHOOK m_hTaskbarHook = nullptr;
    UINT m_msgTaskbarCreated = 0;

    TaskbarDock m_dock;
    Direct2DRenderer m_renderer;
    MediaSession m_mediaSession;
    WasapiLoopback m_audio;
    LyricsService m_lyrics;
    TrayIcon m_trayIcon;

    MediaTrackInfo m_currentTrack;
    LyricDocument m_currentLyrics;
    SpectrumData m_currentSpectrum;

    ButtonType m_hoveredBtn = ButtonType::None;
    bool m_isHovered = false;
    bool m_isDragging = false;
    POINT m_dragStartPt{};
    int m_dragStartOffsetX = 0;

    bool m_isVisible = true;
    bool m_showLyrics = true;
    int m_currentLyricsSource = 0;

    void OnTrackChanged(const MediaTrackInfo& track);
    void OnLyricsLoaded(const LyricDocument& doc);
};

} // namespace MusicBar
