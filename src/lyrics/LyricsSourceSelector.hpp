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

} // namespace MusicBar
