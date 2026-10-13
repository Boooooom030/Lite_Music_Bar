#pragma once

#include "core/Types.hpp"
#include <algorithm>
#include <cstdint>

namespace MusicBar {

// A lyric file written for another edit of the track (live, extended, remaster,
// cover) is the usual reason the lyrics run late: its timestamps reach past the
// end of the track that is playing. The player reports the real duration, so a
// file that cannot fit inside it is treated as a mismatch and the next provider
// is asked instead.
inline constexpr int64_t kLyricsDurationSlackMs = 10000;

inline int64_t LyricsLastTimestampMs(const LyricDocument& document) {
    if (document.lines.empty()) return 0;
    const auto& last = document.lines.back();
    return last.startMs + std::max<int64_t>(last.durationMs, 0);
}

inline bool LyricsFitTrackDuration(const LyricDocument& document, int64_t durationMs) {
    if (document.isInstrumental || durationMs <= 0 || document.lines.empty()) return true;
    return LyricsLastTimestampMs(document) <= durationMs + kLyricsDurationSlackMs;
}

} // namespace MusicBar
