#include "KugouProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>

namespace MusicBar {

std::optional<LyricDocument> KugouProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t durationMs
) {
    if (title.empty()) return std::nullopt;

    std::wstring keyword = title;
    if (!artist.empty()) {
        keyword += L" " + artist;
    }

    std::wstring searchUrl = L"http://mobilecdn.kugou.com/api/v3/search/song?keyword=" +
        Decrypter::UrlEncode(keyword) + L"&page=1&pagesize=3";

    std::map<std::wstring, std::wstring> headers = {
        { L"User-Agent", L"Kugou" }
    };

    auto searchResp = m_http.Get(searchUrl, headers);
    if (!searchResp.success) return std::nullopt;

    auto searchJson = nlohmann::json::parse(searchResp.body, nullptr, false);
    if (searchJson.is_discarded() || !searchJson.contains("data")) return std::nullopt;

    const auto& data = searchJson["data"];
    if (!data.contains("info") || !data["info"].is_array() || data["info"].empty()) {
        return std::nullopt;
    }

    std::string hash = data["info"][0]["hash"].get<std::string>();
    std::wstring wHash = Decrypter::Utf8ToWide(hash);

    // 2. Candidate search
    std::wstring candUrl = L"http://krcs.kugou.com/search?ver=1&man=yes&client=mobi&keyword=&duration=" +
        std::to_wstring(durationMs) + L"&hash=" + wHash;

    auto candResp = m_http.Get(candUrl, headers);
    if (!candResp.success) return std::nullopt;

    auto candJson = nlohmann::json::parse(candResp.body, nullptr, false);
    if (candJson.is_discarded() || !candJson.contains("candidates") || candJson["candidates"].empty()) {
        return std::nullopt;
    }

    const auto& cand = candJson["candidates"][0];
    std::string lyricId = cand["id"].get<std::string>();
    std::string accesskey = cand["accesskey"].get<std::string>();

    // 3. Download encrypted KRC
    std::wstring dlUrl = L"http://krcs.kugou.com/download?ver=1&client=mobi&id=" +
        Decrypter::Utf8ToWide(lyricId) + L"&accesskey=" + Decrypter::Utf8ToWide(accesskey) + L"&fmt=krc";

    auto dlResp = m_http.Get(dlUrl, headers);
    if (!dlResp.success) return std::nullopt;

    auto dlJson = nlohmann::json::parse(dlResp.body, nullptr, false);
    if (dlJson.is_discarded() || !dlJson.contains("content")) return std::nullopt;

    std::string base64Content = dlJson["content"].get<std::string>();
    auto krcBytes = Decrypter::Base64Decode(base64Content);
    if (krcBytes.empty()) return std::nullopt;

    // Decrypt KRC
    std::string decryptedKrc = Decrypter::DecryptKugouKrc(krcBytes);
    if (decryptedKrc.empty()) return std::nullopt;

    LyricDocument doc = LyricsParser::Parse(decryptedKrc);
    doc.title = title;
    doc.artist = artist;
    return doc;
}

} // namespace MusicBar
