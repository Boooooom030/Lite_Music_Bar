#include "FftAnalyzer.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace MusicBar {

FftAnalyzer::FftAnalyzer() {
    m_hannWindow.resize(FFT_SIZE);
    m_fftBuffer.resize(FFT_SIZE);
    m_inputBuffer.reserve(FFT_SIZE * 2);

    PrecomputeHann();
    PrecomputeBands();
}

void FftAnalyzer::PrecomputeHann() {
    constexpr float pi2 = static_cast<float>(std::numbers::pi * 2.0);
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        m_hannWindow[i] = 0.5f * (1.0f - std::cos(pi2 * i / static_cast<float>(FFT_SIZE - 1)));
    }
}

void FftAnalyzer::PrecomputeBands() {
    // 16 bands logarithmic distribution from ~40 Hz to ~16 kHz
    constexpr float sampleRate = 48000.0f;
    constexpr float binWidth = sampleRate / FFT_SIZE; // ~46.875 Hz per bin
    constexpr float minFreq = 40.0f;
    constexpr float maxFreq = 16000.0f;

    for (size_t i = 0; i <= BAND_COUNT; ++i) {
        float f = minFreq * std::pow(maxFreq / minFreq, static_cast<float>(i) / BAND_COUNT);
        float binIdx = f / binWidth;
        m_bandFreqIndices[i] = std::clamp(binIdx, 1.0f, static_cast<float>(FFT_SIZE / 2));
    }
}

void FftAnalyzer::PushSamples(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    m_inputBuffer.insert(m_inputBuffer.end(), samples, samples + count);
    if (m_inputBuffer.size() > FFT_SIZE * 4) {
        // Keep the most recent samples
        m_inputBuffer.erase(m_inputBuffer.begin(), m_inputBuffer.end() - FFT_SIZE * 2);
    }
}

void FftAnalyzer::ComputeFft(std::vector<std::complex<float>>& data) {
    size_t n = data.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(data[i], data[j]);
        }
        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Cooley-Tukey butterflies
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = static_cast<float>(-2.0 * std::numbers::pi / len);
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = data[i + k];
                std::complex<float> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

SpectrumData FftAnalyzer::Process() {
    SpectrumData result{};

    if (m_inputBuffer.size() >= FFT_SIZE) {
        // Copy latest FFT_SIZE samples with Hann windowing
        size_t startOffset = m_inputBuffer.size() - FFT_SIZE;
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            float val = m_inputBuffer[startOffset + i] * m_hannWindow[i];
            m_fftBuffer[i] = std::complex<float>(val, 0.0f);
        }

        ComputeFft(m_fftBuffer);

        // Calculate magnitude and group into 16 bands
        std::array<float, BAND_COUNT> rawBands{};
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            size_t startBin = static_cast<size_t>(m_bandFreqIndices[b]);
            size_t endBin = static_cast<size_t>(m_bandFreqIndices[b + 1]);
            if (endBin <= startBin) endBin = startBin + 1;

            float sum = 0.0f;
            for (size_t bin = startBin; bin < endBin && bin < FFT_SIZE / 2; ++bin) {
                float mag = std::abs(m_fftBuffer[bin]);
                sum += mag;
            }
            float avg = sum / (endBin - startBin);

            // Convert to dB-like scale with frequency compensation
            // Higher frequencies usually have lower energy, so apply mild tilt
            float tilt = 1.0f + static_cast<float>(b) * 0.12f;
            float normalized = (avg * 8.0f * tilt);
            rawBands[b] = std::clamp(normalized, 0.0f, 1.0f);
        }

        // Apply attack/decay physics
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            if (rawBands[b] > m_currentBands[b]) {
                // Instant attack
                m_currentBands[b] = rawBands[b];
            } else {
                // Smooth gravitational decay
                m_currentBands[b] = std::max(0.0f, m_currentBands[b] * 0.85f - 0.005f);
            }
            result.bands[b] = m_currentBands[b];
        }
    } else {
        // Audio idle or no input: decay to 0
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            m_currentBands[b] = std::max(0.0f, m_currentBands[b] * 0.82f - 0.01f);
            result.bands[b] = m_currentBands[b];
        }
    }

    return result;
}

} // namespace MusicBar
