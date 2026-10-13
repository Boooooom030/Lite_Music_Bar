#include "lyrics/LyricsParser.hpp"
#include "lyrics/LyricsTimeline.hpp"
#include "lyrics/providers/NetEaseLyricsResponse.hpp"
#include <cmath>

int main() {
    const auto yrc = MusicBar::LyricsParser::Parse(
        "[12460,1080](12460,216,0)All (12676,210,0)the ");
    if (yrc.lines.size() != 1 || yrc.lines[0].syllables.size() != 2 ||
        yrc.lines[0].syllables[0].startMs != 12460 ||
        yrc.lines[0].syllables[1].startMs != 12676) return 1;

    const auto relative = MusicBar::LyricsParser::Parse(
        "[12460,1080]<0,216,0>All <216,210,0>the ");
    if (relative.lines.size() != 1 || relative.lines[0].syllables.size() != 2 ||
        relative.lines[0].syllables[0].startMs != 12460 ||
        relative.lines[0].syllables[1].startMs != 12676) return 2;
    // Blank timestamp markers end the sung line, but a long interlude keeps
    // that lyric visible until the next nonblank line starts.
    auto interlude = MusicBar::LyricsParser::Parse(
        "[00:00.00]\n[00:05.00]First line\n[00:09.00]\n"
        "[00:15.00]  \n[00:30.00]Next line\n[00:34.00]\n");
    if (interlude.lines.size() != 6 || interlude.lines[1].durationMs != 4000) return 3;
    MusicBar::UpdateLyricPosition(interlude, 4999);
    if (interlude.activeIndex != -1) return 4;
    MusicBar::UpdateLyricPosition(interlude, 5000);
    if (interlude.activeIndex != 1 || interlude.syllableProgress != 0.0f) return 5;
    for (const int64_t position : {6000, 7000, 8000}) {
        MusicBar::UpdateLyricPosition(interlude, position);
        const float expected = static_cast<float>(position - 5000) / 4000.0f;
        if (interlude.activeIndex != 1 ||
            std::abs(interlude.syllableProgress - expected) > 0.001f) return 12;
    }
    for (const int64_t position : {9000, 15000, 29999}) {
        MusicBar::UpdateLyricPosition(interlude, position);
        if (interlude.activeIndex != 1 || interlude.syllableProgress != 1.0f) return 6;
    }
    MusicBar::UpdateLyricPosition(interlude, 30000);
    if (interlude.activeIndex != 4 || interlude.syllableProgress != 0.0f) return 7;
    MusicBar::UpdateLyricPosition(interlude, 32000);
    if (interlude.activeIndex != 4 || interlude.syllableProgress != 0.5f) return 13;
    MusicBar::UpdateLyricPosition(interlude, 60000);
    if (interlude.activeIndex != 4 || interlude.syllableProgress != 1.0f) return 8;
    MusicBar::UpdateLyricPosition(interlude, 1000); // Seeking back clears the old line.
    if (interlude.activeIndex != -1 || interlude.activeSyllableIndex != -1 ||
        interlude.syllableProgress != 0.0f) return 9;

    auto karaokeGap = MusicBar::LyricsParser::Parse(
        "[5000,1000](5000,500,0)First (5500,500,0)line\n"
        "[30000,1000](30000,1000,0)Next");
    MusicBar::UpdateLyricPosition(karaokeGap, 20000);
    if (karaokeGap.activeIndex != 0 || karaokeGap.activeSyllableIndex != 1 ||
        karaokeGap.syllableProgress != 1.0f) return 10;
    karaokeGap = {}; // A new song must not inherit the previous lyric.
    MusicBar::UpdateLyricPosition(karaokeGap, 20000);
    if (karaokeGap.activeIndex != -1 || karaokeGap.activeSyllableIndex != -1) return 11;

    // Actual shape returned for NetEase track 3339531841 (雪色 / 消失).
    auto instrumental = MusicBar::ParseNetEaseLyricsResponse(
        R"({"sgc":false,"sfy":false,"qfy":false,"needDesc":true,"pureMusic":true,
            "lrc":{"version":1,"lyric":""},"code":200,"briefDesc":null})");
    if (!instrumental || !instrumental->isInstrumental || !instrumental->lines.empty()) return 14;
    MusicBar::UpdateLyricPosition(*instrumental, 100000);
    if (instrumental->activeIndex != -1 || instrumental->syllableProgress != 0.0f) return 15;
    if (MusicBar::ParseNetEaseLyricsResponse(R"({"code":200,"lrc":{"lyric":""}})") ||
        MusicBar::ParseNetEaseLyricsResponse(R"({"code":500,"pureMusic":true})") ||
        MusicBar::ParseNetEaseLyricsResponse(R"({"code":200,"pureMusic":"true"})")) return 16;
    const auto lrcFallback = MusicBar::ParseNetEaseLyricsResponse(
        R"({"code":200,"yrc":{"lyric":""},"lrc":{"lyric":"[00:05.00]Singing"}})");
    if (!lrcFallback || lrcFallback->isInstrumental || lrcFallback->lines.size() != 1 ||
        lrcFallback->lines[0].text != L"Singing") return 17;
    return 0;
}
