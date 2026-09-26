#include "lyrics/LyricsParser.hpp"

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
    return 0;
}
