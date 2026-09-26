#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>
#include "audio/VolumeController.hpp"

namespace MusicBar {

class VolumePopup {
public:
    ~VolumePopup();

    bool Initialize(HINSTANCE instance);
    void SetLightTheme(bool lightTheme);
    void Toggle(const RECT& anchorScreen, UINT dpi);
    void Reposition(const RECT& anchorScreen, UINT dpi);
    void Hide();
    bool IsVisible() const;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool RefreshEndpoint();
    void SetVolumeFromX(int x);
    void CaptureBackdrop();
    void UpdateShapeRegion(int width, int height);
    void StartAnimation(int fromY, BYTE fromAlpha, int toY, BYTE toAlpha,
        ULONGLONG durationMs);
    void AdvanceAnimation();
    void Paint();

    HWND m_hwnd = nullptr;
    RECT m_anchorScreen{};
    RECT m_targetRect{};
    UINT m_dpi = 96;
    float m_volume = 0.0f;
    bool m_dragging = false;
    ULONGLONG m_lastInsideTick = 0;
    ULONGLONG m_animationStartTick = 0;
    ULONGLONG m_animationDurationMs = 0;
    int m_animationFromY = 0;
    int m_animationToY = 0;
    BYTE m_animationFromAlpha = 255;
    BYTE m_animationToAlpha = 255;
    BYTE m_displayAlpha = 255;
    bool m_animating = false;
    bool m_isLightTheme = false;
    std::vector<uint8_t> m_blurredBackdrop;
    std::vector<uint8_t> m_shapeAlpha;
    std::vector<uint8_t> m_borderAlpha;
    VolumeController m_volumeController;
};

} // namespace MusicBar
