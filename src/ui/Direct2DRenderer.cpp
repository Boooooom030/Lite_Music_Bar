#include "Direct2DRenderer.hpp"
#include "ImageLoader.hpp"
#include "LyricScroll.hpp"
#include <d2d1helper.h>
#include <algorithm>
#include <cmath>
#include <cstring>

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

    return CreateTextFormats(1.0f);
}

HRESULT Direct2DRenderer::CreateTextFormats(float dpiScale) {
    if (!m_pDWriteFactory || dpiScale <= 0.0f) return E_INVALIDARG;

    auto createFormat = [this, dpiScale](DWRITE_FONT_WEIGHT weight,
        float baseSize, Microsoft::WRL::ComPtr<IDWriteTextFormat>& format) {
        HRESULT hr = m_pDWriteFactory->CreateTextFormat(
            L"Segoe UI Variable Text", nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            baseSize * dpiScale, L"", format.GetAddressOf());
        if (FAILED(hr)) {
            hr = m_pDWriteFactory->CreateTextFormat(
                L"Microsoft YaHei UI", nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                baseSize * dpiScale, L"", format.GetAddressOf());
        }
        return hr;
    };

    Microsoft::WRL::ComPtr<IDWriteTextFormat> title;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> artist;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> lyric;
    HRESULT hr = createFormat(DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, title);
    if (FAILED(hr)) return hr;
    hr = createFormat(DWRITE_FONT_WEIGHT_NORMAL, 10.0f, artist);
    if (FAILED(hr)) return hr;
    hr = createFormat(DWRITE_FONT_WEIGHT_MEDIUM, 11.0f, lyric);
    if (FAILED(hr)) return hr;

    for (auto* format : {title.Get(), artist.Get(), lyric.Get()}) {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(m_pDWriteFactory->CreateEllipsisTrimmingSign(
        title.Get(), &ellipsis))) {
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        title->SetTrimming(&trimming, ellipsis.Get());
        artist->SetTrimming(&trimming, ellipsis.Get());
        lyric->SetTrimming(&trimming, ellipsis.Get());
    }

    m_pTitleFormat = std::move(title);
    m_pArtistFormat = std::move(artist);
    m_pLyricFormat = std::move(lyric);
    m_textDpiScale = dpiScale;
    m_pCurrentLyricLayout.Reset();
    m_currentLyricWidth = 0.0f;
    m_lyricScrollX = 0.0f;
    return S_OK;
}

void Direct2DRenderer::UpdateTheme() {
    m_isLightTheme = DetectSystemLightTheme();

    if (!m_pRenderTarget) return;

    m_baseTextPrimaryAlpha = 0.95f;
    m_baseTextSecondaryAlpha = m_isLightTheme ? 0.88f : 0.85f;
    m_baseLyricAlpha = 0.95f;
    m_baseBtnIconAlpha = 0.95f;
    m_baseBtnHoverAlpha = m_isLightTheme ? 0.10f : 0.13f;
    m_baseBgAlpha = m_isLightTheme ? 0.16f : 0.20f;
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

    D2D1_COLOR_F colorBtnHover = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);

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
        ? D2D1::ColorF(0.86f, 0.88f, 0.91f, 1.0f)
        : D2D1::ColorF(0.36f, 0.38f, 0.42f, 1.0f);

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
    m_pSpectrumGradientBrush.Reset();
    const D2D1_GRADIENT_STOP spectrumStops[] = {
        {0.0f, m_isLightTheme
            ? D2D1::ColorF(0.02f, 0.65f, 0.90f, 0.96f)
            : D2D1::ColorF(0.48f, 0.91f, 1.0f, 0.98f)},
        {0.55f, m_isLightTheme
            ? D2D1::ColorF(0.02f, 0.50f, 0.85f, 0.88f)
            : D2D1::ColorF(0.25f, 0.72f, 0.98f, 0.88f)},
        {1.0f, m_isLightTheme
            ? D2D1::ColorF(0.08f, 0.39f, 0.75f, 0.70f)
            : D2D1::ColorF(0.13f, 0.53f, 0.88f, 0.68f)}
    };
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> gradientStops;
    if (SUCCEEDED(m_pRenderTarget->CreateGradientStopCollection(
        spectrumStops, _countof(spectrumStops), &gradientStops))) {
        m_pRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, 48.0f)),
            gradientStops.Get(), &m_pSpectrumGradientBrush);
    }
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
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f
    );

    HRESULT hr = m_pD2DFactory->CreateDCRenderTarget(
        &rtProps,
        &m_pRenderTarget
    );
    if (FAILED(hr)) return hr;

    UpdateTheme();

    // A recreated render target needs new device-bound bitmaps.
    if (!m_cachedImageBytes.empty()) {
        UpdateAlbumArt(m_cachedImageBytes);
    }

    return S_OK;
}

void Direct2DRenderer::DiscardDeviceResources() {
    m_pAlbumArtBitmap.Reset();
    m_albumArtCache.clear();
    pHitTestBrush.Reset();
    m_pTextPrimaryBrush.Reset();
    m_pTextSecondaryBrush.Reset();
    m_pLyricHighlightBrush.Reset();
    m_pProgressBgBrush.Reset();
    m_pProgressFgBrush.Reset();
    m_pSpectrumBrush.Reset();
    m_pSpectrumGradientBrush.Reset();
    m_pBtnHoverBrush.Reset();
    m_pBtnIconBrush.Reset();
    m_pCoverPlaceholderBrush.Reset();
    m_pFrostedBgBrush.Reset();
    m_pFrostedBorderBrush.Reset();
    m_pFrostedHighlightBrush.Reset();
    m_pRenderTarget.Reset();

    if (m_hMemDC && m_hOldBitmap) {
        SelectObject(m_hMemDC, m_hOldBitmap);
        m_hOldBitmap = nullptr;
    }
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

    if (m_hMemDC && m_hOldBitmap) {
        SelectObject(m_hMemDC, m_hOldBitmap);
        m_hOldBitmap = nullptr;
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
    if (m_hDIB) m_hOldBitmap = SelectObject(m_hMemDC, m_hDIB);
    ReleaseDC(nullptr, hdcScreen);
}

void Direct2DRenderer::UpdateAlbumArt(const std::vector<uint8_t>& imageBytes) {
    if (&imageBytes != &m_cachedImageBytes) m_cachedImageBytes = imageBytes;
    m_pAlbumArtBitmap.Reset();
    if (m_cachedImageBytes.empty() || !m_pRenderTarget || !m_pWICFactory) return;

    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : m_cachedImageBytes) {
        hash = (hash ^ byte) * 1099511628211ull;
    }
    const auto cached = std::find_if(m_albumArtCache.begin(),
        m_albumArtCache.end(), [&](const CachedAlbumArt& art) {
            return art.hash == hash && art.length == m_cachedImageBytes.size();
        });
    if (cached != m_albumArtCache.end()) {
        m_pAlbumArtBitmap = cached->bitmap;
        auto entry = std::move(*cached);
        m_albumArtCache.erase(cached);
        m_albumArtCache.push_front(std::move(entry));
        return;
    }
    m_pAlbumArtBitmap = ImageLoader::CreateBitmapFromMemory(
        m_pRenderTarget.Get(), m_pWICFactory.Get(), m_cachedImageBytes);
    if (m_pAlbumArtBitmap) {
        m_albumArtCache.push_front({hash, m_cachedImageBytes.size(),
            m_pAlbumArtBitmap});
        if (m_albumArtCache.size() > 8) m_albumArtCache.pop_back();
    }
}

ButtonType Direct2DRenderer::HitTestButtons(int mouseX, int mouseY) const {
    float mx = static_cast<float>(mouseX);
    float my = static_cast<float>(mouseY);
    if (m_coverHoverProgress > 0.01f &&
        mx >= m_rcBtnPlayPause.left && mx < m_rcBtnPlayPause.right &&
        my >= m_rcBtnPlayPause.top && my < m_rcBtnPlayPause.bottom) {
        return ButtonType::PlayPause;
    }
    if (m_hoverProgress <= 0.01f) return ButtonType::None;
    if (mx >= m_rcBtnOutputDevice.left && mx < m_rcBtnOutputDevice.right &&
        my >= m_rcBtnOutputDevice.top && my < m_rcBtnOutputDevice.bottom) {
        return ButtonType::OutputDevice;
    }
    if (mx >= m_rcBtnPrev.left && mx <= m_rcBtnPrev.right &&
        my >= m_rcBtnPrev.top && my <= m_rcBtnPrev.bottom) {
        return ButtonType::Prev;
    }
    if (mx >= m_rcBtnNext.left && mx <= m_rcBtnNext.right &&
        my >= m_rcBtnNext.top && my <= m_rcBtnNext.bottom) {
        return ButtonType::Next;
    }
    return ButtonType::None;
}

bool Direct2DRenderer::HitTestMiddle(int mouseX, int mouseY) const {
    return mouseX >= m_rcMiddle.left && mouseX < m_rcMiddle.right &&
        mouseY >= m_rcMiddle.top && mouseY < m_rcMiddle.bottom;
}

bool Direct2DRenderer::HitTestCover(int mouseX, int mouseY) const {
    return mouseX >= m_rcCover.left && mouseX < m_rcCover.right &&
        mouseY >= m_rcCover.top && mouseY < m_rcCover.bottom;
}

bool Direct2DRenderer::HitTestSpectrum(int mouseX, int mouseY) const {
    return mouseX >= m_rcSpectrum.left && mouseX < m_rcSpectrum.right &&
        mouseY >= m_rcSpectrum.top && mouseY < m_rcSpectrum.bottom;
}

RECT Direct2DRenderer::GetSpectrumRect() const {
    return RECT{static_cast<LONG>(m_rcSpectrum.left),
        static_cast<LONG>(m_rcSpectrum.top),
        static_cast<LONG>(m_rcSpectrum.right),
        static_cast<LONG>(m_rcSpectrum.bottom)};
}

RECT Direct2DRenderer::GetOutputDeviceRect() const {
    return RECT{static_cast<LONG>(m_rcBtnOutputDevice.left),
        static_cast<LONG>(m_rcBtnOutputDevice.top),
        static_cast<LONG>(m_rcBtnOutputDevice.right),
        static_cast<LONG>(m_rcBtnOutputDevice.bottom)};
}

void Direct2DRenderer::TriggerSpectrumClick() {
    m_spectrumClickTime = std::chrono::steady_clock::now();
}

bool Direct2DRenderer::HasPendingAnimation() const {
    const bool hoverIsMoving = m_hoverProgress > 0.001f &&
        m_hoverProgress < 0.999f;
    const bool lyricIsMoving = m_lyricTransitionStart.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - m_lyricTransitionStart <
            std::chrono::milliseconds(400);
    const bool coverIsMoving = m_coverHoverProgress > 0.001f &&
        m_coverHoverProgress < 0.999f;
    const bool spectrumClickIsMoving =
        m_spectrumClickTime.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - m_spectrumClickTime <
            std::chrono::milliseconds(280);
    const bool spectrumPeakIsMoving = std::any_of(m_spectrumPeaks.begin(),
        m_spectrumPeaks.end(), [](float peak) { return peak > 0.04f; });
    return hoverIsMoving || coverIsMoving || lyricIsMoving ||
        spectrumClickIsMoving || spectrumPeakIsMoving;
}

void Direct2DRenderer::BlurRoundedPixels(const RECT& area, float strength, float radius) {
    if (!m_pDIBBits || strength <= 0.0f) return;
    const int left = std::clamp<LONG>(area.left, 0, m_width);
    const int top = std::clamp<LONG>(area.top, 0, m_height);
    const int right = std::clamp<LONG>(area.right, 0, m_width);
    const int bottom = std::clamp<LONG>(area.bottom, 0, m_height);
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) return;
    radius = std::clamp(radius, 0.0f, std::min(width, height) * 0.5f);

    const size_t bytes = static_cast<size_t>(width) * height * 4;
    m_blurSource.resize(bytes);
    m_blurHorizontal.resize(bytes);
    auto* canvas = static_cast<uint8_t*>(m_pDIBBits);
    for (int y = 0; y < height; ++y) {
        std::memcpy(m_blurSource.data() + static_cast<size_t>(y) * width * 4,
            canvas + (static_cast<size_t>(top + y) * m_width + left) * 4,
            static_cast<size_t>(width) * 4);
    }
    constexpr int weights[7] = {1, 6, 15, 20, 15, 6, 1};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                int total = 0;
                for (int tap = -3; tap <= 3; ++tap) {
                    const int sampleX = std::clamp(x + tap, 0, width - 1);
                    total += weights[tap + 3] * m_blurSource[
                        (static_cast<size_t>(y) * width + sampleX) * 4 + channel];
                }
                m_blurHorizontal[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                    static_cast<uint8_t>((total + 32) / 64);
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float cornerX = std::clamp(x + 0.5f, radius, width - radius);
            const float cornerY = std::clamp(y + 0.5f, radius, height - radius);
            const float dx = x + 0.5f - cornerX;
            const float dy = y + 0.5f - cornerY;
            const float coverage = std::clamp(radius + 0.5f -
                std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            if (coverage <= 0.0f) continue;
            auto* output = canvas +
                (static_cast<size_t>(top + y) * m_width + left + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                int total = 0;
                for (int tap = -3; tap <= 3; ++tap) {
                    const int sampleY = std::clamp(y + tap, 0, height - 1);
                    total += weights[tap + 3] * m_blurHorizontal[
                        (static_cast<size_t>(sampleY) * width + x) * 4 + channel];
                }
                const auto original = m_blurSource[
                    (static_cast<size_t>(y) * width + x) * 4 + channel];
                output[channel] = static_cast<uint8_t>(std::clamp(
                    original * (1.0f - strength * coverage) +
                        (total / 64.0f) * strength * coverage,
                    0.0f, 255.0f));
            }
        }
    }
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

void Direct2DRenderer::DrawVectorStereo(const D2D1_RECT_F& rect) {
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float size = rect.right - rect.left;
    const float stroke = std::max(1.0f, size * 0.065f);
    const auto cabinet = D2D1::RoundedRect(D2D1::RectF(
        cx - size * 0.24f, cy - size * 0.37f,
        cx + size * 0.24f, cy + size * 0.37f),
        size * 0.055f, size * 0.055f);
    m_pRenderTarget->DrawRoundedRectangle(cabinet,
        m_pBtnIconBrush.Get(), stroke);
    m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(
        cx, cy - size * 0.17f), size * 0.055f, size * 0.055f),
        m_pBtnIconBrush.Get());
    m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(
        cx, cy + size * 0.15f), size * 0.145f, size * 0.145f),
        m_pBtnIconBrush.Get(), stroke);
    m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(
        cx, cy + size * 0.15f), size * 0.035f, size * 0.035f),
        m_pBtnIconBrush.Get());
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
    bool isMiddleHovered,
    bool isCoverHovered
) {
    (void)screenPos;
    if (FAILED(CreateDeviceResources())) return;
    if (std::abs(dpiScale - m_textDpiScale) > 0.001f &&
        FAILED(CreateTextFormats(dpiScale))) return;

    RECT canvas{};
    GetClientRect(m_hWnd, &canvas);
    Resize(static_cast<UINT>(canvas.right), static_cast<UINT>(canvas.bottom));
    if (!m_hMemDC || !m_hDIB) return;

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

    float targetHover = isMiddleHovered ? 1.0f : 0.0f;
    float change = dt / 0.38f; // ~380ms slow and gentle transition
    if (m_hoverProgress < targetHover) {
        m_hoverProgress = std::min(targetHover, m_hoverProgress + change);
    } else if (m_hoverProgress > targetHover) {
        m_hoverProgress = std::max(targetHover, m_hoverProgress - change);
    }
    const float coverTarget = isCoverHovered && !track.title.empty() ? 1.0f : 0.0f;
    if (m_coverHoverProgress < coverTarget) {
        m_coverHoverProgress = std::min(coverTarget, m_coverHoverProgress + change);
    } else if (m_coverHoverProgress > coverTarget) {
        m_coverHoverProgress = std::max(coverTarget, m_coverHoverProgress - change);
    }

    // Smoothstep easing: f(t) = t * t * (3 - 2t)
    float t = std::clamp(m_hoverProgress, 0.0f, 1.0f);
    float eased = t * t * (3.0f - 2.0f * t);
    const float coverT = std::clamp(m_coverHoverProgress, 0.0f, 1.0f);
    const float coverEased = coverT * coverT * (3.0f - 2.0f * coverT);

    // Individual button hover alpha animations (~0.18s glow)
    auto updateBtnHover = [&](int idx, ButtonType type, bool active) {
        float target = (hoveredBtn == type && active) ? 1.0f : 0.0f;
        float btnChange = dt / 0.18f;
        if (m_btnHoverAlpha[idx] < target) {
            m_btnHoverAlpha[idx] = std::min(target, m_btnHoverAlpha[idx] + btnChange);
        } else if (m_btnHoverAlpha[idx] > target) {
            m_btnHoverAlpha[idx] = std::max(target, m_btnHoverAlpha[idx] - btnChange);
        }
    };
    updateBtnHover(0, ButtonType::OutputDevice, isMiddleHovered);
    updateBtnHover(1, ButtonType::Prev, isMiddleHovered);
    updateBtnHover(2, ButtonType::Next, isMiddleHovered);
    updateBtnHover(3, ButtonType::PlayPause, isCoverHovered);

    // Clear the taskbar-sized canvas, then draw only inside the docked bar region.
    m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    m_pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(rcBar.left), static_cast<float>(rcBar.top)));

    const float barWidth = static_cast<float>(rcBar.right - rcBar.left);
    const float barHeight = static_cast<float>(rcBar.bottom - rcBar.top);

    // Invisible hit-test area (alpha=1/255) ensures entire bar area handles drag/right-click
    m_pRenderTarget->FillRectangle(
        D2D1::RectF(0.0f, 0.0f, barWidth, barHeight),
        pHitTestBrush.Get()
    );

    D2D1_RECT_F barRect = D2D1::RectF(
        0.0f,
        0.0f,
        barWidth,
        barHeight
    );

    float fBarHeight = barRect.bottom - barRect.top;
    float padding = 6.0f * dpiScale;

    // 3. Draw Album Art (Left side: vertically centered)
    float artSize = fBarHeight - 2.0f * padding;
    D2D1_RECT_F artRect = D2D1::RectF(
        barRect.left + padding + 2.0f * dpiScale,
        barRect.top + padding,
        barRect.left + padding + 2.0f * dpiScale + artSize,
        barRect.top + padding + artSize
    );

    D2D1_ROUNDED_RECT artRounded = D2D1::RoundedRect(artRect, 6.0f * dpiScale, 6.0f * dpiScale);
    m_rcCover = artRect;

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

    // 4. The spectrum stays on the right and scales with available taskbar width.
    float spectrumWidth = std::clamp(barWidth * 0.18f, 36.0f * dpiScale, 62.0f * dpiScale);
    float spectrumRight = barRect.right - 7.0f * dpiScale;
    float spectrumLeft = spectrumRight - spectrumWidth;
    const float spectrumHeight = fBarHeight;
    m_rcSpectrum = D2D1::RectF(spectrumLeft, 0.0f, spectrumRight, barHeight);

    size_t numBands = SpectrumData::BAND_COUNT;
    float barGap = 1.0f * dpiScale;
    float singleBarW = (spectrumWidth - (numBands - 1) * barGap) / numBands;

    if (m_spectrumClickTime.time_since_epoch().count() != 0) {
        const float elapsed = std::chrono::duration<float>(
            now - m_spectrumClickTime).count();
        const float pulse = std::clamp(1.0f - elapsed / 0.28f, 0.0f, 1.0f);
        if (pulse > 0.0f) {
            const auto pulseRect = D2D1::RoundedRect(D2D1::RectF(
                spectrumLeft - 3.0f * dpiScale, 0.0f,
                spectrumRight + 3.0f * dpiScale, barHeight),
                5.0f * dpiScale, 5.0f * dpiScale);
            m_pSpectrumBrush->SetOpacity(0.14f * pulse * pulse);
            m_pRenderTarget->FillRoundedRectangle(pulseRect,
                m_pSpectrumBrush.Get());
            m_pSpectrumBrush->SetOpacity(0.34f * pulse);
            m_pRenderTarget->DrawRoundedRectangle(pulseRect,
                m_pSpectrumBrush.Get(), 1.0f * dpiScale);
        }
    }
    m_pSpectrumBrush->SetOpacity(0.90f);
    if (m_pSpectrumGradientBrush) {
        m_pSpectrumGradientBrush->SetStartPoint(D2D1::Point2F(0.0f, 0.0f));
        m_pSpectrumGradientBrush->SetEndPoint(D2D1::Point2F(0.0f, barHeight));
    }
    auto* spectrumBodyBrush = m_pSpectrumGradientBrush
        ? static_cast<ID2D1Brush*>(m_pSpectrumGradientBrush.Get())
        : static_cast<ID2D1Brush*>(m_pSpectrumBrush.Get());
    for (size_t i = 0; i < numBands; ++i) {
        const float level = std::clamp(spectrum.bands[i], 0.0f, 1.0f);
        if (level >= m_spectrumPeaks[i]) {
            m_spectrumPeaks[i] = level;
            m_peakHoldUntil[i] = now + std::chrono::milliseconds(120);
        } else if (now >= m_peakHoldUntil[i]) {
            m_spectrumPeaks[i] = std::max(level,
                m_spectrumPeaks[i] - dt * 0.85f);
        }

        const float bx = spectrumLeft + i * (singleBarW + barGap);
        const float height = spectrumHeight *
            (0.025f + 0.90f * std::pow(level, 1.15f));
        const float radius = std::min(singleBarW * 0.45f, height * 0.5f);
        const auto body = D2D1::RectF(bx, barHeight - height,
            bx + singleBarW, barHeight);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(body,
            radius, radius), spectrumBodyBrush);
        m_pRenderTarget->FillRectangle(D2D1::RectF(bx,
            barHeight - std::min(radius, height), bx + singleBarW,
            barHeight), spectrumBodyBrush);

        if (m_spectrumPeaks[i] > 0.07f) {
            const float peakHeight = spectrumHeight *
                (0.025f + 0.90f * std::pow(m_spectrumPeaks[i], 1.15f));
            const float capHeight = std::max(1.2f, 1.4f * dpiScale);
            const float capY = std::max(0.0f,
                barHeight - peakHeight - capHeight - 1.0f * dpiScale);
            m_pSpectrumBrush->SetOpacity(0.90f);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(bx, capY, bx + singleBarW,
                    capY + capHeight), capHeight * 0.5f,
                capHeight * 0.5f), m_pSpectrumBrush.Get());
        }
    }

    float middleLeft = artRect.right + 6.0f * dpiScale;
    float middleRight = spectrumLeft - 6.0f * dpiScale;
    float middleWidth = middleRight - middleLeft;
    m_rcMiddle = D2D1::RectF(middleLeft, 0.0f, middleRight, barHeight);

    // Draw the text first; the middle hover surface is composited later.
    float textAlpha = !track.title.empty()
        ? 1.0f - eased
        : 1.0f;
    if (textAlpha > 0.01f) {
        m_pTextPrimaryBrush->SetOpacity(m_baseTextPrimaryAlpha * textAlpha);
        m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha * textAlpha);
        m_pLyricHighlightBrush->SetOpacity(m_baseLyricAlpha * textAlpha);

        if (middleWidth > 30.0f * dpiScale) {
            const int nextLyricIndex = showLyrics && lyrics.activeIndex >= 0 &&
                lyrics.activeIndex < static_cast<int>(lyrics.lines.size())
                ? lyrics.activeIndex : -1;
            const std::wstring nextLyricText = nextLyricIndex >= 0
                ? lyrics.lines[nextLyricIndex].text : std::wstring{};
            if (track.title != m_lyricTrackTitle) {
                m_lyricTrackTitle = track.title;
                m_previousLyricText.clear();
                m_currentLyricText = nextLyricText;
                m_visibleLyricIndex = nextLyricIndex;
                m_lyricTransitionStart = now - std::chrono::milliseconds(400);
                m_lyricScrollX = 0.0f;
                m_pCurrentLyricLayout.Reset();
            } else if (nextLyricIndex != m_visibleLyricIndex ||
                nextLyricText != m_currentLyricText) {
                m_previousLyricText = m_currentLyricText;
                m_currentLyricText = nextLyricText;
                m_visibleLyricIndex = nextLyricIndex;
                m_lyricTransitionStart = now;
                m_lyricScrollX = 0.0f;
                m_pCurrentLyricLayout.Reset();
            }
            const float slideTime = std::clamp(
                std::chrono::duration<float>(now - m_lyricTransitionStart).count() / 0.40f,
                0.0f, 1.0f);
            const float slide = slideTime * slideTime * (3.0f - 2.0f * slideTime);
            float lineH = (fBarHeight - 8.0f * dpiScale) * 0.5f;
            D2D1_RECT_F titleRect = D2D1::RectF(
                middleLeft, 3.0f * dpiScale,
                middleRight, 3.0f * dpiScale + lineH);
            D2D1_RECT_F detailRect = D2D1::RectF(
                middleLeft, titleRect.bottom, middleRight, titleRect.bottom + lineH);

            if (!track.title.empty()) {
                std::wstring displayTitle = track.title;
                if (!track.artist.empty()) displayTitle += L" - " + track.artist;
                m_pRenderTarget->DrawText(displayTitle.c_str(),
                    static_cast<UINT32>(displayTitle.length()), m_pTitleFormat.Get(),
                    titleRect, m_pTextPrimaryBrush.Get());
            }

            if (!m_currentLyricText.empty() && !m_pCurrentLyricLayout) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                    m_currentLyricText.c_str(),
                    static_cast<UINT32>(m_currentLyricText.size()),
                    m_pLyricFormat.Get(), 4096.0f * dpiScale, lineH, &layout))) {
                    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    DWRITE_TRIMMING noTrimming{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
                    layout->SetTrimming(&noTrimming, nullptr);
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                        m_currentLyricWidth = std::max(1.0f,
                            metrics.widthIncludingTrailingWhitespace);
                        layout->SetMaxWidth(m_currentLyricWidth + 1.0f);
                        m_pCurrentLyricLayout = std::move(layout);
                    }
                }
            }

            m_pRenderTarget->PushAxisAlignedClip(detailRect,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (!m_previousLyricText.empty() && slide < 1.0f) {
                const float oldOffset = slide * lineH;
                D2D1_RECT_F oldRect = D2D1::RectF(detailRect.left,
                    detailRect.top - oldOffset, detailRect.right,
                    detailRect.bottom - oldOffset);
                m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha *
                    textAlpha * (1.0f - slide));
                m_pRenderTarget->DrawText(m_previousLyricText.c_str(),
                    static_cast<UINT32>(m_previousLyricText.size()),
                    m_pLyricFormat.Get(), oldRect, m_pTextSecondaryBrush.Get());
            }

            if (m_pCurrentLyricLayout && nextLyricIndex >= 0) {
                const auto& line = lyrics.lines[nextLyricIndex];
                const int64_t positionMs = track.GetCurrentInterpolatedPositionMs();
                const float lineProgress = line.durationMs > 0
                    ? std::clamp(static_cast<float>(positionMs - line.startMs) /
                        static_cast<float>(line.durationMs), 0.0f, 1.0f)
                    : std::clamp(lyrics.syllableProgress, 0.0f, 1.0f);
                float highlightX = m_currentLyricWidth;
                float focusX = m_currentLyricWidth * lineProgress;
                if (line.HasKaraoke()) {
                    highlightX = 0.0f;
                    if (lyrics.activeSyllableIndex >= 0) {
                        const size_t activeSyllable = std::min<size_t>(
                            static_cast<size_t>(lyrics.activeSyllableIndex),
                            line.syllables.size() - 1);
                        UINT32 charStart = 0;
                        for (size_t i = 0; i < activeSyllable; ++i) {
                            charStart += static_cast<UINT32>(line.syllables[i].text.size());
                        }
                        const UINT32 charEnd = charStart +
                            static_cast<UINT32>(line.syllables[activeSyllable].text.size());
                        FLOAT startX = 0.0f, endX = 0.0f, unusedY = 0.0f;
                        DWRITE_HIT_TEST_METRICS metrics{};
                        if (charEnd > charStart &&
                            charEnd <= m_currentLyricText.size() &&
                            SUCCEEDED(m_pCurrentLyricLayout->HitTestTextPosition(
                                charStart, FALSE, &startX, &unusedY, &metrics)) &&
                            SUCCEEDED(m_pCurrentLyricLayout->HitTestTextPosition(
                                charEnd - 1, TRUE, &endX, &unusedY, &metrics))) {
                            highlightX = startX + (endX - startX) *
                                std::clamp(lyrics.syllableProgress, 0.0f, 1.0f);
                        }
                    }
                    focusX = highlightX;
                }
                const float maxScroll = std::max(0.0f,
                    m_currentLyricWidth - middleWidth);
                const float targetScroll = LyricScrollTarget(
                    m_currentLyricWidth, middleWidth, focusX, lineProgress);
                m_lyricScrollX += (targetScroll - m_lyricScrollX) *
                    std::clamp(dt / 0.12f, 0.0f, 1.0f);
                const float drawX = maxScroll > 0.0f
                    ? middleLeft - m_lyricScrollX
                    : middleLeft + (middleWidth - m_currentLyricWidth) * 0.5f;
                const float drawY = detailRect.top + (1.0f - slide) * lineH;
                const D2D1_POINT_2F origin = D2D1::Point2F(drawX, drawY);
                m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha *
                    textAlpha * slide);
                m_pRenderTarget->DrawTextLayout(origin, m_pCurrentLyricLayout.Get(),
                    m_pTextSecondaryBrush.Get());
                const float filledRight = std::min(detailRect.right,
                    drawX + highlightX);
                if (filledRight > detailRect.left) {
                    m_pRenderTarget->PushAxisAlignedClip(
                        D2D1::RectF(detailRect.left, detailRect.top,
                            filledRight, detailRect.bottom),
                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    m_pLyricHighlightBrush->SetOpacity(m_baseLyricAlpha *
                        textAlpha * slide);
                    m_pRenderTarget->DrawTextLayout(origin,
                        m_pCurrentLyricLayout.Get(), m_pLyricHighlightBrush.Get());
                    m_pRenderTarget->PopAxisAlignedClip();
                }
            }
            m_pRenderTarget->PopAxisAlignedClip();
        }
    }

    // 7. Progress Bar under the text region.
    float progLeft = artRect.right + 6.0f * dpiScale;
    float progRight = spectrumLeft - 6.0f * dpiScale;
    float progTop = barRect.bottom - 3.0f * dpiScale;
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
    if (FAILED(hr)) return;

    // Blur each hover surface inside its own rounded bounds; the FFT stays untouched.
    m_rcBtnOutputDevice = m_rcBtnPrev = m_rcBtnNext = m_rcBtnPlayPause =
        D2D1::RectF(0, 0, 0, 0);
    if (coverEased > 0.002f) {
        RECT coverArea{rcBar.left + static_cast<LONG>(artRect.left),
            rcBar.top + static_cast<LONG>(artRect.top),
            rcBar.left + static_cast<LONG>(artRect.right),
            rcBar.top + static_cast<LONG>(artRect.bottom)};
        BlurRoundedPixels(coverArea, coverEased * 0.95f, 6.0f * dpiScale);
    }
    if (eased > 0.002f && middleWidth > 30.0f * dpiScale) {
        RECT middleArea{rcBar.left + static_cast<LONG>(middleLeft), rcBar.top,
            rcBar.left + static_cast<LONG>(middleRight), rcBar.bottom};
        BlurRoundedPixels(middleArea, eased * 0.95f, 6.0f * dpiScale);
    }
    if (coverEased > 0.002f || (eased > 0.002f && middleWidth > 30.0f * dpiScale)) {
        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Translation(
            static_cast<float>(rcBar.left), static_cast<float>(rcBar.top)));
        if (coverEased > 0.002f) {
            m_pFrostedBgBrush->SetOpacity((0.58f +
                0.08f * m_btnHoverAlpha[3]) * coverEased);
            m_pRenderTarget->FillRoundedRectangle(artRounded,
                m_pFrostedBgBrush.Get());
            m_rcBtnPlayPause = artRect;
            const float inset = 2.0f * dpiScale;
            const auto iconRect = D2D1::RectF(artRect.left + inset,
                artRect.top + inset, artRect.right - inset, artRect.bottom - inset);
            m_pBtnIconBrush->SetOpacity(m_baseBtnIconAlpha * coverEased);
            if (track.isPlaying) DrawVectorPause(iconRect);
            else DrawVectorPlay(iconRect);
        }
        if (eased > 0.002f && middleWidth > 30.0f * dpiScale) {
            const auto middleRect = D2D1::RectF(middleLeft, 0.0f,
                middleRight, barHeight);
            m_pFrostedBgBrush->SetOpacity((m_baseBgAlpha + 0.06f) * eased);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(middleRect,
                6.0f * dpiScale, 6.0f * dpiScale), m_pFrostedBgBrush.Get());

            const float buttonGap = 8.0f * dpiScale;
            const float buttonSize = std::min(26.0f * dpiScale,
                std::max(0.0f, (middleWidth - 2.0f * buttonGap -
                    4.0f * dpiScale) / 3.0f));
            if (buttonSize >= 12.0f * dpiScale) {
                const float totalWidth = 3.0f * buttonSize + 2.0f * buttonGap;
                const float startX = (middleLeft + middleRight - totalWidth) * 0.5f;
                const float buttonY = (barHeight - buttonSize) * 0.5f;
                m_rcBtnOutputDevice = D2D1::RectF(startX, buttonY,
                    startX + buttonSize, buttonY + buttonSize);
                m_pBtnIconBrush->SetOpacity(m_baseBtnIconAlpha * eased);
                auto drawHover = [&](const D2D1_RECT_F& rect, int index) {
                    const float opacity = m_btnHoverAlpha[index] * eased;
                    if (opacity > 0.005f) {
                        m_pBtnHoverBrush->SetOpacity(m_baseBtnHoverAlpha * opacity);
                        m_pRenderTarget->FillRoundedRectangle(
                            D2D1::RoundedRect(rect, 6.0f * dpiScale,
                                6.0f * dpiScale), m_pBtnHoverBrush.Get());
                    }
                };
                drawHover(m_rcBtnOutputDevice, 0);
                DrawVectorStereo(m_rcBtnOutputDevice);
                if (!track.title.empty()) {
                    m_rcBtnPrev = D2D1::RectF(m_rcBtnOutputDevice.right + buttonGap,
                        buttonY, m_rcBtnOutputDevice.right + buttonGap + buttonSize,
                        buttonY + buttonSize);
                    m_rcBtnNext = D2D1::RectF(m_rcBtnPrev.right + buttonGap,
                        buttonY, m_rcBtnPrev.right + buttonGap + buttonSize,
                        buttonY + buttonSize);
                    drawHover(m_rcBtnPrev, 1);
                    DrawVectorPrev(m_rcBtnPrev);
                    drawHover(m_rcBtnNext, 2);
                    DrawVectorNext(m_rcBtnNext);
                }
            }
        }
        hr = m_pRenderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
            return;
        }
        if (FAILED(hr)) return;
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

    UpdateLayeredWindow(m_hWnd, hdcScreen, nullptr, &sz,
        m_hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

} // namespace MusicBar
