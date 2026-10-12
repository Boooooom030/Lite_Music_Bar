#pragma once

#include <windows.h>

namespace MusicBar {

// Drives the animation tick from a high-resolution waitable timer.
//
// SetTimer() is quantized by the system: a 16 ms request lands on the next
// timer quantum (measured 25 ms on Windows 11 26200, and timeBeginPeriod(1)
// does not change it), which caps the render loop at ~40 fps. A waitable timer
// created with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION keeps the requested
// interval, and the tick is posted to the UI thread so all window and Direct2D
// work stays on that thread.
//
// At most one tick is queued at a time: if the UI thread has not consumed the
// previous tick yet, the extra tick is dropped instead of piling up.
class AnimationClock {
public:
    AnimationClock() = default;
    ~AnimationClock();
    AnimationClock(const AnimationClock&) = delete;
    AnimationClock& operator=(const AnimationClock&) = delete;

    bool Start(HWND target, UINT message, int periodMs);
    void Stop();

    // Called on the UI thread when the tick message has been handled.
    void Consumed();

private:
    static DWORD WINAPI ThreadProc(void* parameter);
    void Run();

    HWND m_target = nullptr;
    UINT m_message = 0;
    int m_periodMs = 16;
    HANDLE m_timer = nullptr;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_thread = nullptr;
    volatile LONG m_tickPending = 0;
};

} // namespace MusicBar
