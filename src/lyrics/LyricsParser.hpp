#pragma once

#include <string>
#include <vector>
#include "core/Types.hpp"

namespace MusicBar {

class LyricsParser {
public:
    static LyricDocument Parse(
        std::string_view rawText,
        std::string_view translationText = ""
    );

private:
    static bool TryParseSyllableLine(
        const std::wstring& lineStr,
        LyricLine& outLine
    );

    static bool TryParseStandardLrcLine(
        const std::wstring& lineStr,
        std::vector<LyricLine>& outLines
    );

    static void MergeTranslations(
        std::vector<LyricLine>& baseLines,
        const std::vector<LyricLine>& transLines
    );
};

} // namespace MusicBar
