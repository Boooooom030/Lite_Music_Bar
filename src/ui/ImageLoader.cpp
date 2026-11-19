#include "ImageLoader.hpp"
#include <algorithm>

namespace MusicBar {

bool ImageLoader::Initialize(Microsoft::WRL::ComPtr<IWICImagingFactory>& outFactory) {
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&outFactory)
    );
    return SUCCEEDED(hr);
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> ImageLoader::CreateBitmapFromMemory(
    ID2D1RenderTarget* pRenderTarget,
    IWICImagingFactory* pWicFactory,
    const std::vector<uint8_t>& imageBytes
) {
    if (!pRenderTarget || !pWicFactory || imageBytes.empty()) {
        return nullptr;
    }

    // QQ 音乐 publishes JPEGs whose EXIF block is truncated; asking WIC to cache
    // metadata while decoding makes the whole image fail. Try the lazy path
    // first and fall back to the eager one, then to a plain bitmap copy.
    auto decode = [&](WICDecodeOptions options) -> Microsoft::WRL::ComPtr<ID2D1Bitmap> {
        Microsoft::WRL::ComPtr<IWICStream> pStream;
        HRESULT hr = pWicFactory->CreateStream(&pStream);
        if (FAILED(hr)) return nullptr;
        hr = pStream->InitializeFromMemory(
            const_cast<BYTE*>(imageBytes.data()),
            static_cast<DWORD>(imageBytes.size()));
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
        hr = pWicFactory->CreateDecoderFromStream(pStream.Get(), nullptr, options, &pDecoder);
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
        hr = pDecoder->GetFrame(0, &pFrame);
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<IWICBitmapSource> pSource;
        hr = pFrame.As(&pSource);
        if (FAILED(hr)) return nullptr;
        UINT width = 0, height = 0;
        hr = pFrame->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0) return nullptr;
        constexpr UINT MaxSide = 256;
        const UINT largest = std::max(width, height);
        if (largest > MaxSide) {
            const UINT scaledWidth = std::max(1u,
                static_cast<UINT>(static_cast<uint64_t>(width) * MaxSide / largest));
            const UINT scaledHeight = std::max(1u,
                static_cast<UINT>(static_cast<uint64_t>(height) * MaxSide / largest));
            Microsoft::WRL::ComPtr<IWICBitmapScaler> pScaler;
            hr = pWicFactory->CreateBitmapScaler(&pScaler);
            if (FAILED(hr)) return nullptr;
            hr = pScaler->Initialize(pFrame.Get(), scaledWidth, scaledHeight,
                WICBitmapInterpolationModeFant);
            if (FAILED(hr)) return nullptr;
            hr = pScaler.As(&pSource);
            if (FAILED(hr)) return nullptr;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
        hr = pWicFactory->CreateFormatConverter(&pConverter);
        if (FAILED(hr)) return nullptr;
        hr = pConverter->Initialize(
            pSource.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return nullptr;

        Microsoft::WRL::ComPtr<ID2D1Bitmap> pBitmap;
        if (SUCCEEDED(pRenderTarget->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBitmap))) {
            return pBitmap;
        }
        // Some player streams hand out a color profile D2D refuses; copying into
        // a plain WIC bitmap first drops that association.
        Microsoft::WRL::ComPtr<IWICBitmap> pCopy;
        if (FAILED(pWicFactory->CreateBitmapFromSource(pConverter.Get(),
            WICBitmapCacheOnLoad, &pCopy))) return nullptr;
        if (FAILED(pRenderTarget->CreateBitmapFromWicBitmap(pCopy.Get(), nullptr, &pBitmap))) {
            return nullptr;
        }
        return pBitmap;
    };

    if (auto bitmap = decode(WICDecodeMetadataCacheOnDemand)) return bitmap;
    return decode(WICDecodeMetadataCacheOnLoad);
}

} // namespace MusicBar
