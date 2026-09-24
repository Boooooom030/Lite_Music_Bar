#include "LrclibProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>

namespace MusicBar {

std::optional<LyricDocument> LrclibProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t durationMs
) {
    if (title.empty()) return std::nullopt;

    std::wstring url = L"https://lrclib.net/api/get?track_name=" + Decrypter::UrlEncode(title);
    if (!artist.empty()) {
        url += L"&artist_name=" + Decrypter::UrlEncode(artist);
    }
    if (durationMs > 0) {
        url += L"&duration=" + std::to_wstring(durationMs / 1000);
    }

    std::map<std::wstring, std::wstring> headers = {
        { L"User-Agent", L"MusicBar/1.0" }
    };

    auto resp = m_http.Get(url, headers);
    if (!resp.success) return std::nullopt;

    auto json = nlohmann::json::parse(resp.body, nullptr, false);
    if (json.is_discarded()) return std::nullopt;

    std::string lrcText;
    if (json.contains("syncedLyrics") && !json["syncedLyrics"].is_null()) {
        lrcText = json["syncedLyrics"].get<std::string>();
    } else if (json.contains("plainLyrics") && !json["plainLyrics"].is_null()) {
        lrcText = json["plainLyrics"].get<std::string>();
    }

    if (lrcText.empty()) return std::nullopt;

    LyricDocument doc = LyricsParser::Parse(lrcText);
    doc.title = title;
    doc.artist = artist;
    return doc;
}

} // namespace MusicBar
