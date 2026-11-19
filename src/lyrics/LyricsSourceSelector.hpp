#pragma once

#include "core/Types.hpp"
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace MusicBar {

inline LyricsSource PreferredLyricsSource(std::wstring_view appId) {
    std::wstring source(appId);
    std::transform(source.begin(), source.end(), source.begin(),
        [](wchar_t ch) { return std::towlower(ch); });
    if (source.find(L"cloudmusic") != std::wstring::npos ||
        source.find(L"netease") != std::wstring::npos) {
        return LyricsSource::NetEase;
    }
    if (source.find(L"qqmusic") != std::wstring::npos) {
        return LyricsSource::QQMusic;
    }
    if (source.find(L"kugou") != std::wstring::npos) {
        return LyricsSource::Kugou;
    }
    if (source.find(L"sodamusic") != std::wstring::npos ||
        source.find(L"qishui") != std::wstring::npos) {
        return LyricsSource::Soda;
    }
    return LyricsSource::Auto;
}

// Whether this session can plausibly be a song. Browsers and video players
// publish a page or video title with no artist field; searching the providers
// for that text matches unrelated songs, which is how a video ends up with
// someone else's lyrics. Both signals must hold before the chain is asked at all.
inline bool LyricsLookupAllowed(std::wstring_view appId, std::wstring_view artist) {
    if (artist.empty()) return false;
    std::wstring source(appId);
    std::transform(source.begin(), source.end(), source.begin(),
        [](wchar_t ch) { return std::towlower(ch); });
    static constexpr std::wstring_view NonMusicApps[] = {
        L"msedge", L"chrome", L"firefox", L"opera", L"brave", L"vivaldi",
        L"iexplore", L"360se", L"360chrome", L"sogouexplorer", L"qqbrowser",
        L"potplayer", L"vlc", L"mpc-hc", L"mpc-be", L"mpv", L"wmplayer",
        L"kmplayer", L"gomplayer"
    };
    for (const auto app : NonMusicApps) {
        if (source.find(app) != std::wstring::npos) return false;
    }
    return true;
}

} // namespace MusicBar
