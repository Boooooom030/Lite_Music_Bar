#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <mutex>
#include "FftAnalyzer.hpp"
#include "core/Types.hpp"

namespace MusicBar {

class WasapiLoopback {
public:
    WasapiLoopback();
    ~WasapiLoopback();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_isRunning; }

    SpectrumData GetSpectrum();

private:
    std::atomic<bool> m_isRunning{false};
    std::thread m_captureThread;

    Microsoft::WRL::ComPtr<IAudioClient> m_pAudioClient;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> m_pCaptureClient;

    WAVEFORMATEX* m_pwfx = nullptr;
    FftAnalyzer m_analyzer;
    std::mutex m_spectrumMutex;
    SpectrumData m_latestSpectrum{};

    void CaptureLoop();
};

} // namespace MusicBar
