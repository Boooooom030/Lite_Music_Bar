#include "WasapiLoopback.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <ksmedia.h>
#include <audioclientactivationparams.h>
#include <wrl/implements.h>

namespace MusicBar {

namespace {

using Microsoft::WRL::ComPtr;

class ActivationHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler() : completed(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() { if (completed) CloseHandle(completed); }

    STDMETHOD(ActivateCompleted)(
        IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> activated;
        HRESULT activation = E_FAIL;
        result = operation->GetActivateResult(&activation, &activated);
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = activated.As(&client);
        if (completed) SetEvent(completed);
        Release(); // Keep the handler alive if the caller times out first.
        return S_OK;
    }

    HANDLE completed = nullptr;
    HRESULT result = E_FAIL;
    ComPtr<IAudioClient> client;
};

bool SupportsProcessLoopback() {
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto getVersion = ntdll ? reinterpret_cast<GetVersion>(
        GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return getVersion && getVersion(&version) == 0 &&
        version.dwMajorVersion >= 10 && version.dwBuildNumber >= 20348;
}

HRESULT ActivateProcessLoopback(DWORD processId, ComPtr<IAudioClient>& client) {
    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    if (!handler || !handler->completed) return E_OUTOFMEMORY;
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    parameters.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    parameters.ProcessLoopbackParams.TargetProcessId = processId;
    parameters.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT variant{};
    variant.vt = VT_BLOB;
    variant.blob.cbSize = sizeof(parameters);
    variant.blob.pBlobData = reinterpret_cast<BYTE*>(&parameters);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    handler->AddRef(); // Balanced by ActivateCompleted, even after a timeout.
    const HRESULT started = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
        &variant, handler.Get(), &operation);
    if (FAILED(started)) {
        handler->Release();
        return started;
    }
    if (WaitForSingleObject(handler->completed, 5000) != WAIT_OBJECT_0) {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (SUCCEEDED(handler->result)) client = std::move(handler->client);
    return handler->result;
}

enum class SampleFormat { Unknown, Float32, Float64, Pcm8, Pcm16, Pcm24, Pcm32 };

SampleFormat GetSampleFormat(const WAVEFORMATEX& format) {
    bool floatingPoint = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool pcm = format.wFormatTag == WAVE_FORMAT_PCM;
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        floatingPoint = IsEqualGUID(extended.SubFormat,
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
        pcm = IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != FALSE;
    }
    if (floatingPoint && format.wBitsPerSample == 32) return SampleFormat::Float32;
    if (floatingPoint && format.wBitsPerSample == 64) return SampleFormat::Float64;
    if (pcm && format.wBitsPerSample == 8) return SampleFormat::Pcm8;
    if (pcm && format.wBitsPerSample == 16) return SampleFormat::Pcm16;
    if (pcm && format.wBitsPerSample == 24) return SampleFormat::Pcm24;
    if (pcm && format.wBitsPerSample == 32) return SampleFormat::Pcm32;
    return SampleFormat::Unknown;
}

float ReadSample(const BYTE* sample, SampleFormat format) {
    switch (format) {
    case SampleFormat::Float32: {
        float value = 0.0f;
        std::memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    case SampleFormat::Float64: {
        double value = 0.0;
        std::memcpy(&value, sample, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
    }
    case SampleFormat::Pcm8:
        return (static_cast<int>(sample[0]) - 128) / 128.0f;
    case SampleFormat::Pcm16: {
        int16_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return value / 32768.0f;
    }
    case SampleFormat::Pcm24: {
        int32_t value = static_cast<int32_t>(sample[0]) |
            (static_cast<int32_t>(sample[1]) << 8) |
            (static_cast<int32_t>(sample[2]) << 16);
        if (value & 0x800000) value |= ~0xFFFFFF;
        return value / 8388608.0f;
    }
    case SampleFormat::Pcm32: {
        int32_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value / 2147483648.0);
    }
    default:
        return 0.0f;
    }
}

std::wstring DeviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (!device) return {};
    const HRESULT hr = device->GetId(&id);
    if (FAILED(hr) || !id) {
        CoTaskMemFree(id);
        return {};
    }
    std::wstring result{id};
    CoTaskMemFree(id);
    return result;
}

} // namespace

WasapiLoopback::WasapiLoopback() = default;

WasapiLoopback::~WasapiLoopback() {
    Stop();
}

bool WasapiLoopback::Start() {
    if (m_isRunning.exchange(true)) return true;
    if (m_captureThread.joinable()) m_captureThread.join();
    m_captureThread = std::thread(&WasapiLoopback::CaptureLoop, this);
    return true;
}

void WasapiLoopback::Stop() {
    m_isRunning = false;
    if (m_captureThread.joinable()) m_captureThread.join();
}

void WasapiLoopback::SetCaptureMode(AudioCaptureMode mode) {
    if (m_captureMode.exchange(mode) == mode) return;
    ++m_configGeneration;
    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    m_latestSpectrum = {};
}

void WasapiLoopback::SetTargetProcess(DWORD processId) {
    if (m_targetProcessId.exchange(processId) == processId) return;
    ++m_configGeneration;
    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    m_latestSpectrum = {};
}

void WasapiLoopback::CaptureLoop() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) {
        m_isRunning = false;
        return;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    const bool processLoopbackSupported = SupportsProcessLoopback();
    auto disconnect = [this] {
        if (m_pAudioClient) m_pAudioClient->Stop();
        m_pCaptureClient.Reset();
        m_pAudioClient.Reset();
        if (m_pwfx) {
            CoTaskMemFree(m_pwfx);
            m_pwfx = nullptr;
        }
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        m_latestSpectrum = {};
    };

    while (m_isRunning) {
        const uint64_t generation = m_configGeneration.load();
        const bool processCapture = processLoopbackSupported &&
            m_captureMode.load() == AudioCaptureMode::PlayerProcess;
        const DWORD targetProcess = m_targetProcessId.load();
        if (processCapture && !targetProcess) {
            disconnect();
            Sleep(100);
            continue;
        }
        Microsoft::WRL::ComPtr<IMMDevice> device;
        std::wstring endpointId;
        HRESULT hr = S_OK;
        if (processCapture) {
            hr = ActivateProcessLoopback(targetProcess, m_pAudioClient);
        } else {
            if (!enumerator) hr = CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator));
            if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(
                eRender, eMultimedia, &device);
            if (SUCCEEDED(hr)) {
                endpointId = DeviceId(device.Get());
                hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                    nullptr, reinterpret_cast<void**>(
                        m_pAudioClient.GetAddressOf()));
            }
            if (FAILED(hr)) enumerator.Reset();
        }
        if (FAILED(hr)) {
            disconnect();
            Sleep(250);
            continue;
        }
        if (processCapture) {
            m_pwfx = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
            if (m_pwfx) {
                *m_pwfx = {};
                m_pwfx->wFormatTag = WAVE_FORMAT_PCM;
                m_pwfx->nChannels = 2;
                m_pwfx->nSamplesPerSec = 44100;
                m_pwfx->wBitsPerSample = 16;
                m_pwfx->nBlockAlign = 4;
                m_pwfx->nAvgBytesPerSec = 44100 * 4;
            } else {
                hr = E_OUTOFMEMORY;
            }
        } else {
            hr = m_pAudioClient->GetMixFormat(&m_pwfx);
        }
        if (FAILED(hr) || !m_pwfx) {
            disconnect();
            Sleep(250);
            continue;
        }

        const SampleFormat sampleFormat = GetSampleFormat(*m_pwfx);
        const WORD bytesPerSample = m_pwfx->wBitsPerSample / 8;
        const WORD channels = m_pwfx->nChannels;
        if (sampleFormat == SampleFormat::Unknown || channels == 0 ||
            bytesPerSample == 0 ||
            m_pwfx->nBlockAlign < channels * bytesPerSample) {
            disconnect();
            Sleep(250);
            continue;
        }

        m_analyzer = FftAnalyzer{};
        m_analyzer.ConfigureSampleRate(m_pwfx->nSamplesPerSec);
        hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK |
                (processCapture ? AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM : 0),
            processCapture ? 0 : 1000000, 0, m_pwfx, nullptr);
        if (SUCCEEDED(hr)) {
            hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient),
                reinterpret_cast<void**>(m_pCaptureClient.GetAddressOf()));
        }
        if (SUCCEEDED(hr)) hr = m_pAudioClient->Start();
        if (FAILED(hr)) {
            disconnect();
            Sleep(250);
            continue;
        }

        std::vector<float> monoBuffer;
        auto nextEndpointCheck = std::chrono::steady_clock::now() +
            std::chrono::seconds(1);
        while (m_isRunning && m_configGeneration.load() == generation) {
            UINT32 packetLength = 0;
            hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;

            while (packetLength > 0 && m_isRunning &&
                m_configGeneration.load() == generation) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = m_pCaptureClient->GetBuffer(&data, &frames, &flags,
                    nullptr, nullptr);
                if (FAILED(hr)) break;

                monoBuffer.assign(frames, 0.0f);
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    for (UINT32 frame = 0; frame < frames; ++frame) {
                        const BYTE* frameData = data +
                            static_cast<size_t>(frame) * m_pwfx->nBlockAlign;
                        float sum = 0.0f;
                        for (WORD channel = 0; channel < channels; ++channel) {
                            sum += ReadSample(frameData +
                                static_cast<size_t>(channel) * bytesPerSample,
                                sampleFormat);
                        }
                        monoBuffer[frame] = sum / channels;
                    }
                }
                m_analyzer.PushSamples(monoBuffer.data(), monoBuffer.size());
                m_pCaptureClient->ReleaseBuffer(frames);
                hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) break;
            }
            if (FAILED(hr)) break;

            const SpectrumData spectrum = m_analyzer.Process();
            {
                std::lock_guard<std::mutex> lock(m_spectrumMutex);
                m_latestSpectrum = spectrum;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!processCapture && now >= nextEndpointCheck) {
                nextEndpointCheck = now + std::chrono::seconds(1);
                Microsoft::WRL::ComPtr<IMMDevice> current;
                if (FAILED(enumerator->GetDefaultAudioEndpoint(
                    eRender, eMultimedia, &current)) ||
                    (!endpointId.empty() && DeviceId(current.Get()) != endpointId)) {
                    break;
                }
            }
            Sleep(16);
        }

        disconnect();
        if (m_isRunning) Sleep(200);
    }

    disconnect();
    CoUninitialize();
}

SpectrumData WasapiLoopback::GetSpectrum() {
    std::lock_guard<std::mutex> lock(m_spectrumMutex);
    return m_latestSpectrum;
}

} // namespace MusicBar
