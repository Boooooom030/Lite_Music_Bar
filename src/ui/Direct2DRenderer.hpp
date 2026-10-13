#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <span>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <deque>
#include "core/Types.hpp"

namespace MusicBar {

// Draws the taskbar bar with Direct2D on a Direct3D 11 device and presents the
// frame through a DirectComposition swap chain, so DWM paces presentation and
// the surface is composited on the GPU. The swap chain supplies per-pixel alpha;
// the host has no GDI redirection bitmap, and TaskbarDock bounds mouse input
// with the window region.
class Direct2DRenderer {
public:
    Direct2DRenderer();
    ~Direct2DRenderer();

    bool Initialize(HWND hWnd);
    HRESULT Resize(UINT width, UINT height);
    HRESULT UpdateTheme();
    bool IsLightTheme() const { return m_isLightTheme; }
    HRESULT Render(
        POINT screenPos,
        const RECT& rcBar,
        float dpiScale,
        const MediaTrackInfo& track,
        const LyricDocument& lyrics,
        const SpectrumData& spectrum,
        ButtonType hoveredBtn,
        bool showLyrics,
        bool isMiddleHovered,
        bool isCoverHovered
    );

    ButtonType HitTestButtons(int mouseX, int mouseY) const;
    bool HitTestMiddle(int mouseX, int mouseY) const;
    bool HitTestCover(int mouseX, int mouseY) const;
    bool HitTestSpectrum(int mouseX, int mouseY) const;
    RECT GetSpectrumRect() const;
    RECT GetOutputDeviceRect() const;
    void TriggerSpectrumClick();
    bool HasPendingAnimation() const;
    void UpdateAlbumArt(const std::vector<uint8_t>& imageBytes);

private:
    // The development regression test reads the actual GPU lyric pixels.
    friend struct RendererTestAccess;
    HWND m_hWnd = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory1> m_pD2DFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory1> m_pDWriteFactory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_pWICFactory;

    // Direct3D 11 device, Direct2D device context, and the composition swap
    // chain that carries the finished frame to DWM.
    Microsoft::WRL::ComPtr<ID3D11Device> m_pD3DDevice;
    Microsoft::WRL::ComPtr<ID2D1Device> m_pD2DDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_pRenderTarget;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_pSwapChain;
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_pCompositionDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_pCompositionTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_pCompositionVisual;

    // Bar-sized layer holding the content. The HWND still spans the taskbar.
    // It is readable so the frosted hover surfaces can sample it.
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_pContentBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_pBackBufferBitmap;
    Microsoft::WRL::ComPtr<ID2D1Effect> m_pBlurEffect;
    Microsoft::WRL::ComPtr<ID2D1Layer> m_pBlurLayer;
    UINT m_width = 0;
    UINT m_height = 0;
    POINT m_visualOffset{};

    bool m_isLightTheme = false;

    // Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pHitTestBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextPrimaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextSecondaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pLyricHighlightBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressFgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSpectrumBrush;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> m_pSpectrumGradientBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBtnHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBtnIconBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pCoverPlaceholderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pFrostedBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pFrostedBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pFrostedHighlightBrush;

    // Text Formats
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pTitleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pArtistFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_pLyricFormat;
    float m_textDpiScale = 0.0f;

    // Decoded bitmaps belong to the current Direct2D render target.
    struct CachedAlbumArt {
        uint64_t hash = 0;
        size_t length = 0;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pAlbumArtBitmap;
    std::vector<uint8_t> m_cachedImageBytes;
    std::deque<CachedAlbumArt> m_albumArtCache;

    // Hit test regions
    D2D1_RECT_F m_rcBtnPrev{};
    D2D1_RECT_F m_rcBtnPlayPause{};
    D2D1_RECT_F m_rcBtnNext{};
    D2D1_RECT_F m_rcBtnOutputDevice{};
    D2D1_RECT_F m_rcMiddle{};
    D2D1_RECT_F m_rcCover{};
    D2D1_RECT_F m_rcSpectrum{};

    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_pCurrentLyricLayout;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_pUpcomingLyricLayout;
    std::wstring m_currentLyricText;
    std::wstring m_upcomingLyricText;
    std::wstring m_lyricTrackTitle;
    int m_visibleLyricIndex = -1;
    float m_lyricScrollX = 0.0f;
    float m_lyricScrollTargetX = 0.0f;
    float m_currentLyricWidth = 0.0f;
    float m_upcomingLyricWidth = 0.0f;

    // Smooth Animation states
    float m_hoverProgress = 0.0f; // 0.0 (resting) -> 1.0 (hovered)
    float m_coverHoverProgress = 0.0f;
    float m_btnHoverAlpha[4] = {}; // Output device, Prev, Next, Play/Pause
    std::chrono::steady_clock::time_point m_lastAnimTime{};
    std::chrono::steady_clock::time_point m_spectrumClickTime{};
    std::array<float, SpectrumData::BAND_COUNT> m_spectrumPeaks{};
    std::array<std::chrono::steady_clock::time_point,
        SpectrumData::BAND_COUNT> m_peakHoldUntil{};

    float m_baseBgAlpha = 0.80f;
    float m_baseBorderAlpha = 0.14f;
    float m_baseHighlightAlpha = 0.18f;
    float m_baseBtnHoverAlpha = 0.18f;
    float m_baseTextPrimaryAlpha = 0.95f;
    float m_baseTextSecondaryAlpha = 0.85f;
    float m_baseLyricAlpha = 0.95f;
    float m_baseBtnIconAlpha = 0.95f;

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateTextFormats(float dpiScale);
    HRESULT CreateDeviceResources();
    HRESULT CreatePresentResources();
    HRESULT AcquireBackBuffer();
    void DiscardDeviceResources();
    HRESULT Present();
    HRESULT UpdateVisualOffset(const RECT& bar);
    void BlurRegion(const D2D1_RECT_F& area, float strength, float radius);

    void DrawVectorPlay(const D2D1_RECT_F& rect);
    void DrawVectorPause(const D2D1_RECT_F& rect);
    void DrawVectorPrev(const D2D1_RECT_F& rect);
    void DrawVectorNext(const D2D1_RECT_F& rect);
    void DrawVectorStereo(const D2D1_RECT_F& rect);
    void DrawNoteIcon(const D2D1_RECT_F& rect);
};

} // namespace MusicBar
