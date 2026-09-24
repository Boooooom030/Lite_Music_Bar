#pragma once

#include "ILyricsProvider.hpp"
#include "net/HttpClient.hpp"

namespace MusicBar {

class NetEaseProvider : public ILyricsProvider {
public:
    std::string GetName() const override { return "NetEase"; }

    std::optional<LyricDocument> FetchLyrics(
        const std::wstring& title,
        const std::wstring& artist,
        int64_t durationMs
    ) override;

private:
    HttpClient m_http;
};

} // namespace MusicBar
