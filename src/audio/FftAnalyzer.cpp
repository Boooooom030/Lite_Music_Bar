#include "FftAnalyzer.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace MusicBar {

FftAnalyzer::FftAnalyzer() {
    ConfigureSampleRate(48000);
}

void FftAnalyzer::ConfigureSampleRate(uint32_t sampleRate) {
    if (sampleRate == 0) sampleRate = 48000;
    size_t fftSize = FFT_SIZE;
    while (fftSize < MAX_FFT_SIZE &&
        static_cast<uint64_t>(fftSize) * 40 < sampleRate) {
        fftSize <<= 1;
    }
    if (m_sampleRate == sampleRate && m_fftSize == fftSize &&
        !m_hannWindow.empty()) return;
    m_sampleRate = sampleRate;
    m_fftSize = fftSize;
    m_hannWindow.resize(m_fftSize);
    m_fftBuffer.resize(m_fftSize);
    m_powerSpectrum.resize(m_fftSize / 2);
    m_inputBuffer.clear();
    m_inputBuffer.reserve(m_fftSize * 2);
    m_samplesSinceProcess = 0;
    m_currentBands.fill(0.0f);
    m_referenceMagnitude = 0.0f;
    m_lastReferenceUpdate = {};
    PrecomputeHann();
    PrecomputeBands();
}

void FftAnalyzer::PrecomputeHann() {
    constexpr float pi2 = static_cast<float>(std::numbers::pi * 2.0);
    for (size_t i = 0; i < m_fftSize; ++i) {
        m_hannWindow[i] = 0.5f * (1.0f -
            std::cos(pi2 * i / static_cast<float>(m_fftSize - 1)));
    }
}

void FftAnalyzer::PrecomputeBands() {
    const float binWidth = static_cast<float>(m_sampleRate) / m_fftSize;
    constexpr float minFreq = 45.0f;
    const float maxFreq = std::min(20000.0f,
        static_cast<float>(m_sampleRate) * 0.475f);

    for (size_t i = 0; i <= BAND_COUNT; ++i) {
        float f = minFreq * std::pow(maxFreq / minFreq, static_cast<float>(i) / BAND_COUNT);
        float binIdx = f / binWidth;
        m_bandFreqIndices[i] = std::clamp(binIdx, 1.0f,
            static_cast<float>(m_fftSize / 2 - 1));
    }
    for (size_t i = 0; i < BAND_COUNT; ++i) {
        const float centerHz = std::sqrt(m_bandFreqIndices[i] *
            m_bandFreqIndices[i + 1]) * binWidth;
        const float tiltDb = 3.0f * std::log2(centerHz / 1000.0f);
        m_bandTilt[i] = std::pow(10.0f, tiltDb / 20.0f);
    }
}

void FftAnalyzer::PushSamples(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    m_inputBuffer.insert(m_inputBuffer.end(), samples, samples + count);
    m_samplesSinceProcess += count;
    if (m_inputBuffer.size() > m_fftSize * 4) {
        // Keep the most recent samples
        m_inputBuffer.erase(m_inputBuffer.begin(),
            m_inputBuffer.end() - m_fftSize * 2);
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
    const auto decayBands = [&] {
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            m_currentBands[b] = std::max(0.0f,
                m_currentBands[b] * 0.82f - 0.01f);
            result.bands[b] = m_currentBands[b];
        }
    };
    const auto now = std::chrono::steady_clock::now();
    if (m_lastReferenceUpdate.time_since_epoch().count() != 0 &&
        m_referenceMagnitude > 0.0f) {
        const float seconds = std::chrono::duration<float>(
            now - m_lastReferenceUpdate).count();
        m_referenceMagnitude *= std::pow(10.0f,
            -12.0f * std::max(0.0f, seconds) / 20.0f);
        if (m_referenceMagnitude < 1e-7f) m_referenceMagnitude = 0.0f;
    }
    m_lastReferenceUpdate = now;

    if (m_inputBuffer.size() >= m_fftSize && m_samplesSinceProcess > 0) {
        m_samplesSinceProcess = 0;
        // The window and transform must use the actual endpoint sample rate.
        const size_t startOffset = m_inputBuffer.size() - m_fftSize;
        float meanSquare = 0.0f;
        for (size_t i = 0; i < m_fftSize; ++i) {
            float sample = m_inputBuffer[startOffset + i];
            meanSquare += sample * sample;
            float val = sample * m_hannWindow[i];
            m_fftBuffer[i] = std::complex<float>(val, 0.0f);
        }
        result.hasAudio = meanSquare / m_fftSize > 1e-8f;
        if (!result.hasAudio) {
            decayBands();
            return result;
        }

        ComputeFft(m_fftBuffer);
        for (size_t bin = 0; bin < m_powerSpectrum.size(); ++bin) {
            m_powerSpectrum[bin] = std::norm(m_fftBuffer[bin]);
        }

        // Integrate power across fractional bins so adjacent low bands do not
        // mirror the same FFT bin, including on 96 kHz output devices.
        std::array<float, BAND_COUNT> magnitudes{};
        float peakMagnitude = 0.0f;
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            const float low = m_bandFreqIndices[b];
            const float high = m_bandFreqIndices[b + 1];
            const int count = std::max(2,
                static_cast<int>(std::ceil((high - low) * 2.0f)) + 1);
            float power = 0.0f;
            for (int i = 0; i < count; ++i) {
                const float position = low + (high - low) *
                    static_cast<float>(i) / (count - 1);
                const size_t lower = std::clamp(static_cast<size_t>(position),
                    size_t{1}, m_powerSpectrum.size() - 2);
                const float fraction = std::clamp(position -
                    static_cast<float>(lower), 0.0f, 1.0f);
                power += m_powerSpectrum[lower] * (1.0f - fraction) +
                    m_powerSpectrum[lower + 1] * fraction;
            }
            const float normalizedMagnitude = std::sqrt(power / count) *
                (2.0f / m_fftSize) * m_bandTilt[b];
            magnitudes[b] = std::isfinite(normalizedMagnitude)
                ? normalizedMagnitude : 0.0f;
            peakMagnitude = std::max(peakMagnitude, magnitudes[b]);
        }
        m_referenceMagnitude = std::max(m_referenceMagnitude, peakMagnitude);

        // A moving peak reference keeps the shape visible at different system
        // volumes; the 40 dB window preserves valleys instead of hard clipping.
        for (size_t b = 0; b < BAND_COUNT; ++b) {
            float target = 0.0f;
            if (magnitudes[b] > 0.0f && m_referenceMagnitude > 0.0f) {
                const float relativeDb = 20.0f * std::log10(
                    magnitudes[b] / m_referenceMagnitude);
                const float level = std::clamp((relativeDb + 40.0f) / 40.0f,
                    0.0f, 1.0f);
                target = std::pow(level, 1.5f);
            }
            const float rate = target > m_currentBands[b] ? 0.60f : 0.24f;
            m_currentBands[b] += (target - m_currentBands[b]) * rate;
            if (m_currentBands[b] < 0.002f) m_currentBands[b] = 0.0f;
            result.bands[b] = m_currentBands[b];
        }
    } else {
        // Audio idle or no input: decay to 0
        decayBands();
    }

    return result;
}

} // namespace MusicBar
