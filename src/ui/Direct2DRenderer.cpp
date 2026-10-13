#include "Direct2DRenderer.hpp"
#include "ImageLoader.hpp"
#include "LyricScroll.hpp"
#include "LyricTransition.hpp"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace MusicBar {

namespace {

// CLSID_D2D1GaussianBlur from d2d1effects.h. Declared locally so the effect
// does not depend on which library exports the symbol.
constexpr GUID kGaussianBlurEffect = {
    0x1feb6d69, 0x2fe6, 0x4ac9,
    {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

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
            L"Microsoft YaHei UI", nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            baseSize * dpiScale, L"", format.GetAddressOf());
        if (FAILED(hr)) {
            hr = m_pDWriteFactory->CreateTextFormat(
                L"Segoe UI Variable Text", nullptr, weight,
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
    m_pUpcomingLyricLayout.Reset();
    m_currentLyricWidth = 0.0f;
    m_lyricScrollX = 0.0f;
    m_lyricScrollTargetX = 0.0f;
    return S_OK;
}

HRESULT Direct2DRenderer::UpdateTheme() {
    m_isLightTheme = DetectSystemLightTheme();

    if (!m_pRenderTarget) return S_OK;

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

    HRESULT brushHr = S_OK;
    auto createBrush = [this, &brushHr](const D2D1_COLOR_F& color,
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush) {
        if (SUCCEEDED(brushHr)) {
            brushHr = m_pRenderTarget->CreateSolidColorBrush(color,
                brush.ReleaseAndGetAddressOf());
        }
    };
    createBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.005f), pHitTestBrush);
    createBrush(colorTextPrimary, m_pTextPrimaryBrush);
    createBrush(colorTextSecondary, m_pTextSecondaryBrush);
    createBrush(colorLyric, m_pLyricHighlightBrush);
    createBrush(colorProgressBg, m_pProgressBgBrush);
    createBrush(colorProgressFg, m_pProgressFgBrush);
    createBrush(colorSpectrum, m_pSpectrumBrush);
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
            gradientStops.Get(), m_pSpectrumGradientBrush.ReleaseAndGetAddressOf());
    }
    createBrush(colorBtnHover, m_pBtnHoverBrush);
    createBrush(colorIcon, m_pBtnIconBrush);
    createBrush(colorCoverPlaceholder, m_pCoverPlaceholderBrush);
    createBrush(colorFrostedBg, m_pFrostedBgBrush);
    createBrush(colorFrostedBorder, m_pFrostedBorderBrush);
    createBrush(colorFrostedHighlight, m_pFrostedHighlightBrush);
    if (FAILED(brushHr)) DiscardDeviceResources();
    return brushHr;
}

HRESULT Direct2DRenderer::CreateDeviceResources() {
    if (m_pRenderTarget) return S_OK;

    // All device calls stay on the UI thread, so D3D needs no API-level locks.
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
        D3D11_CREATE_DEVICE_SINGLETHREADED;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, _countof(levels), D3D11_SDK_VERSION,
        device.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        // Machines without a usable GPU driver fall back to the software
        // rasterizer instead of losing the bar.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, _countof(levels), D3D11_SDK_VERSION,
            device.ReleaseAndGetAddressOf(), nullptr, nullptr);
    }
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) return hr;

    hr = m_pD2DFactory->CreateDevice(dxgiDevice.Get(), &m_pD2DDevice);
    if (FAILED(hr)) return hr;

    hr = m_pD2DDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_pRenderTarget);
    if (FAILED(hr)) {
        DiscardDeviceResources();
        return hr;
    }

    // The bar is laid out in physical pixels and scales its own font sizes, so
    // the context stays at 96 DPI and treats coordinates as pixels.
    m_pRenderTarget->SetDpi(96.0f, 96.0f);
    m_pRenderTarget->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

    hr = m_pRenderTarget->CreateEffect(kGaussianBlurEffect,
        m_pBlurEffect.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = m_pBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
            D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    }
    if (FAILED(hr)) {
        DiscardDeviceResources();
        return hr;
    }

    m_pD3DDevice = device;
    hr = CreatePresentResources();
    if (FAILED(hr)) {
        DiscardDeviceResources();
        return hr;
    }

    hr = UpdateTheme();
    if (FAILED(hr)) return hr;

    // A recreated render target needs new device-bound bitmaps.
    if (!m_cachedImageBytes.empty()) {
        UpdateAlbumArt(m_cachedImageBytes);
    }

    return S_OK;
}

// Creates the composition swap chain and the DirectComposition visual tree that
// carries it to DWM. Called once per device, and again after a size change.
HRESULT Direct2DRenderer::CreatePresentResources() {
    if (!m_pD3DDevice || !m_hWnd) return E_FAIL;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_pD3DDevice.As(&dxgiDevice);
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    const UINT width = std::max(1U, m_width);
    const UINT height = std::max(1U, m_height);

    if (!m_pSwapChain) {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.Stereo = FALSE;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        description.Flags = 0;

        hr = factory->CreateSwapChainForComposition(m_pD3DDevice.Get(),
            &description, nullptr, m_pSwapChain.GetAddressOf());
        if (FAILED(hr)) return hr;

        hr = DCompositionCreateDevice(dxgiDevice.Get(),
            IID_PPV_ARGS(m_pCompositionDevice.GetAddressOf()));
        if (FAILED(hr)) return hr;

        hr = m_pCompositionDevice->CreateTargetForHwnd(m_hWnd, TRUE,
            m_pCompositionTarget.GetAddressOf());
        if (FAILED(hr)) return hr;

        hr = m_pCompositionDevice->CreateVisual(m_pCompositionVisual.GetAddressOf());
        if (FAILED(hr)) return hr;

        hr = m_pCompositionVisual->SetContent(m_pSwapChain.Get());
        if (FAILED(hr)) return hr;

        hr = m_pCompositionTarget->SetRoot(m_pCompositionVisual.Get());
        if (FAILED(hr)) return hr;

        hr = m_pCompositionDevice->Commit();
        if (FAILED(hr)) return hr;
    } else {
        // Release every reference to the old targets before resizing buffers.
        m_pRenderTarget->SetTarget(nullptr);
        if (m_pBlurEffect) m_pBlurEffect->SetInput(0, nullptr);
        m_pContentBitmap.Reset();
        m_pBackBufferBitmap.Reset();
        hr = m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return hr;
    }

    m_pContentBitmap.Reset();
    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    return m_pRenderTarget->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
        properties, m_pContentBitmap.GetAddressOf());
}

// Binds the current swap chain buffer as the Direct2D target. The wrapper is
// created per frame and unbound before Present rotates the flip-model buffers.
HRESULT Direct2DRenderer::AcquireBackBuffer() {
    if (!m_pSwapChain || !m_pRenderTarget) return E_FAIL;

    m_pBackBufferBitmap.Reset();

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()));
    if (FAILED(hr)) return hr;

    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    hr = m_pRenderTarget->CreateBitmapFromDxgiSurface(surface.Get(),
        &properties, m_pBackBufferBitmap.GetAddressOf());
    if (FAILED(hr)) return hr;

    m_pRenderTarget->SetTarget(m_pBackBufferBitmap.Get());
    return S_OK;
}

HRESULT Direct2DRenderer::Present() {
    if (!m_pSwapChain) return E_UNEXPECTED;
    // DWM schedules presentation at refresh boundaries. This call can also
    // wait for room in the presentation queue; it is not a frame clock.
    return m_pSwapChain->Present(1, 0);
}

HRESULT Direct2DRenderer::UpdateVisualOffset(const RECT& bar) {
    if (m_visualOffset.x == bar.left && m_visualOffset.y == bar.top) return S_OK;
    HRESULT hr = m_pCompositionVisual->SetOffsetX(static_cast<float>(bar.left));
    if (SUCCEEDED(hr)) {
        hr = m_pCompositionVisual->SetOffsetY(static_cast<float>(bar.top));
    }
    if (SUCCEEDED(hr)) hr = m_pCompositionDevice->Commit();
    if (SUCCEEDED(hr)) m_visualOffset = POINT{bar.left, bar.top};
    return hr;
}

void Direct2DRenderer::DiscardDeviceResources() {
    if (m_pRenderTarget) m_pRenderTarget->SetTarget(nullptr);
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
    m_pBlurLayer.Reset();
    m_pBlurEffect.Reset();
    m_pContentBitmap.Reset();
    m_pBackBufferBitmap.Reset();
    m_pCompositionVisual.Reset();
    m_pCompositionTarget.Reset();
    m_pCompositionDevice.Reset();
    m_pSwapChain.Reset();
    m_pRenderTarget.Reset();
    m_pD2DDevice.Reset();
    m_pD3DDevice.Reset();
    m_width = 0;
    m_height = 0;
    m_visualOffset = {};
}

HRESULT Direct2DRenderer::Resize(UINT width, UINT height) {
    const UINT w = std::max(1U, width);
    const UINT h = std::max(1U, height);
    if (w == m_width && h == m_height && m_pContentBitmap && m_pSwapChain) {
        return S_OK;
    }
    m_width = w;
    m_height = h;
    const HRESULT hr = CreatePresentResources();
    if (FAILED(hr)) {
        DiscardDeviceResources();
    }
    return hr;
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
    const float hover = std::clamp(m_hoverProgress, 0.0f, 1.0f);
    const bool lyricIsMoving = 1.0f - hover * hover * (3.0f - 2.0f * hover) > 0.01f &&
        std::abs(m_lyricScrollTargetX - m_lyricScrollX) > 0.2f;
    const bool coverIsMoving = m_coverHoverProgress > 0.001f &&
        m_coverHoverProgress < 0.999f;
    const bool spectrumClickIsMoving =
        m_spectrumClickTime.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - m_spectrumClickTime <
            std::chrono::milliseconds(280);
    const bool spectrumPeakIsMoving = std::any_of(m_spectrumPeaks.begin(),
        m_spectrumPeaks.end(), [](float peak) { return peak > 0.04f; });
    const bool buttonIsMoving = std::any_of(std::begin(m_btnHoverAlpha),
        std::end(m_btnHoverAlpha), [](float alpha) {
            return alpha > 0.001f && alpha < 0.999f;
        });
    return !m_pRenderTarget || !m_pSwapChain || !m_pContentBitmap ||
        hoverIsMoving || coverIsMoving || lyricIsMoving ||
        spectrumClickIsMoving || spectrumPeakIsMoving || buttonIsMoving;
}

// Blurs a hover surface on the GPU. The bar content was drawn into
// m_pContentBitmap, which is readable as a bitmap, so the effect can sample it;
// the result is composited inside the same rounded bounds the hover surface
// uses, and the layer opacity mixes it with the sharp content underneath.
void Direct2DRenderer::BlurRegion(const D2D1_RECT_F& area, float strength, float radius) {
    if (!m_pBlurEffect || !m_pContentBitmap || !m_pRenderTarget) return;
    if (strength <= 0.002f) return;
    const float width = area.right - area.left;
    const float height = area.bottom - area.top;
    if (width < 2.0f || height < 2.0f) return;

    if (!m_pBlurLayer &&
        FAILED(m_pRenderTarget->CreateLayer(nullptr, m_pBlurLayer.GetAddressOf()))) {
        return;
    }

    const float corner = std::clamp(radius, 1.0f, std::min(width, height) * 0.5f);
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> mask;
    if (FAILED(m_pD2DFactory->CreateRoundedRectangleGeometry(
        D2D1::RoundedRect(area, corner, corner), mask.GetAddressOf()))) {
        return;
    }

    const float deviation = std::max(0.5f, radius / 3.0f);
    m_pBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, deviation);
    m_pBlurEffect->SetInput(0, m_pContentBitmap.Get());

    const D2D1_LAYER_PARAMETERS layer = D2D1::LayerParameters(
        D2D1::InfiniteRect(), mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), strength);
    m_pRenderTarget->PushLayer(layer, m_pBlurLayer.Get());
    m_pRenderTarget->DrawImage(m_pBlurEffect.Get(),
        D2D1::Point2F(area.left, area.top), area,
        D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    m_pRenderTarget->PopLayer();
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

HRESULT Direct2DRenderer::Render(
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
    HRESULT resourceHr = CreateDeviceResources();
    if (FAILED(resourceHr)) return resourceHr;
    if (std::abs(dpiScale - m_textDpiScale) > 0.001f) {
        resourceHr = CreateTextFormats(dpiScale);
        if (FAILED(resourceHr)) return resourceHr;
    }

    if (rcBar.right <= rcBar.left || rcBar.bottom <= rcBar.top) return E_INVALIDARG;
    resourceHr = Resize(static_cast<UINT>(rcBar.right - rcBar.left),
        static_cast<UINT>(rcBar.bottom - rcBar.top));
    if (FAILED(resourceHr)) return resourceHr;
    resourceHr = UpdateVisualOffset(rcBar);
    if (FAILED(resourceHr)) {
        DiscardDeviceResources();
        return resourceHr;
    }

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

    // Ordinary frames draw straight into the swap chain. Only hovering needs
    // an intermediate bitmap that the blur effect can sample.
    const bool needsBlur = eased > 0.002f || coverEased > 0.002f;
    if (needsBlur) {
        m_pRenderTarget->SetTarget(m_pContentBitmap.Get());
    } else {
        resourceHr = AcquireBackBuffer();
        if (FAILED(resourceHr)) {
            DiscardDeviceResources();
            return resourceHr;
        }
    }
    m_pRenderTarget->BeginDraw();

    // Draw in bar-local pixels. DirectComposition positions the surface within
    // the taskbar-sized HWND without allocating a taskbar-sized texture.
    m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    m_pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    const float barWidth = static_cast<float>(rcBar.right - rcBar.left);
    const float barHeight = static_cast<float>(rcBar.bottom - rcBar.top);

    // Preserve the subtle base tint; hit testing uses the HWND region.
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
    float textAlpha = 1.0f - eased;
    if (textAlpha > 0.01f) {
        m_pTextPrimaryBrush->SetOpacity(m_baseTextPrimaryAlpha * textAlpha);
        m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha * textAlpha);
        m_pLyricHighlightBrush->SetOpacity(m_baseLyricAlpha * textAlpha);

        if (middleWidth > 30.0f * dpiScale) {
            const bool instrumentalNotice = !track.title.empty() && showLyrics && lyrics.isInstrumental;
            const int nextLyricIndex = !track.title.empty() && showLyrics
                ? LyricDisplayIndex(lyrics) : -1;
            const std::wstring nextLyricText = instrumentalNotice ? L"纯音乐，请欣赏" : nextLyricIndex >= 0
                ? lyrics.lines[nextLyricIndex].text : std::wstring{};
            if (track.title != m_lyricTrackTitle) {
                m_lyricTrackTitle = track.title;
                m_currentLyricText = nextLyricText;
                m_visibleLyricIndex = nextLyricIndex;
                m_lyricScrollX = 0.0f;
                m_lyricScrollTargetX = 0.0f;
                m_pCurrentLyricLayout.Reset();
                m_pUpcomingLyricLayout.Reset();
                m_upcomingLyricText.clear();
            } else if (nextLyricIndex != m_visibleLyricIndex ||
                nextLyricText != m_currentLyricText) {
                m_currentLyricText = nextLyricText;
                m_visibleLyricIndex = nextLyricIndex;
                m_lyricScrollX = 0.0f;
                m_lyricScrollTargetX = 0.0f;
                if (nextLyricText == m_upcomingLyricText && m_pUpcomingLyricLayout) {
                    m_pCurrentLyricLayout = std::move(m_pUpcomingLyricLayout);
                    m_currentLyricWidth = m_upcomingLyricWidth;
                } else {
                    m_pCurrentLyricLayout.Reset();
                }
            }
            const int64_t positionMs = track.GetCurrentInterpolatedPositionMs();
            const auto transition = nextLyricIndex >= 0
                ? ResolveLyricTransition(lyrics, positionMs) : LyricTransition{};
            const float slide = transition.progress * transition.progress *
                (3.0f - 2.0f * transition.progress);
            float lineH = (fBarHeight - 8.0f * dpiScale) * 0.5f;
            D2D1_RECT_F titleRect = D2D1::RectF(
                middleLeft, 3.0f * dpiScale,
                middleRight, 3.0f * dpiScale + lineH);
            D2D1_RECT_F detailRect = D2D1::RectF(
                middleLeft, titleRect.bottom, middleRight, titleRect.bottom + lineH);

            if (track.title.empty()) {
                constexpr wchar_t idleText[] = L"暂无音乐播放";
                const auto idleRect = D2D1::RectF(middleLeft, 0.0f,
                    middleRight, barHeight);
                m_pRenderTarget->DrawText(idleText,
                    static_cast<UINT32>(std::size(idleText) - 1),
                    m_pTitleFormat.Get(), idleRect,
                    m_pTextPrimaryBrush.Get());
            } else {
                std::wstring displayTitle = track.title;
                if (!track.artist.empty()) displayTitle += L" - " + track.artist;
                m_pRenderTarget->DrawText(displayTitle.c_str(),
                    static_cast<UINT32>(displayTitle.length()), m_pTitleFormat.Get(),
                    titleRect, m_pTextPrimaryBrush.Get());
            }

            auto createLayout = [&](const std::wstring& text,
                Microsoft::WRL::ComPtr<IDWriteTextLayout>& target, float& width) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                    text.c_str(), static_cast<UINT32>(text.size()),
                    m_pLyricFormat.Get(), 4096.0f * dpiScale, lineH, &layout))) {
                    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    DWRITE_TRIMMING noTrimming{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
                    layout->SetTrimming(&noTrimming, nullptr);
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                        width = std::max(1.0f,
                            metrics.widthIncludingTrailingWhitespace);
                        layout->SetMaxWidth(width + 1.0f);
                        target = std::move(layout);
                    }
                }
            };
            if (!m_currentLyricText.empty() && !m_pCurrentLyricLayout)
                createLayout(m_currentLyricText, m_pCurrentLyricLayout, m_currentLyricWidth);
            if (transition.nextIndex >= 0) {
                const auto& upcoming = lyrics.lines[transition.nextIndex].text;
                if (m_upcomingLyricText != upcoming) {
                    m_upcomingLyricText = upcoming;
                    m_pUpcomingLyricLayout.Reset();
                }
                if (!m_pUpcomingLyricLayout)
                    createLayout(upcoming, m_pUpcomingLyricLayout, m_upcomingLyricWidth);
            } else {
                m_pUpcomingLyricLayout.Reset();
                m_upcomingLyricText.clear();
            }

            m_pRenderTarget->PushAxisAlignedClip(detailRect,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (m_pCurrentLyricLayout && instrumentalNotice) {
                const float drawX = middleLeft + std::max(0.0f, (middleWidth - m_currentLyricWidth) * 0.5f);
                m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha * textAlpha);
                m_pRenderTarget->DrawTextLayout(D2D1::Point2F(drawX, detailRect.top),
                    m_pCurrentLyricLayout.Get(), m_pTextSecondaryBrush.Get());
            } else if (m_pCurrentLyricLayout && nextLyricIndex >= 0) {
                const auto& line = lyrics.lines[nextLyricIndex];
                const float lineProgress = line.durationMs > 0
                    ? std::clamp(static_cast<float>(positionMs - line.startMs) /
                        static_cast<float>(line.durationMs), 0.0f, 1.0f)
                    : std::clamp(lyrics.syllableProgress, 0.0f, 1.0f);
                // Standard LRC has line timing, so fill from left to right
                // over that interval instead of painting the whole line blue.
                float highlightX = m_currentLyricWidth * lineProgress;
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
                m_lyricScrollTargetX = targetScroll;
                m_lyricScrollX += (targetScroll - m_lyricScrollX) *
                    std::clamp(dt / 0.12f, 0.0f, 1.0f);
                const float drawX = maxScroll > 0.0f
                    ? middleLeft - m_lyricScrollX
                    : middleLeft + (middleWidth - m_currentLyricWidth) * 0.5f;
                const float drawY = detailRect.top - slide * lineH;
                const D2D1_POINT_2F origin = D2D1::Point2F(drawX, drawY);
                m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha *
                    textAlpha * (1.0f - slide));
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
                        textAlpha * (1.0f - slide));
                    m_pRenderTarget->DrawTextLayout(origin,
                        m_pCurrentLyricLayout.Get(), m_pLyricHighlightBrush.Get());
                    m_pRenderTarget->PopAxisAlignedClip();
                }
            }
            if (transition.nextIndex >= 0 && m_pUpcomingLyricLayout && slide > 0.0f) {
                const float drawX = m_upcomingLyricWidth > middleWidth
                    ? middleLeft : middleLeft + (middleWidth - m_upcomingLyricWidth) * 0.5f;
                const auto origin = D2D1::Point2F(drawX, detailRect.top + (1.0f - slide) * lineH);
                // Preview is unfilled. Its karaoke clock starts only when the
                // timeline selects it at the actual timestamp.
                m_pTextSecondaryBrush->SetOpacity(m_baseTextSecondaryAlpha * textAlpha * slide);
                m_pRenderTarget->DrawTextLayout(origin, m_pUpcomingLyricLayout.Get(),
                    m_pTextSecondaryBrush.Get());
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
    if (FAILED(hr)) {
        DiscardDeviceResources();
        return hr;
    }

    m_rcBtnOutputDevice = m_rcBtnPrev = m_rcBtnNext = m_rcBtnPlayPause =
        D2D1::RectF(0, 0, 0, 0);
    if (needsBlur) {
        // Pass 2 composites the layer into the swap chain buffer. The hover
        // surfaces are blurred here, where the content is already complete.
        resourceHr = AcquireBackBuffer();
        if (FAILED(resourceHr)) {
            DiscardDeviceResources();
            return resourceHr;
        }
        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        m_pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        m_pRenderTarget->DrawBitmap(m_pContentBitmap.Get(),
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width),
                static_cast<float>(m_height)),
            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

        // Blur each hover surface inside its own rounded bounds; the FFT stays untouched.
        if (coverEased > 0.002f) {
            BlurRegion(D2D1::RectF(
                artRect.left,
                artRect.top,
                artRect.right,
                artRect.bottom),
                coverEased * 0.95f, 6.0f * dpiScale);
        }
        if (eased > 0.002f && middleWidth > 30.0f * dpiScale) {
            BlurRegion(D2D1::RectF(
                middleLeft,
                0.0f,
                middleRight,
                barHeight),
                eased * 0.95f, 6.0f * dpiScale);
        }
        if (coverEased > 0.002f || (eased > 0.002f && middleWidth > 30.0f * dpiScale)) {
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
        if (FAILED(hr)) {
            DiscardDeviceResources();
            return hr;
        }
    }

    // Unbind the flip model buffer before it is presented.
    m_pRenderTarget->SetTarget(nullptr);
    m_pBackBufferBitmap.Reset();

    // Hand the finished frame to DWM.
    hr = Present();
    if (FAILED(hr)) DiscardDeviceResources();
    return hr;
}

} // namespace MusicBar
