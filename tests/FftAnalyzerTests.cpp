#include "audio/FftAnalyzer.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {

std::vector<float> Sine(size_t count, float amplitude, float frequency,
    float sampleRate) {
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; ++i) {
        samples[i] = amplitude * std::sin(2.0f * std::numbers::pi_v<float> *
            frequency * static_cast<float>(i) / sampleRate);
    }
    return samples;
}

} // namespace

int main() {
    MusicBar::FftAnalyzer analyzer;
    if (analyzer.GetFftSize() != 2048) return 1;
    auto samples = Sine(analyzer.GetFftSize(), 0.25f, 440.0f, 48000.0f);
    analyzer.PushSamples(samples.data(), samples.size());
    const auto active = analyzer.Process();
    const float peak = *std::max_element(active.bands.begin(),
        active.bands.end());
    if (!active.hasAudio || peak < 0.4f) return 2;
    const auto valleys = std::count_if(active.bands.begin(), active.bands.end(),
        [peak](float level) { return level < peak * 0.5f; });
    if (valleys < 6) return 3;

    MusicBar::FftAnalyzer quietAnalyzer;
    auto quietSamples = Sine(quietAnalyzer.GetFftSize(), 0.025f,
        440.0f, 48000.0f);
    quietAnalyzer.PushSamples(quietSamples.data(), quietSamples.size());
    const auto quiet = quietAnalyzer.Process();
    if (!quiet.hasAudio) return 4;
    for (size_t i = 0; i < active.bands.size(); ++i) {
        if (std::abs(active.bands[i] - quiet.bands[i]) > 0.08f) return 5;
    }

    MusicBar::FftAnalyzer nearSilentAnalyzer;
    auto nearSilent = Sine(nearSilentAnalyzer.GetFftSize(), 0.00005f,
        440.0f, 48000.0f);
    nearSilentAnalyzer.PushSamples(nearSilent.data(), nearSilent.size());
    const auto silence = nearSilentAnalyzer.Process();
    if (silence.hasAudio || *std::max_element(silence.bands.begin(),
        silence.bands.end()) > 0.01f) return 11;

    MusicBar::FftAnalyzer musicAnalyzer;
    std::vector<float> mixture(musicAnalyzer.GetFftSize(), 0.0f);
    constexpr float frequencies[] = {80.0f, 220.0f, 520.0f,
        1400.0f, 3800.0f, 9200.0f};
    constexpr float amplitudes[] = {0.20f, 0.15f, 0.10f,
        0.08f, 0.05f, 0.03f};
    for (size_t tone = 0; tone < 6; ++tone) {
        const auto component = Sine(mixture.size(), amplitudes[tone],
            frequencies[tone], 48000.0f);
        for (size_t i = 0; i < mixture.size(); ++i) {
            mixture[i] += component[i];
        }
    }
    musicAnalyzer.PushSamples(mixture.data(), mixture.size());
    const auto music = musicAnalyzer.Process();
    const auto softerBands = std::count_if(music.bands.begin(), music.bands.end(),
        [](float level) { return level < 0.5f; });
    if (!music.hasAudio || softerBands < 4) return 10;

    for (int i = 0; i < 60; ++i) {
        const auto idle = analyzer.Process();
        if (idle.hasAudio) return 6;
        if (i == 59 &&
            *std::max_element(idle.bands.begin(), idle.bands.end()) >= 0.01f) {
            return 7;
        }
    }

    analyzer.ConfigureSampleRate(96000);
    if (analyzer.GetFftSize() != 4096) return 8;
    samples = Sine(analyzer.GetFftSize(), 0.25f, 100.0f, 96000.0f);
    analyzer.PushSamples(samples.data(), samples.size());
    if (!analyzer.Process().hasAudio) return 9;
    return 0;
}
