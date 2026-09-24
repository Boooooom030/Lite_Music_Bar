#include "SodaProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>

namespace MusicBar {

std::optional<LyricDocument> SodaProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t /*durationMs*/
) {
    if (title.empty()) return std::nullopt;

    std::wstring keyword = title;
    if (!artist.empty()) keyword += L" " + artist;

    std::wstring searchUrl = L"https://api.music.apple.com/v1/catalog/cn/search?types=songs&term=" + Decrypter::UrlEncode(keyword);
    // Alternatively Soda / Douyin music query endpoint
    // Fallback if not configured
    return std::nullopt;
}

} // namespace MusicBar
