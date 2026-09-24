#pragma once

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

namespace MusicBar {

class ImageLoader {
public:
    static bool Initialize(Microsoft::WRL::ComPtr<IWICImagingFactory>& outFactory);

    static Microsoft::WRL::ComPtr<ID2D1Bitmap> CreateBitmapFromMemory(
        ID2D1RenderTarget* pRenderTarget,
        IWICImagingFactory* pWicFactory,
        const std::vector<uint8_t>& imageBytes
    );
};

} // namespace MusicBar
