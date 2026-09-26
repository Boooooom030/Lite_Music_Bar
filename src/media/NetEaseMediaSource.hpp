#pragma once

#include "core/Types.hpp"
#include <windows.h>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MusicBar {

// Reads the current NetEase song ID and playback state from cloudmusic.dll.
// Metadata is cached by ID; artwork is cached by URL. No title/history guessing.
class NetEaseMediaSource {
public:
    std::optional<MediaTrackInfo> Read();

private:
    struct HandleCloser {
        void operator()(void* handle) const noexcept {
            if (handle) CloseHandle(handle);
        }
    };
    struct CoverRequest {
        std::wstring url;
        std::future<std::vector<uint8_t>> result;
    };
    struct CachedTrack {
        std::string identity;
        MediaTrackInfo track;
        std::wstring coverUrl;
    };

    void ResetProcess();
    bool Attach(DWORD pid);
    void CollectCovers();
    void StartCoverIfNeeded(std::chrono::steady_clock::time_point now);

    std::unique_ptr<void, HandleCloser> m_process;
    DWORD m_processId = 0;
    uintptr_t m_playerAddress = 0;
    uintptr_t m_scheduleAddress = 0;
    std::chrono::steady_clock::time_point m_nextAttachAttempt{};
    std::chrono::steady_clock::time_point m_nextMetadataAttempt{};
    std::string m_identity;
    std::wstring m_coverUrl;
    MediaTrackInfo m_track;
    std::deque<CachedTrack> m_trackCache;

    std::vector<CoverRequest> m_coverRequests;
    std::deque<std::pair<std::wstring, std::vector<uint8_t>>> m_coverCache;
    unsigned m_coverAttempts = 0;
    std::chrono::steady_clock::time_point m_nextCoverAttempt{};
};

} // namespace MusicBar
