#include "LyricsService.hpp"
#include "providers/NetEaseProvider.hpp"
#include "providers/KugouProvider.hpp"
#include "providers/QQMusicProvider.hpp"
#include "providers/LrclibProvider.hpp"
#include "providers/SodaProvider.hpp"
#include <thread>
#include <algorithm>

namespace MusicBar {
namespace {

std::wstring CacheKey(const std::wstring& title,
                      const std::wstring& artist, LyricsSource source) {
    return title + L'\x1f' + artist + L'\x1f' +
        std::to_wstring(static_cast<int>(source));
}

} // namespace

LyricsService::LyricsService() {
    m_providers.push_back(std::make_unique<NetEaseProvider>());
    m_providers.push_back(std::make_unique<QQMusicProvider>());
    m_providers.push_back(std::make_unique<KugouProvider>());
    m_providers.push_back(std::make_unique<SodaProvider>());
    m_providers.push_back(std::make_unique<LrclibProvider>());
    m_fetchThread = std::thread(&LyricsService::WorkerLoop, this);
}

LyricsService::~LyricsService() {
    Clear();
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_stopping = true;
    }
    m_requestReady.notify_one();
    if (m_fetchThread.joinable()) m_fetchThread.join();
}

void LyricsService::Clear() {
    {
        std::lock_guard<std::mutex> lock(m_docMutex);
        ++m_queryGeneration;
        m_currentDoc = LyricDocument{};
    }
    std::lock_guard<std::mutex> requestLock(m_requestMutex);
    m_pendingRequest.reset();
}

LyricDocument LyricsService::GetCurrentLyrics() const {
    std::lock_guard<std::mutex> lock(m_docMutex);
    return m_currentDoc;
}

void LyricsService::RequestLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t durationMs,
    LyricsSource source
) {
    if (title.empty()) {
        Clear();
        return;
    }

    const std::wstring key = CacheKey(title, artist, source);
    uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        gen = ++m_queryGeneration;
        m_currentDoc = LyricDocument{};
    }

    std::optional<LyricDocument> cached;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) cached = it->second;
    }
    if (cached) {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        if (m_queryGeneration != gen) return;
        m_currentDoc = *cached;
    }
    if (cached) {
        std::lock_guard<std::mutex> requestLock(m_requestMutex);
        m_pendingRequest.reset();
    }
    if (cached) {
        if (m_callback) m_callback(*cached);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pendingRequest = Request{gen, title, artist, durationMs, source};
    }
    m_requestReady.notify_one();
}

void LyricsService::WorkerLoop() {
    for (;;) {
        Request request{};
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_requestReady.wait(lock, [this] {
                return m_stopping || m_pendingRequest.has_value();
            });
            if (m_stopping) return;
            request = std::move(*m_pendingRequest);
            m_pendingRequest.reset();
        }
        FetchWorker(request.generation, std::move(request.title),
            std::move(request.artist), request.durationMs, request.source);
    }
}

void LyricsService::FetchWorker(
    uint64_t gen,
    std::wstring title,
    std::wstring artist,
    int64_t durationMs,
    LyricsSource source
) {
    std::optional<LyricDocument> result;
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        if (m_queryGeneration != gen) return;
    }

    // If a specific provider was selected:
    if (source != LyricsSource::Auto) {
        size_t idx = static_cast<size_t>(source) - 1;
        if (idx < m_providers.size()) {
            try {
                result = m_providers[idx]->FetchLyrics(title, artist, durationMs);
            } catch (...) {
                result.reset();
            }
        }
    }

    // If auto or preferred source returned nothing, fallback across all providers
    if (!result || result->lines.empty()) {
        for (size_t i = 0; i < m_providers.size(); ++i) {
            if (source != LyricsSource::Auto &&
                i == static_cast<size_t>(source) - 1) continue;
            // Check if superseded
            {
                std::lock_guard<std::mutex> docLock(m_docMutex);
                if (m_queryGeneration != gen) return;
            }

            try {
                result = m_providers[i]->FetchLyrics(title, artist, durationMs);
            } catch (...) {
                result.reset();
            }
            if (result && !result->lines.empty()) {
                break;
            }
        }
    }

    // Check again if superseded
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        if (m_queryGeneration != gen) return;
    }

    if (result && !result->lines.empty()) {
        const std::wstring key = CacheKey(title, artist, source);
        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            if (m_cache.size() >= 50 && !m_cache.contains(key)) {
                m_cache.erase(m_cache.begin());
            }
            m_cache[key] = *result;
        }

        LyricDocument published;
        {
            std::lock_guard<std::mutex> docLock(m_docMutex);
            if (m_queryGeneration != gen) return;
            m_currentDoc = std::move(*result);
            published = m_currentDoc;
        }

        if (m_callback) m_callback(published);
    }
}

void LyricsService::UpdatePosition(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_docMutex);
    m_currentDoc.activeSyllableIndex = -1;
    m_currentDoc.syllableProgress = 0.0f;
    if (m_currentDoc.lines.empty()) {
        m_currentDoc.activeIndex = -1;
        return;
    }

    int activeIdx = -1;
    for (int i = 0; i < static_cast<int>(m_currentDoc.lines.size()); ++i) {
        if (m_currentDoc.lines[i].startMs <= positionMs) {
            activeIdx = i;
        } else {
            break;
        }
    }

    m_currentDoc.activeIndex = activeIdx;
    if (activeIdx >= 0 && activeIdx < static_cast<int>(m_currentDoc.lines.size())) {
        const auto& line = m_currentDoc.lines[activeIdx];
        if (line.HasKaraoke()) {
            for (size_t i = 0; i < line.syllables.size(); ++i) {
                const auto& syl = line.syllables[i];
                if (positionMs < syl.startMs) break;
                m_currentDoc.activeSyllableIndex = static_cast<int>(i);
                m_currentDoc.syllableProgress = syl.durationMs > 0
                    ? std::clamp(static_cast<float>(positionMs - syl.startMs) /
                        static_cast<float>(syl.durationMs), 0.0f, 1.0f)
                    : 1.0f;
            }
        } else if (line.durationMs > 0) {
            m_currentDoc.syllableProgress = static_cast<float>(positionMs - line.startMs) / static_cast<float>(line.durationMs);
            m_currentDoc.syllableProgress = std::clamp(m_currentDoc.syllableProgress, 0.0f, 1.0f);
        }
    }
}

} // namespace MusicBar
