#include "ImageLoader.hpp"

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

    Microsoft::WRL::ComPtr<IWICStream> pStream;
    HRESULT hr = pWicFactory->CreateStream(&pStream);
    if (FAILED(hr)) return nullptr;

    hr = pStream->InitializeFromMemory(
        const_cast<BYTE*>(imageBytes.data()),
        static_cast<DWORD>(imageBytes.size())
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
    hr = pWicFactory->CreateDecoderFromStream(
        pStream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &pDecoder
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
    hr = pWicFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) return nullptr;

    hr = pConverter->Initialize(
        pFrame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> pBitmap;
    hr = pRenderTarget->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBitmap);
    if (FAILED(hr)) return nullptr;

    return pBitmap;
}

} // namespace MusicBar
