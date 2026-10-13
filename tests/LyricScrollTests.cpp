#include "ui/LyricScroll.hpp"
#include "ui/LyricTransition.hpp"
#include "lyrics/LyricsTimeline.hpp"
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

    MusicBar::LyricDocument lyrics{};
    lyrics.lines = {{5000, 4000, L"First"}, {9000, 21000, L""},
        {15000, 15000, L" \t"}, {30000, 4000, L"Next"}};
    auto intro = lyrics;
    intro.lines.insert(intro.lines.begin(), {0, 5000, L" \t\u3000"});
    MusicBar::UpdateLyricPosition(intro, 0);
    if (MusicBar::LyricDisplayIndex(intro) != 1 || intro.activeIndex != -1 ||
        intro.syllableProgress != 0.0f || MusicBar::ResolveLyricTransition(intro, 0).nextIndex != -1)
        return 12;
    MusicBar::UpdateLyricPosition(intro, 5000);
    if (MusicBar::LyricDisplayIndex(intro) != 1 || intro.activeIndex != 1) return 13;
    intro = {};
    intro.lines = {{0, 5000, L" \t"}};
    if (MusicBar::LyricDisplayIndex(intro) != -1) return 14;
    intro.lines = {{5000, 4000, L"First"}};
    intro.isInstrumental = true;
    if (MusicBar::LyricDisplayIndex(intro) != -1) return 15;
    MusicBar::UpdateLyricPosition(lyrics, 29000);
    if (MusicBar::ResolveLyricTransition(lyrics, 29000).nextIndex != -1) return 6;
    const auto half = MusicBar::ResolveLyricTransition(lyrics, 29910);
    if (half.nextIndex != 3 || std::abs(half.progress - 0.5f) > 0.001f) return 7;
    MusicBar::UpdateLyricPosition(lyrics, 30000);
    if (lyrics.activeIndex != 3 || MusicBar::ResolveLyricTransition(lyrics, 30000).nextIndex != -1)
        return 8;
    MusicBar::UpdateLyricPosition(lyrics, 6000); // A seek must not keep an outgoing line.
    if (MusicBar::ResolveLyricTransition(lyrics, 6000).nextIndex != -1) return 9;
    lyrics.lines = {{0, 100, L"One"}, {100, 100, L"Two"}, {200, 100, L"Three"}};
    MusicBar::UpdateLyricPosition(lyrics, 25);
    if (MusicBar::ResolveLyricTransition(lyrics, 25).nextIndex != -1) return 10;
    const auto fast = MusicBar::ResolveLyricTransition(lyrics, 75);
    if (fast.nextIndex != 1 || fast.progress != 0.5f) return 11;
    return 0;
}
