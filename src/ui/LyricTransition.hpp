#pragma once

#include "core/Types.hpp"
#include <algorithm>

namespace MusicBar {

// Keep the first nonblank line readable during the intro without selecting a
// future line on the singing timeline or advancing its karaoke progress.
inline int LyricDisplayIndex(const LyricDocument& lyrics) {
    if (lyrics.isInstrumental) return -1;
    if (lyrics.activeIndex >= 0 && lyrics.activeIndex < static_cast<int>(lyrics.lines.size()))
        return lyrics.activeIndex;
    const auto first = std::find_if(lyrics.lines.begin(), lyrics.lines.end(), [](const LyricLine& line) {
        return line.text.find_first_not_of(L" \t\r\n\u3000") != std::wstring::npos;
    });
    return first == lyrics.lines.end() ? -1 : static_cast<int>(first - lyrics.lines.begin());
}

struct LyricTransition {
    int nextIndex = -1;
    float progress = 0.0f;
};

// Finish the slide at the next sung timestamp, rather than starting a fade
// there. Playback position anchors it, so pause freezes and seeks align it.
inline LyricTransition ResolveLyricTransition(const LyricDocument& lyrics, int64_t positionMs) {
    if (lyrics.activeIndex < 0 || lyrics.activeIndex >= static_cast<int>(lyrics.lines.size())) return {};
    const auto& current = lyrics.lines[lyrics.activeIndex];
    for (int index = lyrics.activeIndex + 1; index < static_cast<int>(lyrics.lines.size()); ++index) {
        const auto& next = lyrics.lines[index];
        if (next.text.find_first_not_of(L" \t\r\n\u3000") == std::wstring::npos ||
            next.startMs <= current.startMs) continue;
        // Rapid lines must still spend most of their interval in place.
        const int64_t leadMs = std::min<int64_t>(180, (next.startMs - current.startMs) / 2);
        if (leadMs <= 0 || positionMs < next.startMs - leadMs) return {};
        const float progress = std::clamp(
            static_cast<float>(positionMs - (next.startMs - leadMs)) /
                static_cast<float>(leadMs), 0.0f, 1.0f);
        return {index, progress};
    }
    return {};
}

} // namespace MusicBar
