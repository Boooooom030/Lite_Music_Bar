#include "NetEaseProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>

namespace MusicBar {

std::optional<LyricDocument> NetEaseProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t /*durationMs*/
) {
    if (title.empty()) return std::nullopt;

    std::wstring keyword = title;
    if (!artist.empty()) {
        keyword += L" " + artist;
    }

    std::string postData = "s=" + Decrypter::WideToUtf8(Decrypter::UrlEncode(keyword)) + "&type=1&offset=0&limit=3";
    std::map<std::wstring, std::wstring> headers = {
        { L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)" },
        { L"Referer", L"https://music.163.com/" }
    };

    auto searchResp = m_http.Post(
        L"https://music.163.com/api/search/get/web",
        postData,
        L"application/x-www-form-urlencoded",
        headers
    );

    if (!searchResp.success) return std::nullopt;

    auto searchJson = nlohmann::json::parse(searchResp.body, nullptr, false);
    if (searchJson.is_discarded() || !searchJson.contains("result")) return std::nullopt;

    const auto& result = searchJson["result"];
    if (!result.contains("songs") || !result["songs"].is_array() || result["songs"].empty()) {
        return std::nullopt;
    }

    int64_t songId = result["songs"][0]["id"].get<int64_t>();

    // Query lyrics
    std::wstring lyricUrl = L"https://music.163.com/api/song/lyric?id=" + std::to_wstring(songId) + L"&lv=1&kv=1&tv=1&yv=1";
    auto lyricResp = m_http.Get(lyricUrl, headers);
    if (!lyricResp.success) return std::nullopt;

    auto lyricJson = nlohmann::json::parse(lyricResp.body, nullptr, false);
    if (lyricJson.is_discarded()) return std::nullopt;

    std::string mainLyric;
    std::string transLyric;

    // Prefer syllable-level YRC if available
    if (lyricJson.contains("yrc") && lyricJson["yrc"].contains("lyric")) {
        mainLyric = lyricJson["yrc"]["lyric"].get<std::string>();
    } else if (lyricJson.contains("lrc") && lyricJson["lrc"].contains("lyric")) {
        mainLyric = lyricJson["lrc"]["lyric"].get<std::string>();
    }

    if (lyricJson.contains("tlyric") && lyricJson["tlyric"].contains("lyric")) {
        transLyric = lyricJson["tlyric"]["lyric"].get<std::string>();
    }

    if (mainLyric.empty()) return std::nullopt;

    LyricDocument doc = LyricsParser::Parse(mainLyric, transLyric);
    doc.title = title;
    doc.artist = artist;
    return doc;
}

} // namespace MusicBar
