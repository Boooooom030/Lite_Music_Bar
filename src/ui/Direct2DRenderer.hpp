#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <span>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include "core/Types.hpp"

namespace MusicBar {

class Direct2DRenderer {
public:
    Direct2DRenderer();
    ~Direct2DRenderer();

    bool Initialize(HWND hWnd);
    void Resize(UINT width, UINT height);
    void UpdateTheme();
    void Render(
        POINT screenPos,
        const RECT& rcBar,
        float dpiScale,
        const MediaTrackInfo& track,
        const LyricDocument& lyrics,
        const SpectrumData& spectrum,
        ButtonType hoveredBtn,
        bool showLyrics,
        bool isHovered
    );

    ButtonType HitTestButtons(int mouseX, int mouseY) const;
    void UpdateAlbumArt(const std::vector<uint8_t>& imageBytes);

private:
    HWND m_hWnd = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory1> m_pD2DFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory1> m_pDWriteFactory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_pWICFactory;

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_pRenderTarget;

    // 32-bit DIB for UpdateLayeredWindow
    HDC m_hMemDC = nullptr;
    HBITMAP m_hDIB = nullptr;
    void* m_pDIBBits = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;

    bool m_isLightTheme = false;

    // Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pHitTestBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextPrimaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextSecondaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pLyricHighlightBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressFgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSpectrumBrush;
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

    // Album art cache
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_pAlbumArtBitmap;
    std::vector<uint8_t> m_cachedImageBytes;

    // Hit test regions
    D2D1_RECT_F m_rcBtnPrev{};
    D2D1_RECT_F m_rcBtnPlayPause{};
    D2D1_RECT_F m_rcBtnNext{};

    // Smooth Animation states
    float m_hoverProgress = 0.0f; // 0.0 (resting) -> 1.0 (hovered)
    float m_btnHoverAlpha[3] = { 0.0f, 0.0f, 0.0f }; // Prev, PlayPause, Next
    std::chrono::steady_clock::time_point m_lastAnimTime{};

    float m_baseBgAlpha = 0.80f;
    float m_baseBorderAlpha = 0.14f;
    float m_baseHighlightAlpha = 0.18f;
    float m_baseBtnHoverAlpha = 0.18f;
    float m_baseTextPrimaryAlpha = 0.95f;
    float m_baseTextSecondaryAlpha = 0.85f;
    float m_baseLyricAlpha = 0.95f;
    float m_baseBtnIconAlpha = 0.95f;

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    void DrawVectorPlay(const D2D1_RECT_F& rect);
    void DrawVectorPause(const D2D1_RECT_F& rect);
    void DrawVectorPrev(const D2D1_RECT_F& rect);
    void DrawVectorNext(const D2D1_RECT_F& rect);
    void DrawNoteIcon(const D2D1_RECT_F& rect);
};

} // namespace MusicBar
