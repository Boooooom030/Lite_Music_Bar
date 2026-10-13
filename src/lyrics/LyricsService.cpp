#include "LyricsService.hpp"
#include "LyricsFit.hpp"
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

LyricsService::LyricsService(std::vector<std::unique_ptr<ILyricsProvider>> providers)
    : m_providers(std::move(providers)) {
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
    int producerIndex = -1;
    // First non-empty result that does not fit the track duration. Providers are
    // asked for another file first, but a mismatched lyric still beats no lyric.
    std::optional<LyricDocument> mismatched;
    int mismatchedIndex = -1;
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        if (m_queryGeneration != gen) return;
    }

    auto tryProvider = [&](size_t index) {
        std::optional<LyricDocument> fetched;
        try {
            fetched = m_providers[index]->FetchLyrics(title, artist, durationMs);
        } catch (...) {
            fetched.reset();
        }
        if (!fetched || (fetched->lines.empty() && !fetched->isInstrumental)) return;
        if (!LyricsFitTrackDuration(*fetched, durationMs)) {
            if (!mismatched) {
                mismatched = std::move(fetched);
                mismatchedIndex = static_cast<int>(index);
            }
            return;
        }
        result = std::move(fetched);
        producerIndex = static_cast<int>(index);
    };

    // The selected source first, then every other provider in order.
    if (source != LyricsSource::Auto) {
        const size_t preferred = static_cast<size_t>(source) - 1;
        if (preferred < m_providers.size()) tryProvider(preferred);
    }

    for (size_t i = 0; i < m_providers.size() && !result; ++i) {
        if (source != LyricsSource::Auto &&
            i == static_cast<size_t>(source) - 1) continue;
        // Check if superseded
        {
            std::lock_guard<std::mutex> docLock(m_docMutex);
            if (m_queryGeneration != gen) return;
        }
        tryProvider(i);
    }

    if (!result && mismatched) {
        result = std::move(mismatched);
        producerIndex = mismatchedIndex;
    }

    // Check again if superseded
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        if (m_queryGeneration != gen) return;
    }

    if (result && (!result->lines.empty() || result->isInstrumental)) {
        // Cache under the requested source and under the provider that actually
        // produced the document, so switching to that source serves the same
        // file instead of refetching the whole chain.
        const LyricsSource producerSource = producerIndex >= 0
            ? static_cast<LyricsSource>(producerIndex + 1) : source;
        auto storeInCache = [this](const std::wstring& key, const LyricDocument& document) {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            if (m_cache.size() >= 50 && !m_cache.contains(key)) {
                m_cache.erase(m_cache.begin());
            }
            m_cache[key] = document;
        };
        storeInCache(CacheKey(title, artist, source), *result);
        if (producerSource != source) {
            storeInCache(CacheKey(title, artist, producerSource), *result);
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

} // namespace MusicBar
