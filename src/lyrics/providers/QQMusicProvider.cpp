#include "QQMusicProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>

namespace MusicBar {

std::optional<LyricDocument> QQMusicProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t /*durationMs*/
) {
    if (title.empty()) return std::nullopt;

    std::wstring keyword = title;
    if (!artist.empty()) {
        keyword += L" " + artist;
    }

    std::wstring searchUrl = L"https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=" +
        Decrypter::UrlEncode(keyword) + L"&format=json&p=1&n=3";

    std::map<std::wstring, std::wstring> headers = {
        { L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)" },
        { L"Referer", L"https://y.qq.com/" }
    };

    auto searchResp = m_http.Get(searchUrl, headers);
    if (!searchResp.success) return std::nullopt;

    std::string searchBody = searchResp.body;
    // Strip JSONP wrapper if present: callback(...)
    if (searchBody.rfind("callback(", 0) == 0) {
        searchBody = searchBody.substr(9);
        if (!searchBody.empty() && searchBody.back() == ')') searchBody.pop_back();
    }

    auto searchJson = nlohmann::json::parse(searchBody, nullptr, false);
    if (searchJson.is_discarded() || !searchJson.contains("data")) return std::nullopt;

    const auto& data = searchJson["data"];
    if (!data.contains("song") || !data["song"].contains("list") || data["song"]["list"].empty()) {
        return std::nullopt;
    }

    std::string songMid = data["song"]["list"][0]["songmid"].get<std::string>();
    std::wstring wSongMid = Decrypter::Utf8ToWide(songMid);

    // Query lyrics
    std::wstring lyricUrl = L"https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" +
        wSongMid + L"&format=json&nobase64=1";

    auto lyricResp = m_http.Get(lyricUrl, headers);
    if (!lyricResp.success) return std::nullopt;

    std::string lyricBody = lyricResp.body;
    if (lyricBody.rfind("callback(", 0) == 0) {
        lyricBody = lyricBody.substr(9);
        if (!lyricBody.empty() && lyricBody.back() == ')') lyricBody.pop_back();
    }

    auto lyricJson = nlohmann::json::parse(lyricBody, nullptr, false);
    if (lyricJson.is_discarded()) return std::nullopt;

    std::string mainLyric;
    std::string transLyric;

    if (lyricJson.contains("lyric")) {
        mainLyric = lyricJson["lyric"].get<std::string>();
    }
    if (lyricJson.contains("trans")) {
        transLyric = lyricJson["trans"].get<std::string>();
    }

    // Check if base64 encoded
    if (!mainLyric.empty() && mainLyric[0] != '[') {
        auto decoded = Decrypter::Base64Decode(mainLyric);
        if (!decoded.empty()) {
            mainLyric = std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        }
    }

    if (!transLyric.empty() && transLyric[0] != '[') {
        auto decoded = Decrypter::Base64Decode(transLyric);
        if (!decoded.empty()) {
            transLyric = std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        }
    }

    if (mainLyric.empty()) return std::nullopt;

    LyricDocument doc = LyricsParser::Parse(mainLyric, transLyric);
    doc.title = title;
    doc.artist = artist;
    return doc;
}

} // namespace MusicBar
