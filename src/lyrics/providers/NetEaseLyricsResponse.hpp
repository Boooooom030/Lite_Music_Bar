#pragma once

#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace MusicBar {

// Pure response parsing also covers NetEase's successful instrumental result,
// which has pureMusic=true and an intentionally empty lrc.lyric field.
inline std::optional<LyricDocument> ParseNetEaseLyricsResponse(std::string_view body) {
    const auto response = nlohmann::json::parse(body, nullptr, false);
    if (!response.is_object() ||
        (response.contains("code") && response["code"] != 200)) return std::nullopt;
    const auto pure = response.find("pureMusic");
    if (pure != response.end() && pure->is_boolean() && pure->get<bool>()) {
        LyricDocument document{};
        document.isInstrumental = true;
        return document;
    }
    auto text = [&](const char* field) -> std::string {
        const auto entry = response.find(field);
        if (entry == response.end() || !entry->is_object()) return {};
        const auto lyric = entry->find("lyric");
        return lyric != entry->end() && lyric->is_string() ? lyric->get<std::string>() : std::string{};
    };
    const std::string translation = text("tlyric");
    // A present but empty/unusable YRC must not suppress valid standard LRC.
    for (const char* field : {"yrc", "lrc"}) {
        const auto raw = text(field);
        if (raw.empty()) continue;
        auto document = LyricsParser::Parse(raw, translation);
        if (!document.lines.empty()) return document;
    }
    return std::nullopt;
}

} // namespace MusicBar
