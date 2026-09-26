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

enum class AudioCaptureMode { PlayerProcess, SystemMix };

class WasapiLoopback {
public:
    WasapiLoopback();
    ~WasapiLoopback();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_isRunning; }
    void SetCaptureMode(AudioCaptureMode mode);
    void SetTargetProcess(DWORD processId);
    AudioCaptureMode GetCaptureMode() const { return m_captureMode.load(); }

    SpectrumData GetSpectrum();

private:
    std::atomic<bool> m_isRunning{false};
    std::atomic<AudioCaptureMode> m_captureMode{AudioCaptureMode::PlayerProcess};
    std::atomic<DWORD> m_targetProcessId{0};
    std::atomic<uint64_t> m_configGeneration{0};
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
