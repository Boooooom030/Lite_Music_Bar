#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <thread>
#include "core/Types.hpp"
#include "providers/ILyricsProvider.hpp"

namespace MusicBar {

class LyricsService {
public:
    using LyricsLoadedCallback = std::function<void(const LyricDocument&)>;

    LyricsService();
    ~LyricsService();

    void SetCallback(LyricsLoadedCallback cb) { m_callback = cb; }

    void RequestLyrics(
        const std::wstring& title,
        const std::wstring& artist,
        int64_t durationMs,
        LyricsSource source = LyricsSource::Auto
    );

    void UpdatePosition(int64_t positionMs);

    LyricDocument GetCurrentLyrics() const;
    void Clear();

private:
    struct Request {
        uint64_t generation;
        std::wstring title;
        std::wstring artist;
        int64_t durationMs;
        LyricsSource source;
    };

    LyricsLoadedCallback m_callback;
    std::vector<std::unique_ptr<ILyricsProvider>> m_providers;
    std::map<std::wstring, LyricDocument> m_cache;
    mutable std::mutex m_cacheMutex;
    mutable std::mutex m_docMutex;

    LyricDocument m_currentDoc;
    uint64_t m_queryGeneration = 0;
    std::mutex m_requestMutex;
    std::condition_variable m_requestReady;
    std::optional<Request> m_pendingRequest;
    std::thread m_fetchThread;
    bool m_stopping = false;

    void WorkerLoop();

    void FetchWorker(
        uint64_t gen,
        std::wstring title,
        std::wstring artist,
        int64_t durationMs,
        LyricsSource source
    );
};

} // namespace MusicBar
