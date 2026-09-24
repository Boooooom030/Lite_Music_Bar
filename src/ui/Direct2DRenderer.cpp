#include "Direct2DRenderer.hpp"
#include "ImageLoader.hpp"
#include <d2d1helper.h>
#include <algorithm>

namespace MusicBar {

namespace {

bool DetectSystemLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size
    );
    return (status == ERROR_SUCCESS && value != 0);
}

} // namespace

Direct2DRenderer::Direct2DRenderer() = default;

Direct2DRenderer::~Direct2DRenderer() {
    DiscardDeviceResources();
}

bool Direct2DRenderer::Initialize(HWND hWnd) {
    m_hWnd = hWnd;

    if (FAILED(CreateDeviceIndependentResources())) {
        return false;
    }

    if (!ImageLoader::Initialize(m_pWICFactory)) {
        return false;
    }

    return SUCCEEDED(CreateDeviceResources());
}

HRESULT Direct2DRenderer::CreateDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        reinterpret_cast<void**>(m_pD2DFactory.GetAddressOf())
    );
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory1),
        reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return hr;

    // Create Text Formats (Segoe UI Variable Text / Microsoft YaHei UI)
    hr = m_pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"",
        &m_pTitleFormat
    );
    if (FAILED(hr)) {
        hr = m_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.5f,
            L"",
            &m_pTitleFormat
        );
        if (FAILED(hr)) return hr;
    }
    m_pTitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_pTitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    hr = m_pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.0f,
        L"",
        &m_pArtistFormat
    );
    if (FAILED(hr)) {
        m_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            10.0f,
            L"",
            &m_pArtistFormat
        );
    }
    m_pArtistFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_pArtistFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    hr = m_pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.0f,
        L"",
        &m_pLyricFormat
    );
    if (FAILED(hr)) {
        m_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.0f,
            L"",
            &m_pLyricFormat
        );
    }
    m_pLyricFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_pLyricFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Create Trimming options (ellipsis)
    Microsoft::WRL::ComPtr<IDWriteInlineObject> pEllipsisSign;
    m_pDWriteFactory->CreateEllipsisTrimmingSign(m_pTitleFormat.Get(), &pEllipsisSign);
    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_pTitleFormat->SetTrimming(&trimming, pEllipsisSign.Get());
    m_pArtistFormat->SetTrimming(&trimming, pEllipsisSign.Get());
    m_pLyricFormat->SetTrimming(&trimming, pEllipsisSign.Get());

    return S_OK;
}

void Direct2DRenderer::UpdateTheme() {
    m_isLightTheme = DetectSystemLightTheme();

    if (!m_pRenderTarget) return;

    m_baseTextPrimaryAlpha = 0.95f;
    m_baseTextSecondaryAlpha = m_isLightTheme ? 0.88f : 0.85f;
    m_baseLyricAlpha = 0.95f;
    m_baseBtnIconAlpha = 0.95f;
    m_baseBtnHoverAlpha = m_isLightTheme ? 0.12f : 0.18f;
    m_baseBgAlpha = m_isLightTheme ? 0.85f : 0.80f;
    m_baseBorderAlpha = m_isLightTheme ? 0.10f : 0.14f;
    m_baseHighlightAlpha = m_isLightTheme ? 0.45f : 0.18f;

    D2D1_COLOR_F colorTextPrimary = m_isLightTheme
        ? D2D1::ColorF(0.10f, 0.10f, 0.10f, m_baseTextPrimaryAlpha)
        : D2D1::ColorF(0.98f, 0.98f, 0.98f, m_baseTextPrimaryAlpha);

    D2D1_COLOR_F colorTextSecondary = m_isLightTheme
        ? D2D1::ColorF(0.38f, 0.38f, 0.38f, m_baseTextSecondaryAlpha)
        : D2D1::ColorF(0.70f, 0.70f, 0.70f, m_baseTextSecondaryAlpha);

    D2D1_COLOR_F colorLyric = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.48f, 0.85f, m_baseLyricAlpha)
        : D2D1::ColorF(0.24f, 0.88f, 0.60f, m_baseLyricAlpha);

    D2D1_COLOR_F colorIcon = m_isLightTheme
        ? D2D1::ColorF(0.12f, 0.12f, 0.12f, m_baseBtnIconAlpha)
        : D2D1::ColorF(0.95f, 0.95f, 0.95f, m_baseBtnIconAlpha);

    D2D1_COLOR_F colorBtnHover = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, m_baseBtnHoverAlpha)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, m_baseBtnHoverAlpha);

    D2D1_COLOR_F colorProgressBg = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f);

    D2D1_COLOR_F colorProgressFg = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.95f)
        : D2D1::ColorF(0.24f, 0.88f, 0.60f, 0.95f);

    D2D1_COLOR_F colorSpectrum = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.90f)
        : D2D1::ColorF(0.30f, 0.76f, 1.0f, 0.90f);

    D2D1_COLOR_F colorCoverPlaceholder = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);

    D2D1_COLOR_F colorFrostedBg = m_isLightTheme
        ? D2D1::ColorF(0.95f, 0.95f, 0.97f, m_baseBgAlpha)
        : D2D1::ColorF(0.12f, 0.12f, 0.14f, m_baseBgAlpha);

    D2D1_COLOR_F colorFrostedBorder = m_isLightTheme
        ? D2D1::ColorF(0.0f, 0.0f, 0.0f, m_baseBorderAlpha)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, m_baseBorderAlpha);

    D2D1_COLOR_F colorFrostedHighlight = m_isLightTheme
        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, m_baseHighlightAlpha)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, m_baseHighlightAlpha);

    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.005f), &pHitTestBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorTextPrimary, &m_pTextPrimaryBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorTextSecondary, &m_pTextSecondaryBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorLyric, &m_pLyricHighlightBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorProgressBg, &m_pProgressBgBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorProgressFg, &m_pProgressFgBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorSpectrum, &m_pSpectrumBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorBtnHover, &m_pBtnHoverBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorIcon, &m_pBtnIconBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorCoverPlaceholder, &m_pCoverPlaceholderBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorFrostedBg, &m_pFrostedBgBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorFrostedBorder, &m_pFrostedBorderBrush);
    m_pRenderTarget->CreateSolidColorBrush(colorFrostedHighlight, &m_pFrostedHighlightBrush);
}

HRESULT Direct2DRenderer::CreateDeviceResources() {
    if (m_pRenderTarget) return S_OK;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    HRESULT hr = m_pD2DFactory->CreateDCRenderTarget(
        &rtProps,
        &m_pRenderTarget
    );
    if (FAILED(hr)) return hr;

    UpdateTheme();

    // Reload cached album art if present
    if (!m_cachedImageBytes.empty()) {
        m_pAlbumArtBitmap = ImageLoader::CreateBitmapFromMemory(
            m_pRenderTarget.Get(),
            m_pWICFactory.Get(),
            m_cachedImageBytes
        );
    }

    return S_OK;
}

void Direct2DRenderer::DiscardDeviceResources() {
    m_pAlbumArtBitmap.Reset();
    pHitTestBrush.Reset();
    m_pTextPrimaryBrush.Reset();
    m_pTextSecondaryBrush.Reset();
    m_pLyricHighlightBrush.Reset();
    m_pProgressBgBrush.Reset();
    m_pProgressFgBrush.Reset();
    m_pSpectrumBrush.Reset();
    m_pBtnHoverBrush.Reset();
    m_pBtnIconBrush.Reset();
    m_pCoverPlaceholderBrush.Reset();
    m_pFrostedBgBrush.Reset();
    m_pFrostedBorderBrush.Reset();
    m_pFrostedHighlightBrush.Reset();
    m_pRenderTarget.Reset();

    if (m_hDIB) {
        DeleteObject(m_hDIB);
        m_hDIB = nullptr;
    }
    if (m_hMemDC) {
        DeleteDC(m_hMemDC);
        m_hMemDC = nullptr;
    }
    m_pDIBBits = nullptr;
    m_width = 0;
    m_height = 0;
}

void Direct2DRenderer::Resize(UINT width, UINT height) {
    UINT w = std::max(1U, width);
    UINT h = std::max(1U, height);
    if (w == m_width && h == m_height && m_hDIB != nullptr) {
        return;
    }

    if (m_hDIB) {
        DeleteObject(m_hDIB);
        m_hDIB = nullptr;
    }
    if (m_hMemDC) {
        DeleteDC(m_hMemDC);
        m_hMemDC = nullptr;
    }

    m_width = w;
    m_height = h;

    HDC hdcScreen = GetDC(nullptr);
    m_hMemDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(m_width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(m_height); // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_hDIB = CreateDIBSection(m_hMemDC, &bmi, DIB_RGB_COLORS, &m_pDIBBits, nullptr, 0);
    SelectObject(m_hMemDC, m_hDIB);
    ReleaseDC(nullptr, hdcScreen);
}

void Direct2DRenderer::UpdateAlbumArt(const std::vector<uint8_t>& imageBytes) {
    m_cachedImageBytes = imageBytes;
    if (m_pRenderTarget && m_pWICFactory) {
        m_pAlbumArtBitmap = ImageLoader::CreateBitmapFromMemory(
            m_pRenderTarget.Get(),
            m_pWICFactory.Get(),
            m_cachedImageBytes
        );
    }
}

ButtonType Direct2DRenderer::HitTestButtons(int mouseX, int mouseY) const {
    if (m_hoverProgress < 0.4f || m_rcBtnPrev.right <= m_rcBtnPrev.left) {
        return ButtonType::None;
    }

    float mx = static_cast<float>(mouseX);
    float my = static_cast<float>(mouseY);

    if (mx >= m_rcBtnPrev.left && mx <= m_rcBtnPrev.right &&
        my >= m_rcBtnPrev.top && my <= m_rcBtnPrev.bottom) {
        return ButtonType::Prev;
    }
    if (mx >= m_rcBtnPlayPause.left && mx <= m_rcBtnPlayPause.right &&
        my >= m_rcBtnPlayPause.top && my <= m_rcBtnPlayPause.bottom) {
        return ButtonType::PlayPause;
    }
    if (mx >= m_rcBtnNext.left && mx <= m_rcBtnNext.right &&
        my >= m_rcBtnNext.top && my <= m_rcBtnNext.bottom) {
        return ButtonType::Next;
    }
    return ButtonType::None;
}

void Direct2DRenderer::DrawVectorPlay(const D2D1_RECT_F& rect) {
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float r = (rect.right - rect.left) * 0.28f;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (SUCCEEDED(m_pD2DFactory->CreatePathGeometry(&path))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - r * 0.7f, cy - r), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx + r * 1.1f, cy));
            sink->AddLine(D2D1::Point2F(cx - r * 0.7f, cy + r));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            m_pRenderTarget->FillGeometry(path.Get(), m_pBtnIconBrush.Get());
        }
    }
}

void Direct2DRenderer::DrawVectorPause(const D2D1_RECT_F& rect) {
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float w = rect.right - rect.left;
    float barW = w * 0.11f;
    float barH = w * 0.44f;
    float gap = w * 0.10f;

    D2D1_RECT_F bar1 = D2D1::RectF(cx - gap - barW, cy - barH * 0.5f, cx - gap, cy + barH * 0.5f);
    D2D1_RECT_F bar2 = D2D1::RectF(cx + gap, cy - barH * 0.5f, cx + gap + barW, cy + barH * 0.5f);
    m_pRenderTarget->FillRectangle(bar1, m_pBtnIconBrush.Get());
    m_pRenderTarget->FillRectangle(bar2, m_pBtnIconBrush.Get());
}

void Direct2DRenderer::DrawVectorPrev(const D2D1_RECT_F& rect) {
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float r = (rect.right - rect.left) * 0.24f;

    // Line + left triangle
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(cx - r * 1.1f, cy - r * 0.8f, cx - r * 0.8f, cy + r * 0.8f),
        m_pBtnIconBrush.Get()
    );

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (SUCCEEDED(m_pD2DFactory->CreatePathGeometry(&path))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx + r * 0.9f, cy - r * 0.8f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx - r * 0.5f, cy));
            sink->AddLine(D2D1::Point2F(cx + r * 0.9f, cy + r * 0.8f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            m_pRenderTarget->FillGeometry(path.Get(), m_pBtnIconBrush.Get());
        }
    }
}

void Direct2DRenderer::DrawVectorNext(const D2D1_RECT_F& rect) {
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float r = (rect.right - rect.left) * 0.24f;

    // Right triangle + line
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(cx + r * 0.8f, cy - r * 0.8f, cx + r * 1.1f, cy + r * 0.8f),
        m_pBtnIconBrush.Get()
    );

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    if (SUCCEEDED(m_pD2DFactory->CreatePathGeometry(&path))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - r * 0.9f, cy - r * 0.8f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx + r * 0.5f, cy));
            sink->AddLine(D2D1::Point2F(cx - r * 0.9f, cy + r * 0.8f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            m_pRenderTarget->FillGeometry(path.Get(), m_pBtnIconBrush.Get());
        }
    }
}

void Direct2DRenderer::DrawNoteIcon(const D2D1_RECT_F& rect) {
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float noteR = (rect.right - rect.left) * 0.16f;

    // Stylized dual music notes
    m_pRenderTarget->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(cx - noteR * 1.0f, cy + noteR * 0.8f), noteR * 0.8f, noteR * 0.6f),
        m_pTextSecondaryBrush.Get()
    );
    m_pRenderTarget->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(cx + noteR * 0.8f, cy + noteR * 0.4f), noteR * 0.8f, noteR * 0.6f),
        m_pTextSecondaryBrush.Get()
    );
    // Vertical stems
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(cx - noteR * 0.4f, cy - noteR * 1.2f, cx - noteR * 0.2f, cy + noteR * 0.8f),
        m_pTextSecondaryBrush.Get()
    );
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(cx + noteR * 1.4f, cy - noteR * 1.6f, cx + noteR * 1.6f, cy + noteR * 0.4f),
        m_pTextSecondaryBrush.Get()
    );
    // Top beam connecting stems
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(cx - noteR * 0.4f, cy - noteR * 1.6f, cx + noteR * 1.6f, cy - noteR * 1.1f),
        m_pTextSecondaryBrush.Get()
    );
}

void Direct2DRenderer::Render(
    POINT screenPos,
    const RECT& rcBar,
    float dpiScale,
    const MediaTrackInfo& track,
    const LyricDocument& lyrics,
    const SpectrumData& spectrum,
    ButtonType hoveredBtn,
    bool showLyrics,
    bool isHovered
) {
    (void)screenPos;
    if (FAILED(CreateDeviceResources())) return;

    UINT barWidth = static_cast<UINT>(std::max(1L, rcBar.right - rcBar.left));
    UINT barHeight = static_cast<UINT>(std::max(1L, rcBar.bottom - rcBar.top));

    Resize(barWidth, barHeight);

    RECT rcClient{ 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    if (FAILED(m_pRenderTarget->BindDC(m_hMemDC, &rcClient))) {
        return;
    }

    m_pRenderTarget->BeginDraw();

    // 0. Smooth Animation Engine (Gentle 380ms hover transition + 180ms button glow)
    auto now = std::chrono::steady_clock::now();
    if (m_lastAnimTime.time_since_epoch().count() == 0) {
        m_lastAnimTime = now;
    }
    float dt = std::chrono::duration<float>(now - m_lastAnimTime).count();
    m_lastAnimTime = now;
    if (dt > 0.05f) dt = 0.016f;

    float targetHover = isHovered ? 1.0f : 0.0f;
    float change = dt / 0.38f; // ~380ms slow and gentle transition
    if (m_hoverProgress < targetHover) {
        m_hoverProgress = std::min(targetHover, m_hoverProgress + change);
    } else if (m_hoverProgress > targetHover) {
        m_hoverProgress = std::max(targetHover, m_hoverProgress - change);
    }

    // Smoothstep easing: f(t) = t * t * (3 - 2t)
    float t = std::clamp(m_hoverProgress, 0.0f, 1.0f);
    float eased = t * t * (3.0f - 2.0f * t);

    // Individual button hover alpha animations (~0.18s glow)
    auto updateBtnHover = [&](int idx, ButtonType type) {
        float target = (hoveredBtn == type && isHovered) ? 1.0f : 0.0f;
        float btnChange = dt / 0.18f;
        if (m_btnHoverAlpha[idx] < target) {
            m_btnHoverAlpha[idx] = std::min(target, m_btnHoverAlpha[idx] + btnChange);
        } else if (m_btnHoverAlpha[idx] > target) {
            m_btnHoverAlpha[idx] = std::max(target, m_btnHoverAlpha[idx] - btnChange);
        }
    };
    updateBtnHover(0, ButtonType::Prev);
    updateBtnHover(1, ButtonType::PlayPause);
    updateBtnHover(2, ButtonType::Next);

    // 1. Transparent canvas: No dark card, no border. Taskbar shows through!
    m_pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // Invisible hit-test area (alpha=1/255) ensures entire bar area handles drag/right-click
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)),
        pHitTestBrush.Get()
    );

    D2D1_RECT_F barRect = D2D1::RectF(
        0.0f,
        0.0f,
        static_cast<float>(m_width),
        static_cast<float>(m_height)
    );

    float fBarHeight = barRect.bottom - barRect.top;
    float padding = 4.0f * dpiScale;

    // 2. Frosted background card (Gently fades and blooms in with animation)
    if (eased > 0.002f && m_pFrostedBgBrush && m_pFrostedBorderBrush) {
        m_pFrostedBgBrush->SetOpacity(m_baseBgAlpha * eased);
        m_pFrostedBorderBrush->SetOpacity(m_baseBorderAlpha * eased);

        float inset = (2.0f - 1.0f * eased) * dpiScale;
        D2D1_RECT_F bgRect = D2D1::RectF(
            inset,
            inset,
            static_cast<float>(m_width) - inset,
            static_cast<float>(m_height) - inset
        );
        float radius = 7.0f * dpiScale;
        D2D1_ROUNDED_RECT roundedBg = D2D1::RoundedRect(bgRect, radius, radius);

        m_pRenderTarget->FillRoundedRectangle(roundedBg, m_pFrostedBgBrush.Get());
        m_pRenderTarget->DrawRoundedRectangle(roundedBg, m_pFrostedBorderBrush.Get(), 1.0f * dpiScale);

        if (m_pFrostedHighlightBrush) {
            m_pFrostedHighlightBrush->SetOpacity(m_baseHighlightAlpha * eased);
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(bgRect.left + radius, bgRect.top + 1.0f * dpiScale),
                D2D1::Point2F(bgRect.right - radius, bgRect.top + 1.0f * dpiScale),
                m_pFrostedHighlightBrush.Get(),
                1.0f * dpiScale
            );
        }
    }

    // 3. Draw Album Art (Left side: 28x28 with 4px rounded corners)
    float artSize = fBarHeight - 2.0f * padding;
    D2D1_RECT_F artRect = D2D1::RectF(
        barRect.left + padding + 2.0f * dpiScale,
        barRect.top + padding,
        barRect.left + padding + 2.0f * dpiScale + artSize,
        barRect.top + padding + artSize
    );

    D2D1_ROUNDED_RECT artRounded = D2D1::RoundedRect(artRect, 4.0f * dpiScale, 4.0f * dpiScale);

    if (m_pAlbumArtBitmap) {
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> artGeom;
        if (SUCCEEDED(m_pD2DFactory->CreateRoundedRectangleGeometry(artRounded, &artGeom))) {
            Microsoft::WRL::ComPtr<ID2D1Layer> layer;
            if (SUCCEEDED(m_pRenderTarget->CreateLayer(nullptr, &layer))) {
                m_pRenderTarget->PushLayer(
                    D2D1::LayerParameters(D2D1::InfiniteRect(), artGeom.Get()),
                    layer.Get()
                );
                m_pRenderTarget->DrawBitmap(
                    m_pAlbumArtBitmap.Get(),
                    artRect,
                    1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
                );
                m_pRenderTarget->PopLayer();
            }
        }
    } else {
        m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha);
        m_pRenderTarget->FillRoundedRectangle(artRounded, m_pCoverPlaceholderBrush.Get());
        DrawNoteIcon(artRect);
    }

    // 4. Spectrum Bars (Right side: 16 bands)
    float spectrumWidth = 36.0f * dpiScale;
    float spectrumHeight = fBarHeight - 14.0f * dpiScale;
    float spectrumBottom = barRect.bottom - 7.0f * dpiScale;
    float spectrumLeft = barRect.right - padding - spectrumWidth - 4.0f * dpiScale;

    size_t numBands = SpectrumData::BAND_COUNT;
    float barGap = 1.0f * dpiScale;
    float singleBarW = (spectrumWidth - (numBands - 1) * barGap) / numBands;

    m_pSpectrumBrush->SetOpacity(0.90f);
    for (size_t i = 0; i < numBands; ++i) {
        float val = std::clamp(spectrum.bands[i], 0.05f, 1.0f);
        float h = val * spectrumHeight;
        float bx = spectrumLeft + i * (singleBarW + barGap);
        float by = spectrumBottom - h;

        D2D1_RECT_F barR = D2D1::RectF(bx, by, bx + singleBarW, spectrumBottom);
        m_pRenderTarget->FillRectangle(barR, m_pSpectrumBrush.Get());
    }

    // 5. Center Area: Smooth Cross-fade between Text and Centered Buttons
    if (eased > 0.001f) {
        // Buttons gently unfurl from center (20px -> 26px, 4px -> 8px gap) with opacity
        float btnWidth = (20.0f + 6.0f * eased) * dpiScale;
        float btnHeight = (20.0f + 6.0f * eased) * dpiScale;
        float btnGap = (4.0f + 4.0f * eased) * dpiScale;
        float btnsTotalW = 3.0f * btnWidth + 2.0f * btnGap;

        float centerX = (barRect.left + barRect.right) * 0.5f;
        float centerY = (barRect.top + barRect.bottom) * 0.5f;
        float btnStartX = centerX - btnsTotalW * 0.5f;
        float btnTop = centerY - btnHeight * 0.5f;

        m_rcBtnPrev = D2D1::RectF(btnStartX, btnTop, btnStartX + btnWidth, btnTop + btnHeight);
        m_rcBtnPlayPause = D2D1::RectF(m_rcBtnPrev.right + btnGap, btnTop, m_rcBtnPrev.right + btnGap + btnWidth, btnTop + btnHeight);
        m_rcBtnNext = D2D1::RectF(m_rcBtnPlayPause.right + btnGap, btnTop, m_rcBtnPlayPause.right + btnGap + btnWidth, btnTop + btnHeight);

        m_pBtnIconBrush->SetOpacity(m_baseBtnIconAlpha * eased);

        auto drawBtnHover = [&](const D2D1_RECT_F& r, int idx) {
            float hAlpha = m_btnHoverAlpha[idx] * eased;
            if (hAlpha > 0.005f) {
                D2D1_ROUNDED_RECT hoverRect = D2D1::RoundedRect(r, 6.0f * dpiScale, 6.0f * dpiScale);
                m_pBtnHoverBrush->SetOpacity(m_baseBtnHoverAlpha * hAlpha);
                m_pRenderTarget->FillRoundedRectangle(hoverRect, m_pBtnHoverBrush.Get());
            }
        };

        drawBtnHover(m_rcBtnPrev, 0);
        DrawVectorPrev(m_rcBtnPrev);

        drawBtnHover(m_rcBtnPlayPause, 1);
        if (track.isPlaying) {
            DrawVectorPause(m_rcBtnPlayPause);
        } else {
            DrawVectorPlay(m_rcBtnPlayPause);
        }

        drawBtnHover(m_rcBtnNext, 2);
        DrawVectorNext(m_rcBtnNext);
    } else {
        m_rcBtnPrev = D2D1::RectF(0, 0, 0, 0);
        m_rcBtnPlayPause = D2D1::RectF(0, 0, 0, 0);
        m_rcBtnNext = D2D1::RectF(0, 0, 0, 0);
    }

    // Text in the middle cross-fades out smoothly
    float textAlpha = std::clamp(1.0f - eased * 1.4f, 0.0f, 1.0f);
    if (textAlpha > 0.01f) {
        m_pTextPrimaryBrush->SetOpacity(m_baseTextPrimaryAlpha * textAlpha);
        m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha * textAlpha);
        m_pLyricHighlightBrush->SetOpacity(m_baseLyricAlpha * textAlpha);

        float middleLeft = artRect.right + 6.0f * dpiScale;
        float middleRight = spectrumLeft - 6.0f * dpiScale;
        float middleWidth = middleRight - middleLeft;

        if (middleWidth > 30.0f * dpiScale) {
            bool hasLyrics = showLyrics && !lyrics.lines.empty() && lyrics.activeIndex >= 0;

            if (hasLyrics) {
                const auto& line = lyrics.lines[lyrics.activeIndex];
                D2D1_RECT_F lyricRect = D2D1::RectF(
                    middleLeft,
                    barRect.top + 2.0f * dpiScale,
                    middleRight,
                    barRect.bottom - 8.0f * dpiScale
                );

                m_pRenderTarget->DrawText(
                    line.text.c_str(),
                    static_cast<UINT32>(line.text.length()),
                    m_pLyricFormat.Get(),
                    lyricRect,
                    m_pLyricHighlightBrush.Get()
                );
            } else {
                float lineH = (fBarHeight - 10.0f * dpiScale) * 0.5f;

                D2D1_RECT_F titleRect = D2D1::RectF(
                    middleLeft,
                    barRect.top + 3.0f * dpiScale,
                    middleRight,
                    barRect.top + 3.0f * dpiScale + lineH
                );

                std::wstring displayTitle = track.title.empty() ? L"MusicBar" : track.title;
                m_pRenderTarget->DrawText(
                    displayTitle.c_str(),
                    static_cast<UINT32>(displayTitle.length()),
                    m_pTitleFormat.Get(),
                    titleRect,
                    m_pTextPrimaryBrush.Get()
                );

                D2D1_RECT_F artistRect = D2D1::RectF(
                    middleLeft,
                    titleRect.bottom,
                    middleRight,
                    titleRect.bottom + lineH
                );

                std::wstring displayArtist = track.artist.empty() ? L"未播放音频" : track.artist;
                m_pRenderTarget->DrawText(
                    displayArtist.c_str(),
                    static_cast<UINT32>(displayArtist.length()),
                    m_pArtistFormat.Get(),
                    artistRect,
                    m_pTextSecondaryBrush.Get()
                );
            }
        }
    }

    // 6. Progress Bar (1.5px height at bottom of the middle region)
    float progLeft = artRect.right + 6.0f * dpiScale;
    float progRight = spectrumLeft - 6.0f * dpiScale;
    float progTop = barRect.bottom - 4.5f * dpiScale;
    float progBottom = progTop + 1.5f * dpiScale;

    if (progRight > progLeft) {
        D2D1_RECT_F bgProgRect = D2D1::RectF(progLeft, progTop, progRight, progBottom);
        m_pProgressBgBrush->SetOpacity(0.15f);
        m_pRenderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(bgProgRect, 0.75f * dpiScale, 0.75f * dpiScale),
            m_pProgressBgBrush.Get()
        );

        float ratio = 0.0f;
        if (track.durationMs > 0) {
            ratio = static_cast<float>(track.GetCurrentInterpolatedPositionMs()) / static_cast<float>(track.durationMs);
            ratio = std::clamp(ratio, 0.0f, 1.0f);
        }

        if (ratio > 0.0f) {
            float curRight = progLeft + (progRight - progLeft) * ratio;
            D2D1_RECT_F fgProgRect = D2D1::RectF(progLeft, progTop, curRight, progBottom);
            m_pProgressFgBrush->SetOpacity(0.95f);
            m_pRenderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(fgProgRect, 0.75f * dpiScale, 0.75f * dpiScale),
                m_pProgressFgBrush.Get()
            );
        }
    }

    HRESULT hr = m_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return;
    }

    // Direct hardware composite update to DWM via UpdateLayeredWindow
    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc{ 0, 0 };
    SIZE sz{ static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hWnd, hdcScreen, nullptr, &sz, m_hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

} // namespace MusicBar
