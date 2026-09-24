#include "MediaSession.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <thread>
#include <atomic>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace MusicBar {

struct MediaSession::Impl {
    HWND hNotifyWnd = nullptr;
    TrackChangedCallback callback;
    GlobalSystemMediaTransportControlsSessionManager sessionManager = nullptr;
    GlobalSystemMediaTransportControlsSession currentSession = nullptr;

    winrt::event_token tokenSessionChanged{};
    winrt::event_token tokenPropsChanged{};
    winrt::event_token tokenPlaybackChanged{};
    winrt::event_token tokenTimelineChanged{};

    std::mutex trackMutex;
    MediaTrackInfo currentTrack;

    void HookSession(GlobalSystemMediaTransportControlsSession session) {
        UnhookCurrentSession();
        currentSession = session;
        if (!currentSession) return;

        try {
            tokenPropsChanged = currentSession.MediaPropertiesChanged(
                [this](auto&&, auto&&) {
                    UpdateTrackProperties();
                }
            );
            tokenPlaybackChanged = currentSession.PlaybackInfoChanged(
                [this](auto&&, auto&&) {
                    UpdatePlaybackInfo();
                }
            );
            tokenTimelineChanged = currentSession.TimelinePropertiesChanged(
                [this](auto&&, auto&&) {
                    UpdateTimelineInfo();
                }
            );
        } catch (...) {}

        UpdateAll();
    }

    void UnhookCurrentSession() {
        if (!currentSession) return;
        try {
            if (tokenPropsChanged.value) currentSession.MediaPropertiesChanged(tokenPropsChanged);
            if (tokenPlaybackChanged.value) currentSession.PlaybackInfoChanged(tokenPlaybackChanged);
            if (tokenTimelineChanged.value) currentSession.TimelinePropertiesChanged(tokenTimelineChanged);
        } catch (...) {}
        tokenPropsChanged = {};
        tokenPlaybackChanged = {};
        tokenTimelineChanged = {};
        currentSession = nullptr;
    }

    void UpdateAll() {
        if (!currentSession) {
            std::lock_guard<std::mutex> lock(trackMutex);
            currentTrack = MediaTrackInfo{};
            if (callback) callback(currentTrack);
            return;
        }

        UpdateTrackProperties();
        UpdatePlaybackInfo();
        UpdateTimelineInfo();
    }

    void UpdateTrackProperties() {
        if (!currentSession) return;
        try {
            auto asyncOp = currentSession.TryGetMediaPropertiesAsync();
            auto props = asyncOp.get();
            if (!props) return;

            std::wstring title = props.Title().c_str();
            std::wstring artist = props.Artist().c_str();
            std::wstring album = props.AlbumTitle().c_str();
            std::vector<uint8_t> thumb;

            auto thumbRef = props.Thumbnail();
            if (thumbRef) {
                try {
                    auto stream = thumbRef.OpenReadAsync().get();
                    uint32_t size = static_cast<uint32_t>(stream.Size());
                    if (size > 0 && size < 10 * 1024 * 1024) {
                        Buffer buffer(size);
                        stream.ReadAsync(buffer, size, InputStreamOptions::None).get();
                        auto reader = DataReader::FromBuffer(buffer);
                        thumb.resize(size);
                        reader.ReadBytes(thumb);
                    }
                } catch (...) {}
            }

            {
                std::lock_guard<std::mutex> lock(trackMutex);
                currentTrack.title = std::move(title);
                currentTrack.artist = std::move(artist);
                currentTrack.album = std::move(album);
                if (!thumb.empty()) {
                    currentTrack.thumbnailBytes = std::move(thumb);
                }
            }

            if (callback) {
                std::lock_guard<std::mutex> lock(trackMutex);
                callback(currentTrack);
            }
        } catch (...) {}
    }

    void UpdatePlaybackInfo() {
        if (!currentSession) return;
        try {
            auto info = currentSession.GetPlaybackInfo();
            if (!info) return;

            bool playing = (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            {
                std::lock_guard<std::mutex> lock(trackMutex);
                currentTrack.isPlaying = playing;
                currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
            }

            if (callback) {
                std::lock_guard<std::mutex> lock(trackMutex);
                callback(currentTrack);
            }
        } catch (...) {}
    }

    void UpdateTimelineInfo() {
        if (!currentSession) return;
        try {
            auto timeline = currentSession.GetTimelineProperties();
            if (!timeline) return;

            int64_t posMs = timeline.Position().count() / 10000; // 100ns units to ms
            int64_t durMs = timeline.EndTime().count() / 10000;

            {
                std::lock_guard<std::mutex> lock(trackMutex);
                currentTrack.positionMs = posMs;
                currentTrack.durationMs = durMs;
                currentTrack.lastUpdatedTime = std::chrono::steady_clock::now();
            }

            if (callback) {
                std::lock_guard<std::mutex> lock(trackMutex);
                callback(currentTrack);
            }
        } catch (...) {}
    }
};

MediaSession::MediaSession() : m_impl(std::make_unique<Impl>()) {}
MediaSession::~MediaSession() { Shutdown(); }

bool MediaSession::Initialize(HWND hNotifyWnd, TrackChangedCallback callback) {
    m_impl->hNotifyWnd = hNotifyWnd;
    m_impl->callback = callback;

    try {
        winrt::init_apartment();
    } catch (...) {}

    // Initialize session manager on background thread to avoid blocking UI initialization
    std::thread([this]() {
        try {
            winrt::init_apartment();
            auto op = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
            m_impl->sessionManager = op.get();

            if (m_impl->sessionManager) {
                m_impl->tokenSessionChanged = m_impl->sessionManager.CurrentSessionChanged(
                    [this](auto&&, auto&&) {
                        try {
                            auto active = m_impl->sessionManager.GetCurrentSession();
                            m_impl->HookSession(active);
                        } catch (...) {}
                    }
                );

                auto active = m_impl->sessionManager.GetCurrentSession();
                m_impl->HookSession(active);
            }
        } catch (...) {}
    }).detach();

    return true;
}

void MediaSession::Shutdown() {
    if (m_impl) {
        if (m_impl->sessionManager && m_impl->tokenSessionChanged.value) {
            try {
                m_impl->sessionManager.CurrentSessionChanged(m_impl->tokenSessionChanged);
            } catch (...) {}
            m_impl->tokenSessionChanged = {};
        }
        m_impl->UnhookCurrentSession();
    }
}

void MediaSession::PlayPause() {
    if (!m_impl || !m_impl->currentSession) return;
    std::thread([this]() {
        try {
            m_impl->currentSession.TryTogglePlayPauseAsync().get();
        } catch (...) {}
    }).detach();
}

void MediaSession::NextTrack() {
    if (!m_impl || !m_impl->currentSession) return;
    std::thread([this]() {
        try {
            m_impl->currentSession.TrySkipNextAsync().get();
        } catch (...) {}
    }).detach();
}

void MediaSession::PreviousTrack() {
    if (!m_impl || !m_impl->currentSession) return;
    std::thread([this]() {
        try {
            m_impl->currentSession.TrySkipPreviousAsync().get();
        } catch (...) {}
    }).detach();
}

MediaTrackInfo MediaSession::GetCurrentTrack() const {
    if (!m_impl) return {};
    std::lock_guard<std::mutex> lock(m_impl->trackMutex);
    return m_impl->currentTrack;
}

void MediaSession::RefreshCurrentTrack() {
    if (m_impl) {
        m_impl->UpdateAll();
    }
}

} // namespace MusicBar
