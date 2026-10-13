#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <chrono>

namespace MusicBar {

struct MediaTrackInfo {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring sourceAppId;
    uint32_t sourceProcessId = 0;
    std::vector<uint8_t> thumbnailBytes;
    bool hasSession = false;
    bool isPlaying = false;
    int64_t positionMs = 0;
    int64_t durationMs = 0;
    std::chrono::steady_clock::time_point lastUpdatedTime = std::chrono::steady_clock::now();

    int64_t GetCurrentInterpolatedPositionMs() const {
        if (!isPlaying || durationMs <= 0) return positionMs;
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdatedTime).count();
        int64_t current = positionMs + elapsedMs;
        if (current > durationMs) current = durationMs;
        return current;
    }
};

struct LyricSyllable {
    int64_t startMs = 0;
    int64_t durationMs = 0;
    std::wstring text;
};

struct LyricLine {
    int64_t startMs = 0;
    int64_t durationMs = 0;
    std::wstring text;
    std::wstring translation;
    std::vector<LyricSyllable> syllables;

    bool HasKaraoke() const {
        return !syllables.empty();
    }
};

struct LyricDocument {
    std::wstring title;
    std::wstring artist;
    std::vector<LyricLine> lines;
    int activeIndex = -1;
    int activeSyllableIndex = -1;
    float syllableProgress = 0.0f; // 0.0 to 1.0 inside current syllable/line
    bool isInstrumental = false; // Confirmed by a provider, not inferred from empty lyrics.
};

enum class LyricsSource {
    Auto = 0,
    NetEase,
    QQMusic,
    Kugou,
    Soda,
    Lrclib
};

struct SpectrumData {
    static constexpr size_t BAND_COUNT = 16;
    std::array<float, BAND_COUNT> bands{};
    bool hasAudio = false;
};

enum class ButtonType {
    None,
    OutputDevice,
    Prev,
    PlayPause,
    Next
};

} // namespace MusicBar
