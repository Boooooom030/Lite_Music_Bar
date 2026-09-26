#include "MediaSession.hpp"
#include "NetEaseMediaSource.hpp"
#include "ProcessResolver.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace MusicBar {

namespace {
using Session = GlobalSystemMediaTransportControlsSession;
using Manager = GlobalSystemMediaTransportControlsSessionManager;
using Properties = GlobalSystemMediaTransportControlsSessionMediaProperties;
using Clock = std::chrono::steady_clock;
constexpr auto MissingSessionGrace = std::chrono::seconds(3);
constexpr auto AutoSwitchGrace = std::chrono::seconds(1);
constexpr auto PlaybackCommandGrace = std::chrono::seconds(3);

bool SameSession(const Session& left, const Session& right) {
    return left && right && winrt::get_abi(left) == winrt::get_abi(right);
}

std::vector<uint8_t> ReadThumbnail(const Properties& properties) {
    std::vector<uint8_t> bytes;
    if (!properties) return bytes;
    try {
        auto reference = properties.Thumbnail();
        if (!reference) return bytes;
        auto stream = reference.OpenReadAsync().get();
        const uint64_t size = stream.Size();
        if (size == 0 || size > 10 * 1024 * 1024) return bytes;
        Buffer buffer(static_cast<uint32_t>(size));
        auto readBuffer = stream.ReadAsync(buffer, static_cast<uint32_t>(size),
            InputStreamOptions::None).get();
        bytes.resize(readBuffer.Length());
        auto reader = DataReader::FromBuffer(readBuffer);
        reader.ReadBytes(bytes);
    } catch (...) {}
    return bytes;
}

bool SendMediaKey(WORD virtualKey) {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = virtualKey;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent == 1) {
        // A key-down may already have toggled playback. Finish the release,
        // but never queue a second toggle for this partially delivered press.
        SendInput(1, &inputs[1], sizeof(INPUT));
        return true;
    }
    if (sent == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent == 1) {
            SendInput(1, &inputs[1], sizeof(INPUT));
            return true;
        }
    }
    return sent == 2;
}
} // namespace

struct MediaSession::Impl : std::enable_shared_from_this<Impl> {
    enum class Command { Toggle, Next, Previous };
    struct Candidate {
        Session session = nullptr;
        Properties properties = nullptr;
        MediaTrackInfo track;
        std::wstring source;
        bool preferred = false;
        bool selected = false;
        bool playbackKnown = false;
    };
    struct PendingPlayback {
        std::wstring sourceAppId;
        std::wstring title;
        std::wstring artist;
        uint32_t sourceProcessId = 0;
        bool isPlaying = false;
        int64_t positionMs = 0;
        Clock::time_point startedAt{};
    };

    TrackChangedCallback callback;
    std::mutex trackMutex;
    MediaTrackInfo publishedTrack;
    std::optional<PendingPlayback> pendingPlayback;
    std::mutex commandMutex;
    std::vector<Command> commands;
    std::atomic<bool> refreshRequested{true};
    std::jthread worker;

    Manager manager = nullptr;
    NetEaseMediaSource netEaseSource;
    bool netEaseActive = false;
    Session selectedSession = nullptr;
    std::wstring selectedSource;
    winrt::event_token tokenCurrentChanged{};
    winrt::event_token tokenSessionsChanged{};
    winrt::event_token tokenPropsChanged{};
    winrt::event_token tokenPlaybackChanged{};
    winrt::event_token tokenTimelineChanged{};
    Clock::time_point missingSince{};
    Clock::time_point switchSince{};
    Clock::time_point nextManagerAttempt{};
    std::wstring pendingSwitchSource;
    std::wstring resolvedSource;
    DWORD resolvedProcessId = 0;
    Clock::time_point nextProcessResolve{};

    DWORD ResolveProcess(const std::wstring& source) {
        const auto now = Clock::now();
        if (source != resolvedSource) {
            resolvedSource = source;
            resolvedProcessId = 0;
            nextProcessResolve = {};
        }
        if (now < nextProcessResolve) return resolvedProcessId;
        nextProcessResolve = now + std::chrono::seconds(2);
        if (resolvedProcessId) {
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, resolvedProcessId);
            const bool alive = process &&
                WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            if (process) CloseHandle(process);
            if (alive) return resolvedProcessId;
            resolvedProcessId = 0;
        }
        resolvedProcessId = ResolveMediaProcess(source);
        return resolvedProcessId;
    }

    void SignalRefresh() { refreshRequested.store(true, std::memory_order_relaxed); }

    void Queue(Command command) {
        bool deliveredMediaKey = false;
        MediaTrackInfo snapshot;
        {
            std::lock_guard<std::mutex> lock(trackMutex);
            if (command != Command::Toggle) pendingPlayback.reset();
            if (!publishedTrack.hasSession && !publishedTrack.title.empty()) {
                WORD key = VK_MEDIA_PLAY_PAUSE;
                if (command == Command::Next) key = VK_MEDIA_NEXT_TRACK;
                else if (command == Command::Previous) key = VK_MEDIA_PREV_TRACK;
                deliveredMediaKey = SendMediaKey(key);
                if (deliveredMediaKey && command == Command::Toggle) {
                    snapshot = publishedTrack;
                    snapshot.positionMs = snapshot.GetCurrentInterpolatedPositionMs();
                    snapshot.isPlaying = !snapshot.isPlaying;
                    snapshot.lastUpdatedTime = Clock::now();
                    pendingPlayback = PendingPlayback{
                        snapshot.sourceAppId, snapshot.title, snapshot.artist,
                        snapshot.sourceProcessId, snapshot.isPlaying,
                        snapshot.positionMs, snapshot.lastUpdatedTime
                    };
                    publishedTrack = snapshot;
                }
            }
        }
        if (deliveredMediaKey) {
            if (command == Command::Toggle && callback) callback(snapshot);
            SignalRefresh();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            commands.push_back(command);
        }
        SignalRefresh();
    }

    void Publish(MediaTrackInfo track) {
        bool changed = false;
        MediaTrackInfo snapshot;
        {
            std::lock_guard<std::mutex> lock(trackMutex);
            if (pendingPlayback) {
                const auto& pending = *pendingPlayback;
                const bool sameTrack = !track.hasSession &&
                    track.sourceAppId == pending.sourceAppId &&
                    track.sourceProcessId == pending.sourceProcessId &&
                    track.title == pending.title && track.artist == pending.artist;
                if (!sameTrack || Clock::now() - pending.startedAt >= PlaybackCommandGrace ||
                    track.isPlaying == pending.isPlaying) {
                    pendingPlayback.reset();
                } else {
                    // The media key has been delivered, but Cloud Music's process
                    // snapshot still reports the previous state. Keep the icon and
                    // timeline stable until the player confirms the command.
                    track.isPlaying = pending.isPlaying;
                    track.positionMs = pending.positionMs;
                    track.lastUpdatedTime = pending.startedAt;
                }
            }
            changed = publishedTrack.title != track.title ||
                publishedTrack.artist != track.artist ||
                publishedTrack.album != track.album ||
                publishedTrack.hasSession != track.hasSession ||
                publishedTrack.sourceAppId != track.sourceAppId ||
                publishedTrack.sourceProcessId != track.sourceProcessId ||
                publishedTrack.isPlaying != track.isPlaying ||
                publishedTrack.positionMs != track.positionMs ||
                publishedTrack.durationMs != track.durationMs ||
                publishedTrack.thumbnailBytes != track.thumbnailBytes;
            if (changed) {
                publishedTrack = std::move(track);
                snapshot = publishedTrack;
            }
        }
        if (changed && callback) callback(snapshot);
    }

    void UnhookSelected() {
        if (selectedSession) {
            try {
                if (tokenPropsChanged.value) selectedSession.MediaPropertiesChanged(tokenPropsChanged);
                if (tokenPlaybackChanged.value) selectedSession.PlaybackInfoChanged(tokenPlaybackChanged);
                if (tokenTimelineChanged.value) selectedSession.TimelinePropertiesChanged(tokenTimelineChanged);
            } catch (...) {}
        }
        tokenPropsChanged = {};
        tokenPlaybackChanged = {};
        tokenTimelineChanged = {};
        selectedSession = nullptr;
    }

    void HookSelected(const Session& session) {
        if (SameSession(selectedSession, session)) return;
        UnhookSelected();
        selectedSession = session;
        if (!selectedSession) return;
        auto weak = weak_from_this();
        try {
            tokenPropsChanged = selectedSession.MediaPropertiesChanged(
                [weak](auto&&, auto&&) { if (auto self = weak.lock()) self->SignalRefresh(); });
            tokenPlaybackChanged = selectedSession.PlaybackInfoChanged(
                [weak](auto&&, auto&&) { if (auto self = weak.lock()) self->SignalRefresh(); });
            tokenTimelineChanged = selectedSession.TimelinePropertiesChanged(
                [weak](auto&&, auto&&) { if (auto self = weak.lock()) self->SignalRefresh(); });
        } catch (...) {}
    }

    void EnsureManager() {
        if (manager || Clock::now() < nextManagerAttempt) return;
        nextManagerAttempt = Clock::now() + std::chrono::seconds(2);
        try {
            manager = Manager::RequestAsync().get();
            auto weak = weak_from_this();
            tokenCurrentChanged = manager.CurrentSessionChanged(
                [weak](auto&&, auto&&) { if (auto self = weak.lock()) self->SignalRefresh(); });
            tokenSessionsChanged = manager.SessionsChanged(
                [weak](auto&&, auto&&) { if (auto self = weak.lock()) self->SignalRefresh(); });
            SignalRefresh();
        } catch (...) {
            manager = nullptr;
        }
    }

    void ExecuteCommands() {
        std::vector<Command> pending;
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            pending.swap(commands);
        }
        if (!selectedSession) {
            if (!netEaseActive) return;
            for (auto command : pending) {
                switch (command) {
                case Command::Toggle: SendMediaKey(VK_MEDIA_PLAY_PAUSE); break;
                case Command::Next: SendMediaKey(VK_MEDIA_NEXT_TRACK); break;
                case Command::Previous: SendMediaKey(VK_MEDIA_PREV_TRACK); break;
                }
            }
            if (!pending.empty()) SignalRefresh();
            return;
        }
        for (auto command : pending) {
            try {
                switch (command) {
                case Command::Toggle: {
                    bool accepted = false;
                    try {
                        auto playback = selectedSession.GetPlaybackInfo();
                        if (playback) {
                            const auto status = playback.PlaybackStatus();
                            if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                                accepted = selectedSession.TryPauseAsync().get();
                            } else if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
                                accepted = selectedSession.TryPlayAsync().get();
                            }
                        }
                    } catch (...) {}
                    if (!accepted) selectedSession.TryTogglePlayPauseAsync().get();
                    break;
                }
                case Command::Next: selectedSession.TrySkipNextAsync().get(); break;
                case Command::Previous: selectedSession.TrySkipPreviousAsync().get(); break;
                }
            } catch (...) {}
        }
        if (!pending.empty()) SignalRefresh();
    }

    void Refresh() {
        std::vector<Candidate> candidates;
        Session preferred = nullptr;
        try { if (manager) preferred = manager.GetCurrentSession(); } catch (...) {}
        try {
            if (manager) for (const auto& session : manager.GetSessions()) {
                Candidate candidate;
                candidate.session = session;
                candidate.track.hasSession = true;
                candidate.preferred = SameSession(session, preferred);
                candidate.selected = SameSession(session, selectedSession);
                try { candidate.source = session.SourceAppUserModelId().c_str(); } catch (...) {}
                candidate.track.sourceAppId = candidate.source;
                try {
                    auto playback = session.GetPlaybackInfo();
                    if (playback) {
                        const auto status = playback.PlaybackStatus();
                        candidate.playbackKnown = status ==
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ||
                            status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused ||
                            status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped;
                        candidate.track.isPlaying = status ==
                            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    }
                } catch (...) {}
                try {
                    candidate.properties = session.TryGetMediaPropertiesAsync().get();
                    if (candidate.properties) {
                        candidate.track.title = candidate.properties.Title().c_str();
                        candidate.track.artist = candidate.properties.Artist().c_str();
                        candidate.track.album = candidate.properties.AlbumTitle().c_str();
                    }
                } catch (...) {}
                if (!candidate.playbackKnown && candidate.selected) {
                    candidate.track.isPlaying = publishedTrack.isPlaying;
                }
                candidates.push_back(std::move(candidate));
            }
        } catch (...) {
            candidates.clear();
        }

        Candidate* current = nullptr;
        Candidate* best = nullptr;
        auto score = [](const Candidate& candidate) {
            return (candidate.track.isPlaying ? 100 : 0) +
                (candidate.track.title.empty() ? 0 : 50) +
                (candidate.preferred ? 10 : 0) + (candidate.selected ? 5 : 0);
        };
        for (auto& candidate : candidates) {
            if (candidate.selected || (!current && !selectedSource.empty() &&
                candidate.source == selectedSource)) current = &candidate;
            if (!best || score(candidate) > score(*best)) best = &candidate;
        }

        const bool namedSessionPlaying = std::any_of(candidates.begin(), candidates.end(),
            [](const Candidate& candidate) {
                return candidate.track.isPlaying && !candidate.track.title.empty();
            });
        if (!namedSessionPlaying) {
            if (auto netEaseTrack = netEaseSource.Read(); netEaseTrack &&
                (candidates.empty() || netEaseTrack->isPlaying || netEaseActive)) {
                netEaseActive = true;
                missingSince = {};
                pendingSwitchSource.clear();
                HookSelected(nullptr);
                selectedSource.clear();
                Publish(std::move(*netEaseTrack));
                return;
            }
        }

        const auto now = Clock::now();
        Candidate* chosen = current;
        if (!chosen || (chosen->track.title.empty() && best &&
            best->track.isPlaying && !best->track.title.empty())) {
            chosen = best;
        } else if (best && best != current && best->track.isPlaying &&
            !current->track.isPlaying) {
            if (pendingSwitchSource != best->source) {
                pendingSwitchSource = best->source;
                switchSince = now;
            }
            if (now - switchSince >= AutoSwitchGrace) chosen = best;
        } else {
            pendingSwitchSource.clear();
        }

        if (!chosen || chosen->track.title.empty()) {
            netEaseActive = false;
            if (missingSince.time_since_epoch().count() == 0) missingSince = now;
            if (!chosen) {
                HookSelected(nullptr);
                if (publishedTrack.hasSession && now - missingSince < MissingSessionGrace) {
                    MediaTrackInfo held = publishedTrack;
                    held.hasSession = false;
                    Publish(std::move(held));
                }
            }
            if (chosen && !publishedTrack.title.empty() &&
                chosen->source == selectedSource && now - missingSince < MissingSessionGrace) {
                MediaTrackInfo held = publishedTrack;
                held.isPlaying = chosen->track.isPlaying;
                held.lastUpdatedTime = now;
                HookSelected(chosen->session);
                Publish(std::move(held));
            }
            if (now - missingSince >= MissingSessionGrace) {
                if (chosen) {
                    HookSelected(chosen->session);
                    selectedSource = chosen->source;
                    Publish(std::move(chosen->track));
                } else {
                    selectedSource.clear();
                    Publish({});
                }
            }
            return;
        }

        missingSince = {};
        netEaseActive = false;
        pendingSwitchSource.clear();
        const bool trackChanged = chosen->track.title != publishedTrack.title ||
            chosen->track.artist != publishedTrack.artist ||
            chosen->track.album != publishedTrack.album ||
            chosen->source != selectedSource;
        if (!trackChanged) chosen->track.thumbnailBytes = publishedTrack.thumbnailBytes;
        if (trackChanged || chosen->track.thumbnailBytes.empty()) {
            chosen->track.thumbnailBytes = ReadThumbnail(chosen->properties);
        }
        try {
            auto timeline = chosen->session.GetTimelineProperties();
            if (timeline) {
                chosen->track.positionMs = timeline.Position().count() / 10000;
                chosen->track.durationMs = timeline.EndTime().count() / 10000;
            }
        } catch (...) {}
        chosen->track.lastUpdatedTime = now;
        chosen->track.sourceProcessId = ResolveProcess(chosen->source);
        HookSelected(chosen->session);
        selectedSource = chosen->source;
        Publish(std::move(chosen->track));
    }

    void Run(std::stop_token stopToken) {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
        catch (...) { return; }
        Clock::time_point lastRefresh{};
        while (!stopToken.stop_requested()) {
            EnsureManager();
            ExecuteCommands();
            const auto now = Clock::now();
            const bool retrying = missingSince.time_since_epoch().count() != 0 ||
                !pendingSwitchSource.empty();
            const auto interval = retrying || netEaseActive
                ? std::chrono::milliseconds(200) : std::chrono::seconds(2);
            if (refreshRequested.exchange(false) ||
                lastRefresh.time_since_epoch().count() == 0 || now - lastRefresh >= interval) {
                Refresh();
                lastRefresh = Clock::now();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        UnhookSelected();
        if (manager) {
            try {
                if (tokenCurrentChanged.value) manager.CurrentSessionChanged(tokenCurrentChanged);
                if (tokenSessionsChanged.value) manager.SessionsChanged(tokenSessionsChanged);
            } catch (...) {}
        }
        manager = nullptr;
    }
};

MediaSession::MediaSession() : m_impl(std::make_shared<Impl>()) {}
MediaSession::~MediaSession() { Shutdown(); }

bool MediaSession::Initialize(HWND /*hNotifyWnd*/, TrackChangedCallback callback) {
    m_impl->callback = std::move(callback);
    auto self = m_impl;
    m_impl->worker = std::jthread([self](std::stop_token stopToken) { self->Run(stopToken); });
    return true;
}

void MediaSession::Shutdown() {
    if (m_impl && m_impl->worker.joinable()) {
        m_impl->worker.request_stop();
        m_impl->worker.join();
    }
}

void MediaSession::PlayPause() { if (m_impl) m_impl->Queue(Impl::Command::Toggle); }
void MediaSession::NextTrack() { if (m_impl) m_impl->Queue(Impl::Command::Next); }
void MediaSession::PreviousTrack() { if (m_impl) m_impl->Queue(Impl::Command::Previous); }

MediaTrackInfo MediaSession::GetCurrentTrack() const {
    if (!m_impl) return {};
    std::lock_guard<std::mutex> lock(m_impl->trackMutex);
    return m_impl->publishedTrack;
}

void MediaSession::RefreshCurrentTrack() { if (m_impl) m_impl->SignalRefresh(); }

} // namespace MusicBar
