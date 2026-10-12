#include "AnimationClock.hpp"

namespace MusicBar {

namespace {
constexpr int kShutdownWaitMs = 2000;
constexpr LONGLONG kHundredNanosecondsPerMs = 10000;
} // namespace

AnimationClock::~AnimationClock() {
    Stop();
}

bool AnimationClock::Start(HWND target, UINT message, int periodMs) {
    if (m_thread) return true;
    if (!target || message == 0 || periodMs <= 0) return false;

    m_target = target;
    m_message = message;
    m_periodMs = periodMs;
    m_tickPending = 0;

    m_timer = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!m_timer) {
        // Pre-1803 systems only have the regular timer.
        m_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (!m_timer) return false;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) {
        CloseHandle(m_timer);
        m_timer = nullptr;
        return false;
    }

    m_thread = CreateThread(nullptr, 0, &AnimationClock::ThreadProc, this, 0, nullptr);
    if (!m_thread) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
        CloseHandle(m_timer);
        m_timer = nullptr;
        return false;
    }
    return true;
}

void AnimationClock::Stop() {
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread) {
        WaitForSingleObject(m_thread, kShutdownWaitMs);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_timer) {
        CancelWaitableTimer(m_timer);
        CloseHandle(m_timer);
        m_timer = nullptr;
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    m_target = nullptr;
    m_tickPending = 0;
}

void AnimationClock::Consumed() {
    InterlockedExchange(&m_tickPending, 0);
}

DWORD WINAPI AnimationClock::ThreadProc(void* parameter) {
    static_cast<AnimationClock*>(parameter)->Run();
    return 0;
}

void AnimationClock::Run() {
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(m_periodMs) * kHundredNanosecondsPerMs;
    if (!SetWaitableTimer(m_timer, &due, m_periodMs, nullptr, nullptr, FALSE)) {
        return;
    }

    const HANDLE waits[2] = { m_timer, m_stopEvent };
    for (;;) {
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (result != WAIT_OBJECT_0) break;
        if (InterlockedCompareExchange(&m_tickPending, 1, 0) != 0) continue;
        if (!PostMessageW(m_target, m_message, 0, 0)) {
            InterlockedExchange(&m_tickPending, 0);
            break;
        }
    }
}

} // namespace MusicBar
