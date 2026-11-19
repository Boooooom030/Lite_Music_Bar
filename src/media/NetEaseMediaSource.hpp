#pragma once

#include "core/Types.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MusicBar {

// Reads the current NetEase song ID and playback state from cloudmusic.dll.
// Metadata is cached by ID; artwork is cached by URL. No title/history guessing.
class NetEaseMediaSource {
public:
    std::optional<MediaTrackInfo> Read();

    // Diagnostics for the artwork cache: how many covers are cached and how
    // many warm-up requests are still queued or in flight.
    size_t CachedCoverCount() const { return m_coverCache.size(); }
    size_t PendingCoverWork() const {
        return m_coverRequests.size() + m_prefetchQueue.size();
    }
    bool HasCachedCover(std::wstring_view url) const {
        return std::any_of(m_coverCache.begin(), m_coverCache.end(),
            [&](const auto& item) { return item.first == url; });
    }

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
    void StartCoverRequest(const std::wstring& url);
    void QueuePrefetchCovers(const std::vector<std::wstring>& urls);
    void StartPrefetch();

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
    std::deque<std::wstring> m_prefetchQueue;
    unsigned m_coverAttempts = 0;
    std::chrono::steady_clock::time_point m_nextCoverAttempt{};
};

} // namespace MusicBar
