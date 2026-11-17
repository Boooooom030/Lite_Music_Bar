#include "lyrics/LyricsService.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
class FakeProvider final : public MusicBar::ILyricsProvider {
public:
    FakeProvider(bool first, std::atomic<int>& calls) : m_first(first), m_calls(calls) {}
    std::string GetName() const override { return m_first ? "NetEase" : "QQMusic"; }
    std::optional<MusicBar::LyricDocument> FetchLyrics(const std::wstring& title,
        const std::wstring&, int64_t) override {
        ++m_calls;
        MusicBar::LyricDocument document{};
        if (m_first && title == L"Instrumental") {
            document.isInstrumental = true;
        } else if (!m_first) {
            document.lines.push_back({0, 4000, L"Singing"});
        }
        return document;
    }
private:
    bool m_first;
    std::atomic<int>& m_calls;
};
class EmptyProvider final : public MusicBar::ILyricsProvider {
public:
    explicit EmptyProvider(std::atomic<int>& calls) : m_calls(calls) {}
    std::string GetName() const override { return "Empty"; }
    std::optional<MusicBar::LyricDocument> FetchLyrics(const std::wstring&,
        const std::wstring&, int64_t) override {
        ++m_calls;
        return MusicBar::LyricDocument{};
    }
private:
    std::atomic<int>& m_calls;
};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        std::atomic<int> preferredCalls{}, fallbackCalls{};
        std::mutex mutex;
        std::condition_variable loaded;
        int completions = 0;
        MusicBar::LyricDocument published{};
        std::vector<std::unique_ptr<MusicBar::ILyricsProvider>> providers;
        providers.push_back(std::make_unique<FakeProvider>(true, preferredCalls));
        providers.push_back(std::make_unique<FakeProvider>(false, fallbackCalls));
        MusicBar::LyricsService service(std::move(providers));
        service.SetCallback([&](const MusicBar::LyricDocument& document) {
            {
                std::lock_guard lock(mutex);
                published = document;
                ++completions;
            }
            loaded.notify_one();
        });
        auto request = [&](const std::wstring& title, int completion,
            MusicBar::LyricsSource source = MusicBar::LyricsSource::NetEase) {
            service.RequestLyrics(title, L"Artist", 185448, source);
            std::unique_lock lock(mutex);
            Require(loaded.wait_for(lock, std::chrono::seconds(2), [&] {
                return completions >= completion;
            }), "Lyric result was not published");
            return published;
        };
        auto document = request(L"Instrumental", 1);
        Require(document.isInstrumental && document.lines.empty() &&
            service.GetCurrentLyrics().isInstrumental, "Instrumental result was lost before UI publication");
        Require(preferredCalls == 1 && fallbackCalls == 0, "Instrumental unnecessarily fell back to another source");
        document = request(L"Instrumental", 2);
        Require(document.isInstrumental && preferredCalls == 1 && fallbackCalls == 0,
            "Cache lost instrumental status");
        document = request(L"Missing preferred lyrics", 3);
        Require(!document.isInstrumental && document.lines.size() == 1 &&
            preferredCalls == 2 && fallbackCalls == 1, "Empty lyrics were treated as instrumental");
        document = request(L"Missing preferred lyrics", 4, MusicBar::LyricsSource::QQMusic);
        Require(!document.isInstrumental && fallbackCalls == 1, "Produced-source cache regressed");
        service.Clear();
        Require(!service.GetCurrentLyrics().isInstrumental, "Instrumental status survived clearing the song");

        // No provider has a lyric for this track: the empty state is published
        // once, flagged for the UI, and served from the cache afterwards.
        std::atomic<int> firstEmptyCalls{}, secondEmptyCalls{};
        std::vector<std::unique_ptr<MusicBar::ILyricsProvider>> emptyProviders;
        emptyProviders.push_back(std::make_unique<EmptyProvider>(firstEmptyCalls));
        emptyProviders.push_back(std::make_unique<EmptyProvider>(secondEmptyCalls));
        MusicBar::LyricsService emptyService(std::move(emptyProviders));
        std::mutex emptyMutex;
        std::condition_variable emptyLoaded;
        int emptyCompletions = 0;
        MusicBar::LyricDocument emptyPublished{};
        emptyService.SetCallback([&](const MusicBar::LyricDocument& emptyDocument) {
            {
                std::lock_guard lock(emptyMutex);
                emptyPublished = emptyDocument;
                ++emptyCompletions;
            }
            emptyLoaded.notify_one();
        });
        auto expectEmptyState = [&](int completion) {
            std::unique_lock lock(emptyMutex);
            Require(emptyLoaded.wait_for(lock, std::chrono::seconds(2), [&] {
                return emptyCompletions >= completion;
            }), "No-lyrics state was not published");
            return emptyPublished;
        };
        emptyService.RequestLyrics(L"Unknown song", L"Nobody", 200000,
            MusicBar::LyricsSource::Auto);
        emptyPublished = expectEmptyState(1);
        Require(emptyPublished.noLyrics && emptyPublished.lines.empty() &&
            !emptyPublished.isInstrumental, "Missing lyric was not flagged as a no-lyrics state");
        Require(emptyService.GetCurrentLyrics().noLyrics,
            "No-lyrics state was lost before UI publication");
        Require(firstEmptyCalls == 1 && secondEmptyCalls == 1,
            "Every provider must be asked for a missing lyric");
        emptyService.RequestLyrics(L"Unknown song", L"Nobody", 200000,
            MusicBar::LyricsSource::Auto);
        emptyPublished = expectEmptyState(2);
        Require(emptyPublished.noLyrics && firstEmptyCalls == 1 && secondEmptyCalls == 1,
            "Cached no-lyrics state refetched the providers");
        emptyService.Clear();
        Require(!emptyService.GetCurrentLyrics().noLyrics,
            "No-lyrics state survived clearing the song");

        std::cout << "Lyrics service: instrumental publication, fallback stopping, caching, "
                     "normal fallback and the no-lyrics state passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
