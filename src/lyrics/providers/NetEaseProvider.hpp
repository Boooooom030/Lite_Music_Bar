#pragma once

#include "ILyricsProvider.hpp"

namespace MusicBar {

class NetEaseProvider : public ILyricsProvider {
public:
    std::string GetName() const override { return "NetEase"; }

    std::optional<LyricDocument> FetchLyrics(
        const std::wstring& title,
        const std::wstring& artist,
        int64_t durationMs
    ) override;

};

} // namespace MusicBar
