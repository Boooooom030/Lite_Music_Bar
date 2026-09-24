#include "WasapiLoopback.hpp"
#include <vector>
#include <cmath>
#include <ksmedia.h>

namespace MusicBar {

WasapiLoopback::WasapiLoopback() = default;

WasapiLoopback::~WasapiLoopback() {
    Stop();
}

bool WasapiLoopback::Start() {
    if (m_isRunning) return true;

    m_isRunning = true;
    m_captureThread = std::thread(&WasapiLoopback::CaptureLoop, this);
    return true;
}

void WasapiLoopback::Stop() {
    if (m_isRunning) {
        m_isRunning = false;
        if (m_captureThread.joinable()) {
            m_captureThread.join();
        }
    }

    if (m_pAudioClient) {
        m_pAudioClient->Stop();
        m_pAudioClient.Reset();
    }
    m_pCaptureClient.Reset();

    if (m_pwfx) {
        CoTaskMemFree(m_pwfx);
        m_pwfx = nullptr;
    }
}

void WasapiLoopback::CaptureLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pEnumerator;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&pEnumerator)
    );
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    Microsoft::WRL::ComPtr<IMMDevice> pDevice;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(m_pAudioClient.GetAddressOf()));
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    hr = m_pAudioClient->GetMixFormat(&m_pwfx);
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    bool isFloat = false;
    if (m_pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (m_pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_pwfx);
        if (IsEqualGUID(pExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            isFloat = true;
        }
    }

    // 100ms buffer in 100ns units
    REFERENCE_TIME hnsRequestedDuration = 1000000;
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsRequestedDuration,
        0,
        m_pwfx,
        nullptr
    );
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    hr = m_pAudioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(m_pCaptureClient.GetAddressOf())
    );
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        m_isRunning = false;
        CoUninitialize();
        return;
    }

    std::vector<float> monoBuffer;
    monoBuffer.reserve(1024);

    while (m_isRunning) {
        UINT32 packetLength = 0;
        hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        while (packetLength > 0) {
            BYTE* pData = nullptr;
            UINT32 numFramesRead = 0;
            DWORD flags = 0;

            hr = m_pCaptureClient->GetBuffer(&pData, &numFramesRead, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            monoBuffer.clear();
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                monoBuffer.resize(numFramesRead, 0.0f);
            } else if (pData) {
                WORD channels = m_pwfx->nChannels;
                monoBuffer.resize(numFramesRead);

                if (isFloat && m_pwfx->wBitsPerSample == 32) {
                    const float* fData = reinterpret_cast<const float*>(pData);
                    for (UINT32 i = 0; i < numFramesRead; ++i) {
                        float sum = 0.0f;
                        for (WORD c = 0; c < channels; ++c) {
                            sum += fData[i * channels + c];
                        }
                        monoBuffer[i] = sum / channels;
                    }
                } else if (!isFloat && m_pwfx->wBitsPerSample == 16) {
                    const int16_t* sData = reinterpret_cast<const int16_t*>(pData);
                    for (UINT32 i = 0; i < numFramesRead; ++i) {
                        float sum = 0.0f;
                        for (WORD c = 0; c < channels; ++c) {
                            sum += sData[i * channels + c];
                        }
                        monoBuffer[i] = (sum / channels) / 32768.0f;
                    }
                }
            }

            m_analyzer.PushSamples(monoBuffer.data(), monoBuffer.size());
            m_pCaptureClient->ReleaseBuffer(numFramesRead);

            hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }

        // Run FFT update
        SpectrumData spec = m_analyzer.Process();
        {
            std::lock_guard<std::mutex> lock(m_spectrumMutex);
            m_latestSpectrum = spec;
        }

        // Sleep ~16ms for 60 FPS update rate
        Sleep(16);
    }

    CoUninitialize();
}

SpectrumData WasapiLoopback::GetSpectrum() {
    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    return m_latestSpectrum;
}

} // namespace MusicBar
