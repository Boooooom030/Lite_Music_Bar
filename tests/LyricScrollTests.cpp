#include "ui/LyricScroll.hpp"
#include <cmath>

int main() {
    constexpr float textWidth = 960.0f;
    constexpr float viewportWidth = 240.0f;
    constexpr float end = textWidth - viewportWidth;

    // A timed LRC line starts at the beginning and advances during playback.
    if (MusicBar::LyricScrollTarget(textWidth, viewportWidth, 0.0f, 0.0f)
        != 0.0f) return 1;
    const float halfway = MusicBar::LyricScrollTarget(
        textWidth, viewportWidth, textWidth * 0.5f, 0.5f);
    if (!(halfway > 0.0f && halfway < end)) return 2;
    if (std::abs(MusicBar::LyricScrollTarget(
        textWidth, viewportWidth, textWidth * 0.85f, 0.85f) - end)
        > 0.001f) return 3;

    // The tail remains readable even if syllable timing ends early.
    if (std::abs(MusicBar::LyricScrollTarget(
        textWidth, viewportWidth, textWidth * 0.6f, 0.95f) - end)
        > 0.001f) return 4;
    if (MusicBar::LyricScrollTarget(180.0f, viewportWidth,
        180.0f, 1.0f) != 0.0f) return 5;
    return 0;
}
