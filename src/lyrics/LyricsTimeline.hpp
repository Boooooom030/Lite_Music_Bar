#pragma once

#include "core/Types.hpp"
#include <algorithm>
#include <iterator>

namespace MusicBar {

// Playback state belongs to the UI's loaded document. Blank LRC markers still
// delimit singing durations, but they do not replace the last visible lyric.
inline void UpdateLyricPosition(LyricDocument& document, int64_t positionMs) {
    document.activeIndex = -1;
    document.activeSyllableIndex = -1;
    document.syllableProgress = 0.0f;
    if (document.isInstrumental) return;

    auto lineIt = std::upper_bound(document.lines.begin(), document.lines.end(),
        positionMs, [](int64_t position, const LyricLine& line) {
            return position < line.startMs;
        });
    while (lineIt != document.lines.begin()) {
        --lineIt;
        if (lineIt->text.find_first_not_of(L" \t\r\n\u3000") != std::wstring::npos) {
            document.activeIndex = static_cast<int>(
                std::distance(document.lines.begin(), lineIt));
            break;
        }
    }
    if (document.activeIndex < 0) return;

    const auto& line = *lineIt;
    if (line.HasKaraoke()) {
        for (size_t i = 0; i < line.syllables.size(); ++i) {
            const auto& syllable = line.syllables[i];
            if (positionMs < syllable.startMs) break;
            document.activeSyllableIndex = static_cast<int>(i);
            document.syllableProgress = syllable.durationMs > 0
                ? std::clamp(static_cast<float>(positionMs - syllable.startMs) /
                    static_cast<float>(syllable.durationMs), 0.0f, 1.0f)
                : 1.0f;
        }
    } else if (line.durationMs > 0) {
        document.syllableProgress = std::clamp(
            static_cast<float>(positionMs - line.startMs) /
                static_cast<float>(line.durationMs), 0.0f, 1.0f);
    }
}

} // namespace MusicBar
