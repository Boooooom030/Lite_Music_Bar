#pragma once

#include <vector>
#include <array>
#include <complex>
#include <span>
#include <chrono>
#include <cstdint>
#include "core/Types.hpp"

namespace MusicBar {

class FftAnalyzer {
public:
    static constexpr size_t FFT_SIZE = 1024;
    static constexpr size_t MAX_FFT_SIZE = 4096;
    static constexpr size_t BAND_COUNT = SpectrumData::BAND_COUNT;

    FftAnalyzer();

    void ConfigureSampleRate(uint32_t sampleRate);
    size_t GetFftSize() const { return m_fftSize; }
    void PushSamples(const float* samples, size_t count);
    SpectrumData Process();

private:
    std::vector<float> m_inputBuffer;
    size_t m_samplesSinceProcess = 0;
    std::vector<float> m_hannWindow;
    std::vector<std::complex<float>> m_fftBuffer;
    std::vector<float> m_powerSpectrum;
    std::array<float, BAND_COUNT> m_currentBands{};
    std::array<float, BAND_COUNT + 1> m_bandFreqIndices{};
    std::array<float, BAND_COUNT> m_bandTilt{};
    size_t m_fftSize = FFT_SIZE;
    uint32_t m_sampleRate = 48000;
    float m_referenceMagnitude = 0.0f;
    std::chrono::steady_clock::time_point m_lastReferenceUpdate{};

    void PrecomputeHann();
    void PrecomputeBands();
    void ComputeFft(std::vector<std::complex<float>>& data);
};

} // namespace MusicBar
