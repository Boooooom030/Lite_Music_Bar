#pragma once

#include "core/Types.hpp"
#include <string>
#include <optional>

namespace MusicBar {

class ILyricsProvider {
public:
    virtual ~ILyricsProvider() = default;
    virtual std::string GetName() const = 0;
    virtual std::optional<LyricDocument> FetchLyrics(
        const std::wstring& title,
        const std::wstring& artist,
        int64_t durationMs
    ) = 0;
};

} // namespace MusicBar
