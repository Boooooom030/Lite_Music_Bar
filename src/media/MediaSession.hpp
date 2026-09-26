#pragma once

#include <windows.h>
#include <functional>
#include <memory>
#include <mutex>
#include "core/Types.hpp"

namespace MusicBar {

class MediaSession {
public:
    using TrackChangedCallback = std::function<void(const MediaTrackInfo&)>;

    MediaSession();
    ~MediaSession();

    bool Initialize(HWND hNotifyWnd, TrackChangedCallback callback);
    void Shutdown();

    void PlayPause();
    void NextTrack();
    void PreviousTrack();

    MediaTrackInfo GetCurrentTrack() const;
    void RefreshCurrentTrack();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace MusicBar
