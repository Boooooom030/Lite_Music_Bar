#pragma once

#include <vector>
#include <array>
#include <complex>
#include <span>
#include "core/Types.hpp"

namespace MusicBar {

class FftAnalyzer {
public:
    static constexpr size_t FFT_SIZE = 1024;
    static constexpr size_t BAND_COUNT = SpectrumData::BAND_COUNT;

    FftAnalyzer();

    void PushSamples(const float* samples, size_t count);
    SpectrumData Process();

private:
    std::vector<float> m_inputBuffer;
    std::vector<float> m_hannWindow;
    std::vector<std::complex<float>> m_fftBuffer;
    std::array<float, BAND_COUNT> m_currentBands{};
    std::array<float, BAND_COUNT + 1> m_bandFreqIndices{};

    void PrecomputeHann();
    void PrecomputeBands();
    void ComputeFft(std::vector<std::complex<float>>& data);
};

} // namespace MusicBar
