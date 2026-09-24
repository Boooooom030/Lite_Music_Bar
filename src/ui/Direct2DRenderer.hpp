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
#include "core/Types.hpp"

namespace MusicBar {

class Direct2DRenderer {
public:
    Direct2DRenderer();
    ~Direct2DRenderer();

    bool Initialize(HWND hWnd);
    void Resize(UINT width, UINT height);
    void Render(
        POINT screenPos,
        const RECT& rcBar,
        float dpiScale,
        const MediaTrackInfo& track,
        const LyricDocument& lyrics,
        const SpectrumData& spectrum,
        ButtonType hoveredBtn,
        bool showLyrics
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

    // Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextPrimaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pTextSecondaryBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pLyricHighlightBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pProgressFgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSpectrumBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBtnHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pBtnIconBrush;

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
