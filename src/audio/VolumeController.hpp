#pragma once

#include <endpointvolume.h>
#include <wrl/client.h>

namespace MusicBar {

class VolumeController {
public:
    bool ConnectDefault();
    bool Read(float& level);
    bool Write(float level);

private:
    Microsoft::WRL::ComPtr<IAudioEndpointVolume> m_endpoint;
};

} // namespace MusicBar
