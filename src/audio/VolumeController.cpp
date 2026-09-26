#include "VolumeController.hpp"
#include <mmdeviceapi.h>
#include <algorithm>

namespace MusicBar {

bool VolumeController::ConnectDefault() {
    m_endpoint.Reset();
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return false;
    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
        &device))) return false;
    return SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(m_endpoint.GetAddressOf())));
}

bool VolumeController::Read(float& level) {
    if (!m_endpoint && !ConnectDefault()) return false;
    if (SUCCEEDED(m_endpoint->GetMasterVolumeLevelScalar(&level))) return true;
    return ConnectDefault() &&
        SUCCEEDED(m_endpoint->GetMasterVolumeLevelScalar(&level));
}

bool VolumeController::Write(float level) {
    level = std::clamp(level, 0.0f, 1.0f);
    if (!m_endpoint && !ConnectDefault()) return false;
    if (SUCCEEDED(m_endpoint->SetMasterVolumeLevelScalar(level, nullptr))) return true;
    return ConnectDefault() &&
        SUCCEEDED(m_endpoint->SetMasterVolumeLevelScalar(level, nullptr));
}

} // namespace MusicBar
