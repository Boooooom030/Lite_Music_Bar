#include "lyrics/LyricsFit.hpp"
#include "lyrics/LyricsParser.hpp"

namespace {

MusicBar::LyricDocument MakeDocument(int64_t startMs, int64_t durationMs) {
    MusicBar::LyricDocument document{};
    MusicBar::LyricLine line{};
    line.startMs = startMs;
    line.durationMs = durationMs;
    line.text = L"line";
    document.lines.push_back(line);
    return document;
}

} // namespace

int main() {
    using MusicBar::LyricsFitTrackDuration;

    // A file that ends inside the track fits, with a small tail allowed for the
    // outro.
    if (!LyricsFitTrackDuration(MakeDocument(154777, 1544), 206475)) return 1;
    if (!LyricsFitTrackDuration(MakeDocument(206000, 4000), 206475)) return 2;
    // Ten seconds of slack is allowed, one second more is not.
    if (!LyricsFitTrackDuration(MakeDocument(210000, 6000), 206475)) return 3;
    if (LyricsFitTrackDuration(MakeDocument(211000, 6000), 206475)) return 4;

    // The regression this guards: a lyric file for a longer edit of the track
    // (measured: last line 250.3 s against a 206.5 s track) must be rejected.
    if (LyricsFitTrackDuration(MakeDocument(250290, 4000), 206475)) return 5;
    // A karaoke file whose only line is a 320 s "instrumental" marker for a
    // 206 s track is rejected as well.
    if (LyricsFitTrackDuration(MakeDocument(1589, 320317), 206475)) return 6;

    // Without a duration from the player there is nothing to compare against.
    if (!LyricsFitTrackDuration(MakeDocument(250290, 4000), 0)) return 7;
    // An empty document is the caller's business, not a mismatch.
    MusicBar::LyricDocument empty{};
    if (!LyricsFitTrackDuration(empty, 206475)) return 8;

    // Lines are sorted by start time, so the last entry is the latest one.
    MusicBar::LyricDocument ordered{};
    MusicBar::LyricLine first{};
    first.startMs = 410;
    first.durationMs = 940;
    first.text = L"header";
    MusicBar::LyricLine second{};
    second.startMs = 41967;
    second.durationMs = 1543;
    second.text = L"first verse";
    ordered.lines.push_back(first);
    ordered.lines.push_back(second);
    if (!LyricsFitTrackDuration(ordered, 206475)) return 9;

    return 0;
}
