#include "LyricsService.hpp"
#include "providers/NetEaseProvider.hpp"
#include "providers/KugouProvider.hpp"
#include "providers/QQMusicProvider.hpp"
#include "providers/LrclibProvider.hpp"
#include "providers/SodaProvider.hpp"
#include <thread>
#include <algorithm>

namespace MusicBar {

LyricsService::LyricsService() {
    m_providers.push_back(std::make_unique<NetEaseProvider>());
    m_providers.push_back(std::make_unique<KugouProvider>());
    m_providers.push_back(std::make_unique<QQMusicProvider>());
    m_providers.push_back(std::make_unique<LrclibProvider>());
    m_providers.push_back(std::make_unique<SodaProvider>());
}

LyricsService::~LyricsService() = default;

void LyricsService::Clear() {
    std::lock_guard<std::mutex> lock(m_docMutex);
    m_currentDoc = LyricDocument{};
    m_currentKey.clear();
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

    std::wstring key = title + L" - " + artist;

    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            {
                std::lock_guard<std::mutex> docLock(m_docMutex);
                m_currentDoc = it->second;
                m_currentKey = key;
            }
            if (m_callback) m_callback(m_currentDoc);
            return;
        }
    }

    // New request: increment generation and spin worker thread
    uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> docLock(m_docMutex);
        gen = ++m_queryGeneration;
        m_currentDoc = LyricDocument{};
        m_currentKey = key;
    }

    std::thread([this, gen, title, artist, durationMs, source]() {
        FetchWorker(gen, title, artist, durationMs, source);
    }).detach();
}

void LyricsService::FetchWorker(
    uint64_t gen,
    std::wstring title,
    std::wstring artist,
    int64_t durationMs,
    LyricsSource source
) {
    std::optional<LyricDocument> result;

    // If a specific provider was selected:
    if (source != LyricsSource::Auto) {
        size_t idx = static_cast<size_t>(source) - 1;
        if (idx < m_providers.size()) {
            result = m_providers[idx]->FetchLyrics(title, artist, durationMs);
        }
    }

    // If auto or preferred source returned nothing, fallback across all providers
    if (!result) {
        for (const auto& provider : m_providers) {
            // Check if superseded
            {
                std::lock_guard<std::mutex> docLock(m_docMutex);
                if (m_queryGeneration != gen) return;
            }

            result = provider->FetchLyrics(title, artist, durationMs);
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
        std::wstring key = title + L" - " + artist;
        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            if (m_cache.size() > 50) {
                m_cache.erase(m_cache.begin());
            }
            m_cache[key] = *result;
        }

        {
            std::lock_guard<std::mutex> docLock(m_docMutex);
            m_currentDoc = std::move(*result);
        }

        if (m_callback) {
            m_callback(m_currentDoc);
        }
    }
}

void LyricsService::UpdatePosition(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_docMutex);
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
            for (const auto& syl : line.syllables) {
                if (positionMs >= syl.startMs && positionMs < syl.startMs + syl.durationMs) {
                    m_currentDoc.syllableProgress = static_cast<float>(positionMs - syl.startMs) / static_cast<float>(syl.durationMs);
                    break;
                }
            }
        } else if (line.durationMs > 0) {
            m_currentDoc.syllableProgress = static_cast<float>(positionMs - line.startMs) / static_cast<float>(line.durationMs);
            m_currentDoc.syllableProgress = std::clamp(m_currentDoc.syllableProgress, 0.0f, 1.0f);
        }
    }
}

} // namespace MusicBar
