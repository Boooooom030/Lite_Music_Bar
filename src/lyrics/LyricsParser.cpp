#include "LyricsParser.hpp"
#include "Decrypter.hpp"
#include <sstream>
#include <regex>
#include <algorithm>

namespace MusicBar {

bool LyricsParser::TryParseSyllableLine(
    const std::wstring& lineStr,
    LyricLine& outLine
) {
    // Matches line timestamp [startMs,durMs]
    // Syllable timestamps may be offsets (KRC) or absolute song times (YRC).
    static const std::wregex lineHeaderRegex(LR"(^\[(\d+),(\d+)\])");
    std::wsmatch match;

    if (!std::regex_search(lineStr, match, lineHeaderRegex)) {
        return false;
    }

    outLine.startMs = std::stoll(match[1].str());
    outLine.durationMs = std::stoll(match[2].str());
    outLine.syllables.clear();
    outLine.text.clear();

    size_t pos = match.length();
    // Parse syllable tokens: (<offset,dur,?>Word or (offset,dur)Word)
    static const std::wregex syllableRegex(LR"(([<\(])(\d+),(\d+)(?:,\d+)?([>\)])([^<\(]*))");
    auto words_begin = std::wsregex_iterator(lineStr.begin() + pos, lineStr.end(), syllableRegex);
    auto words_end = std::wsregex_iterator();

    for (auto it = words_begin; it != words_end; ++it) {
        std::wsmatch sylMatch = *it;
        int64_t sylOffset = std::stoll(sylMatch[2].str());
        int64_t sylDur = std::stoll(sylMatch[3].str());
        std::wstring sylText = sylMatch[5].str();

        LyricSyllable syl{};
        syl.startMs = sylOffset;
        syl.durationMs = sylDur;
        syl.text = sylText;

        outLine.syllables.push_back(syl);
        outLine.text += sylText;
    }

    if (!outLine.syllables.empty()) {
        const int64_t firstTime = outLine.syllables.front().startMs;
        const bool absoluteTimes = outLine.startMs > 0 &&
            firstTime >= outLine.startMs &&
            firstTime <= outLine.startMs + std::max<int64_t>(outLine.durationMs, 1000);
        if (!absoluteTimes) {
            for (auto& syllable : outLine.syllables) {
                syllable.startMs += outLine.startMs;
            }
        }
    }

    return !outLine.syllables.empty() || !outLine.text.empty();
}

bool LyricsParser::TryParseStandardLrcLine(
    const std::wstring& lineStr,
    std::vector<LyricLine>& outLines
) {
    // Standard LRC timestamp: [mm:ss.xx] or [mm:ss.xxx]
    static const std::wregex lrcTagRegex(LR"(\[(\d{1,2}):(\d{2})(?:\.(\d{2,3}))?\])");
    auto tags_begin = std::wsregex_iterator(lineStr.begin(), lineStr.end(), lrcTagRegex);
    auto tags_end = std::wsregex_iterator();

    if (tags_begin == tags_end) return false;

    // Find end of last timestamp tag to extract line text
    size_t lastTagEnd = 0;
    std::vector<int64_t> timestamps;

    for (auto it = tags_begin; it != tags_end; ++it) {
        std::wsmatch m = *it;
        int min = std::stoi(m[1].str());
        int sec = std::stoi(m[2].str());
        int ms = 0;
        if (m[3].matched) {
            std::wstring msStr = m[3].str();
            ms = std::stoi(msStr);
            if (msStr.length() == 2) ms *= 10;
        }
        int64_t totalMs = (min * 60 + sec) * 1000 + ms;
        timestamps.push_back(totalMs);
        lastTagEnd = m.position() + m.length();
    }

    std::wstring text = lineStr.substr(lastTagEnd);
    // Trim leading / trailing spaces
    size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) text.clear();
    else text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);

    for (int64_t ts : timestamps) {
        LyricLine line{};
        line.startMs = ts;
        line.text = text;
        outLines.push_back(line);
    }

    return true;
}

void LyricsParser::MergeTranslations(
    std::vector<LyricLine>& baseLines,
    const std::vector<LyricLine>& transLines
) {
    for (const auto& trans : transLines) {
        // Find closest base line within 350ms
        auto it = std::min_element(
            baseLines.begin(),
            baseLines.end(),
            [&trans](const LyricLine& a, const LyricLine& b) {
                return std::abs(a.startMs - trans.startMs) < std::abs(b.startMs - trans.startMs);
            }
        );

        if (it != baseLines.end() && std::abs(it->startMs - trans.startMs) <= 350) {
            it->translation = trans.text;
        }
    }
}

LyricDocument LyricsParser::Parse(
    std::string_view rawText,
    std::string_view translationText
) {
    LyricDocument doc{};
    if (rawText.empty()) return doc;

    std::wstring wideRaw = Decrypter::Utf8ToWide(rawText);
    std::wistringstream stream(wideRaw);
    std::wstring line;

    std::vector<LyricLine> parsedLines;

    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;

        // 1. Try syllable line
        LyricLine sylLine{};
        if (TryParseSyllableLine(line, sylLine)) {
            parsedLines.push_back(sylLine);
            continue;
        }

        // 2. Try standard LRC line
        std::vector<LyricLine> lrcLines;
        if (TryParseStandardLrcLine(line, lrcLines)) {
            parsedLines.insert(parsedLines.end(), lrcLines.begin(), lrcLines.end());
            continue;
        }
    }

    // Sort lines by start time
    std::sort(parsedLines.begin(), parsedLines.end(), [](const LyricLine& a, const LyricLine& b) {
        return a.startMs < b.startMs;
    });

    // Calculate line durations if missing
    for (size_t i = 0; i < parsedLines.size(); ++i) {
        if (parsedLines[i].durationMs <= 0) {
            if (i + 1 < parsedLines.size()) {
                parsedLines[i].durationMs = parsedLines[i + 1].startMs - parsedLines[i].startMs;
            } else {
                parsedLines[i].durationMs = 4000;
            }
        }
    }

    // Parse translations if present
    if (!translationText.empty()) {
        std::wstring wideTrans = Decrypter::Utf8ToWide(translationText);
        std::wistringstream transStream(wideTrans);
        std::wstring transLineStr;
        std::vector<LyricLine> transLines;

        while (std::getline(transStream, transLineStr)) {
            if (!transLineStr.empty() && transLineStr.back() == L'\r') transLineStr.pop_back();
            if (transLineStr.empty()) continue;

            std::vector<LyricLine> cur;
            if (TryParseStandardLrcLine(transLineStr, cur)) {
                transLines.insert(transLines.end(), cur.begin(), cur.end());
            }
        }

        MergeTranslations(parsedLines, transLines);
    }

    doc.lines = std::move(parsedLines);
    return doc;
}

} // namespace MusicBar
