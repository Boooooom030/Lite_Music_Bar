#pragma once

#include <algorithm>

namespace MusicBar {

inline float LyricScrollTarget(float textWidth, float viewportWidth,
    float focusX, float lineProgress) {
    const float maxScroll = std::max(0.0f, textWidth - viewportWidth);
    if (maxScroll <= 0.0f) return 0.0f;

    const float following = std::clamp(focusX - viewportWidth * 0.48f,
        0.0f, maxScroll);
    // Bring the last words fully into view before the next line replaces them.
    const float ending = std::clamp((lineProgress - 0.65f) / 0.20f,
        0.0f, 1.0f);
    const float easedEnding = ending * ending * (3.0f - 2.0f * ending);
    return std::max(following, maxScroll * easedEnding);
}

} // namespace MusicBar
