#pragma once

#include "ILyricsProvider.hpp"
#include "net/HttpClient.hpp"

namespace MusicBar {

class LrclibProvider : public ILyricsProvider {
public:
    std::string GetName() const override { return "LRCLIB"; }

    std::optional<LyricDocument> FetchLyrics(
        const std::wstring& title,
        const std::wstring& artist,
        int64_t durationMs
    ) override;

private:
    HttpClient m_http;
};

} // namespace MusicBar
