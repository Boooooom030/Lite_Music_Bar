#include "VolumePopup.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MusicBar {

namespace {

struct PopupPalette {
    COLORREF tint;
    COLORREF text;
    COLORREF border;
    COLORREF track;
    COLORREF accent;
    float tintMix;
};

PopupPalette PaletteFor(bool lightTheme) {
    return lightTheme
        ? PopupPalette{RGB(238, 242, 248), RGB(32, 38, 47),
            RGB(61, 76, 94), RGB(179, 190, 204),
            RGB(0, 122, 217), 0.66f}
        : PopupPalette{RGB(103, 111, 124), RGB(239, 243, 248),
            RGB(226, 235, 246), RGB(117, 129, 144),
            RGB(77, 194, 255), 0.66f};
}

bool InsideSuperellipse(float x, float y, float width, float height,
    float radius) {
    if (x < 0.0f || y < 0.0f || x >= width || y >= height ||
        radius <= 0.0f) return false;
    const float centerX = std::clamp(x, radius, width - radius);
    const float centerY = std::clamp(y, radius, height - radius);
    const float dx = (x - centerX) / radius;
    const float dy = (y - centerY) / radius;
    const float dx2 = dx * dx;
    const float dy2 = dy * dy;
    return dx2 * dx2 + dy2 * dy2 <= 1.0f;
}

uint8_t ShapeCoverage(int x, int y, float width, float height,
    float radius, float inset = 0.0f) {
    int inside = 0;
    for (int sampleY = 0; sampleY < 4; ++sampleY) {
        for (int sampleX = 0; sampleX < 4; ++sampleX) {
            inside += InsideSuperellipse(
                x + (sampleX + 0.5f) * 0.25f - inset,
                y + (sampleY + 0.5f) * 0.25f - inset,
                width - inset * 2.0f, height - inset * 2.0f,
                std::max(1.0f, radius - inset)) ? 1 : 0;
        }
    }
    return static_cast<uint8_t>((inside * 255 + 8) / 16);
}

void BlendPixel(uint8_t* pixel, COLORREF color, float opacity) {
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    const float remaining = 1.0f - opacity;
    pixel[0] = static_cast<uint8_t>(std::lround(
        pixel[0] * remaining + GetBValue(color) * opacity));
    pixel[1] = static_cast<uint8_t>(std::lround(
        pixel[1] * remaining + GetGValue(color) * opacity));
    pixel[2] = static_cast<uint8_t>(std::lround(
        pixel[2] * remaining + GetRValue(color) * opacity));
}

float CapsuleCoverage(int x, int y, float left, float right,
    float centerY, float radius) {
    if (right <= left) return 0.0f;
    radius = std::min(radius, (right - left) * 0.5f);
    int inside = 0;
    for (int sampleY = 0; sampleY < 4; ++sampleY) {
        for (int sampleX = 0; sampleX < 4; ++sampleX) {
            const float px = x + (sampleX + 0.5f) * 0.25f;
            const float py = y + (sampleY + 0.5f) * 0.25f;
            const float centerX = std::clamp(px, left + radius,
                right - radius);
            const float dx = px - centerX;
            const float dy = py - centerY;
            inside += dx * dx + dy * dy <= radius * radius ? 1 : 0;
        }
    }
    return inside / 16.0f;
}

} // namespace

VolumePopup::~VolumePopup() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}

bool VolumePopup::Initialize(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32649));
    wc.lpszClassName = L"MusicBar_Volume_Popup";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
        WS_EX_LAYERED | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!m_hwnd) return false;
    return true;
}

void VolumePopup::SetLightTheme(bool lightTheme) {
    if (m_isLightTheme == lightTheme) return;
    m_isLightTheme = lightTheme;
    if (!IsVisible()) return;
    ShowWindow(m_hwnd, SW_HIDE);
    CaptureBackdrop();
    Paint();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_lastInsideTick = GetTickCount64();
}

bool VolumePopup::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void VolumePopup::Toggle(const RECT& anchorScreen, UINT dpi) {
    if (IsVisible()) {
        RECT current{};
        GetWindowRect(m_hwnd, &current);
        if (m_animating && m_animationToAlpha == 0) {
            m_lastInsideTick = GetTickCount64();
            StartAnimation(current.top, m_displayAlpha,
                m_targetRect.top, 255, 240);
            return;
        }
        const int offset = MulDiv(8, m_dpi, 96) *
            (m_targetRect.bottom <= m_anchorScreen.top ? 1 : -1);
        StartAnimation(current.top, m_displayAlpha,
            m_targetRect.top + offset, 0, 170);
        return;
    }
    if (!m_hwnd) return;
    RefreshEndpoint();
    Reposition(anchorScreen, dpi);
    CaptureBackdrop();
    m_lastInsideTick = GetTickCount64();
    const int offset = MulDiv(8, m_dpi, 96) *
        (m_targetRect.bottom <= m_anchorScreen.top ? 1 : -1);
    const int startY = m_targetRect.top + offset;
    m_displayAlpha = 0;
    SetWindowPos(m_hwnd, HWND_TOPMOST, m_targetRect.left, startY,
        m_targetRect.right - m_targetRect.left,
        m_targetRect.bottom - m_targetRect.top,
        SWP_NOACTIVATE);
    Paint();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    StartAnimation(startY, 0, m_targetRect.top, 255, 240);
}

void VolumePopup::Reposition(const RECT& anchorScreen, UINT dpi) {
    if (!m_hwnd) return;
    if (IsVisible() && EqualRect(&anchorScreen, &m_anchorScreen) &&
        (dpi ? dpi : 96) == m_dpi) return;
    if (IsVisible()) Hide();
    m_anchorScreen = anchorScreen;
    m_dpi = dpi ? dpi : 96;
    int width = MulDiv(196, m_dpi, 96);
    int height = MulDiv(64, m_dpi, 96);
    const int gap = MulDiv(8, m_dpi, 96);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    HMONITOR nearest = MonitorFromRect(&anchorScreen, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(nearest, &monitor)) return;
    const RECT work = monitor.rcWork;
    width = std::min(width, static_cast<int>(work.right - work.left));
    height = std::min(height, static_cast<int>(work.bottom - work.top));
    int x = (anchorScreen.left + anchorScreen.right - width) / 2;
    x = std::clamp(x, static_cast<int>(work.left),
        std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    int y = anchorScreen.top - height - gap;
    if (y < work.top) y = anchorScreen.bottom + gap;
    y = std::clamp(y, static_cast<int>(work.top),
        std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
    m_targetRect = RECT{x, y, x + width, y + height};
    SetWindowPos(m_hwnd, nullptr, x, y, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateShapeRegion(width, height);
}

void VolumePopup::UpdateShapeRegion(int width, int height) {
    if (width <= 0 || height <= 0) return;
    const size_t pixels = static_cast<size_t>(width) * height;
    m_shapeAlpha.resize(pixels);
    m_borderAlpha.resize(pixels);
    const float radius = std::min(static_cast<float>(MulDiv(23, m_dpi, 96)),
        std::min(width, height) * 0.5f);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    for (int y = 0; y < height; ++y) {
        int first = width;
        int last = -1;
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            const uint8_t outer = ShapeCoverage(x, y,
                static_cast<float>(width), static_cast<float>(height), radius);
            const uint8_t inner = ShapeCoverage(x, y,
                static_cast<float>(width), static_cast<float>(height),
                radius, 1.25f);
            m_shapeAlpha[index] = outer;
            m_borderAlpha[index] = outer > inner ? outer - inner : 0;
            if (outer) {
                first = std::min(first, x);
                last = x;
            }
        }
        if (region && last >= first) {
            HRGN row = CreateRectRgn(first, y, last + 1, y + 1);
            if (row) {
                CombineRgn(region, region, row, RGN_OR);
                DeleteObject(row);
            }
        }
    }
    if (region && !SetWindowRgn(m_hwnd, region, TRUE)) {
        DeleteObject(region);
    }
}

void VolumePopup::Hide() {
    if (!m_hwnd) return;
    if (m_dragging) {
        m_dragging = false;
        ReleaseCapture();
    }
    KillTimer(m_hwnd, 1);
    m_animating = false;
    ShowWindow(m_hwnd, SW_HIDE);
}

bool VolumePopup::RefreshEndpoint() {
    return m_volumeController.ConnectDefault() &&
        m_volumeController.Read(m_volume);
}

void VolumePopup::SetVolumeFromX(int x) {
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const int padding = MulDiv(16, m_dpi, 96);
    const int width = std::max(1, static_cast<int>(client.right) - 2 * padding);
    const float value = std::clamp(static_cast<float>(x - padding) / width,
        0.0f, 1.0f);
    if (!m_volumeController.Write(value)) return;
    m_volume = value;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void VolumePopup::CaptureBackdrop() {
    const int width = m_targetRect.right - m_targetRect.left;
    const int height = m_targetRect.bottom - m_targetRect.top;
    if (width <= 0 || height <= 0) return;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> source(bytes, 0);
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = screen ? CreateDIBSection(screen, &info,
        DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool captured = false;
    if (memory && bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        captured = BitBlt(memory, 0, 0, width, height, screen,
            m_targetRect.left, m_targetRect.top, SRCCOPY | CAPTUREBLT) != FALSE;
        if (captured) std::memcpy(source.data(), bits, bytes);
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);

    m_blurredBackdrop.resize(bytes);
    const auto palette = PaletteFor(m_isLightTheme);
    const uint8_t tint[3] = {GetBValue(palette.tint),
        GetGValue(palette.tint), GetRValue(palette.tint)};
    if (!captured) {
        for (size_t pixel = 0; pixel < bytes; pixel += 4) {
            m_blurredBackdrop[pixel] = tint[0];
            m_blurredBackdrop[pixel + 1] = tint[1];
            m_blurredBackdrop[pixel + 2] = tint[2];
            m_blurredBackdrop[pixel + 3] = 255;
        }
        return;
    }

    // A DPI-scaled Gaussian blur keeps the captured scene recognizable by
    // color while removing the sharp detail that looked transparent.
    const int radius = std::clamp(MulDiv(10, m_dpi, 96), 6, 24);
    const float sigma = radius * 0.45f;
    std::vector<float> weights(static_cast<size_t>(radius * 2 + 1));
    float weightSum = 0.0f;
    for (int tap = -radius; tap <= radius; ++tap) {
        const float distance = static_cast<float>(tap) / sigma;
        const float weight = std::exp(-0.5f * distance * distance);
        weights[static_cast<size_t>(tap + radius)] = weight;
        weightSum += weight;
    }
    for (float& weight : weights) weight /= weightSum;
    std::vector<uint8_t> horizontal(bytes);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                float sum = 0.0f;
                for (int tap = -radius; tap <= radius; ++tap) {
                    const int sampleX = std::clamp(x + tap, 0, width - 1);
                    sum += weights[static_cast<size_t>(tap + radius)] * source[
                        (static_cast<size_t>(y) * width + sampleX) * 4 + channel];
                }
                horizontal[(static_cast<size_t>(y) * width + x) * 4 + channel] =
                    static_cast<uint8_t>(std::lround(std::clamp(sum, 0.0f, 255.0f)));
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                float sum = 0.0f;
                for (int tap = -radius; tap <= radius; ++tap) {
                    const int sampleY = std::clamp(y + tap, 0, height - 1);
                    sum += weights[static_cast<size_t>(tap + radius)] * horizontal[
                        (static_cast<size_t>(sampleY) * width + x) * 4 + channel];
                }
                m_blurredBackdrop[pixel + channel] = static_cast<uint8_t>(
                    std::clamp(sum * (1.0f - palette.tintMix) +
                        tint[channel] * palette.tintMix,
                        0.0f, m_isLightTheme ? 255.0f : 124.0f));
            }
            m_blurredBackdrop[pixel + 3] = 255;
        }
    }
}

void VolumePopup::StartAnimation(int fromY, BYTE fromAlpha, int toY,
    BYTE toAlpha, ULONGLONG durationMs) {
    m_animationFromY = fromY;
    m_animationToY = toY;
    m_animationFromAlpha = fromAlpha;
    m_animationToAlpha = toAlpha;
    m_animationDurationMs = durationMs;
    m_animationStartTick = GetTickCount64();
    m_animating = true;
    SetTimer(m_hwnd, 1, 16, nullptr);
}

void VolumePopup::AdvanceAnimation() {
    if (!m_animating) return;
    const float progress = std::clamp(static_cast<float>(
        GetTickCount64() - m_animationStartTick) /
        std::max<ULONGLONG>(1, m_animationDurationMs), 0.0f, 1.0f);
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const int y = static_cast<int>(std::lround(m_animationFromY +
        (m_animationToY - m_animationFromY) * eased));
    m_displayAlpha = static_cast<BYTE>(std::lround(m_animationFromAlpha +
        (static_cast<int>(m_animationToAlpha) - m_animationFromAlpha) * eased));
    SetWindowPos(m_hwnd, nullptr, m_targetRect.left, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Paint();
    if (progress >= 1.0f) {
        m_animating = false;
        if (m_animationToAlpha == 0) Hide();
        else SetTimer(m_hwnd, 1, 120, nullptr);
    }
}

void VolumePopup::Paint() {
    if (!m_hwnd) return;
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) return;
    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
        &bits, nullptr, 0);
    if (!memory || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    const auto palette = PaletteFor(m_isLightTheme);
    if (m_blurredBackdrop.size() == bytes) {
        std::memcpy(bits, m_blurredBackdrop.data(), bytes);
    } else {
        HBRUSH background = CreateSolidBrush(palette.tint);
        FillRect(memory, &client, background);
        DeleteObject(background);
    }

    HFONT font = CreateFontW(-MulDiv(12, m_dpi, 96), 0, 0, 0,
        FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = font ? SelectObject(memory, font) : nullptr;
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, palette.text);
    const int padding = MulDiv(16, m_dpi, 96);
    RECT title{padding, MulDiv(8, m_dpi, 96), width - padding,
        MulDiv(31, m_dpi, 96)};
    DrawTextW(memory, L"音量", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const std::wstring percent =
        std::to_wstring(static_cast<int>(std::lround(m_volume * 100.0f))) + L"%";
    DrawTextW(memory, percent.c_str(), -1, &title,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int trackY = MulDiv(45, m_dpi, 96);
    const float trackRadius = std::max(2.0f,
        MulDiv(5, m_dpi, 96) * 0.5f);
    const int thumbRadius = std::max(4, MulDiv(6, m_dpi, 96));
    const int right = width - padding;
    const int fillRight = padding + static_cast<int>((right - padding) * m_volume);
    if (oldFont) SelectObject(memory, oldFont);
    if (font) DeleteObject(font);

    auto* pixels = static_cast<uint8_t*>(bits);
    const int sliderTop = std::max(0, trackY - thumbRadius - 1);
    const int sliderBottom = std::min(height, trackY + thumbRadius + 2);
    for (int y = sliderTop; y < sliderBottom; ++y) {
        for (int x = std::max(0, padding - thumbRadius - 1);
            x < std::min(width, right + thumbRadius + 2); ++x) {
            auto* pixel = pixels + (static_cast<size_t>(y) * width + x) * 4;
            BlendPixel(pixel, palette.track, CapsuleCoverage(x, y,
                static_cast<float>(padding), static_cast<float>(right),
                static_cast<float>(trackY), trackRadius));
            if (fillRight > padding) {
                BlendPixel(pixel, palette.accent, CapsuleCoverage(x, y,
                    static_cast<float>(padding), static_cast<float>(fillRight),
                    static_cast<float>(trackY), trackRadius));
            }
            BlendPixel(pixel, palette.accent, CapsuleCoverage(x, y,
                static_cast<float>(fillRight - thumbRadius),
                static_cast<float>(fillRight + thumbRadius),
                static_cast<float>(trackY), static_cast<float>(thumbRadius)));
        }
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixelCount; ++i) {
        auto* pixel = pixels + i * 4;
        const uint8_t alpha = m_shapeAlpha.size() == pixelCount
            ? m_shapeAlpha[i] : 255;
        if (m_borderAlpha.size() == pixelCount) {
            BlendPixel(pixel, palette.border,
                m_borderAlpha[i] / 255.0f * 0.38f);
        }
        for (int channel = 0; channel < 3; ++channel) {
            pixel[channel] = static_cast<uint8_t>(
                (static_cast<unsigned>(pixel[channel]) * alpha + 127) / 255);
        }
        pixel[3] = alpha;
    }
    RECT position{};
    GetWindowRect(m_hwnd, &position);
    POINT destination{position.left, position.top};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, m_displayAlpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, screen, &destination, &size,
        memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK VolumePopup::WindowProc(HWND hwnd, UINT message,
    WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<VolumePopup*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<VolumePopup*>(GetWindowLongPtrW(hwnd,
        GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT VolumePopup::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        Paint();
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (GET_Y_LPARAM(lParam) >= MulDiv(27, m_dpi, 96)) {
            m_dragging = true;
            SetCapture(m_hwnd);
            SetVolumeFromX(GET_X_LPARAM(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (m_dragging) SetVolumeFromX(GET_X_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (m_dragging) {
            SetVolumeFromX(GET_X_LPARAM(lParam));
            m_dragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        m_dragging = false;
        return 0;
    case WM_MOUSEWHEEL: {
        const float nextVolume = std::clamp(m_volume +
            GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f * 0.05f, 0.0f, 1.0f);
        if (m_volumeController.Write(nextVolume)) m_volume = nextVolume;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER: {
        AdvanceAnimation();
        if (!IsVisible()) return 0;
        if (m_animating && m_animationToAlpha == 0) return 0;
        if (!m_dragging) {
            float current = m_volume;
            if (m_volumeController.Read(current) &&
                std::abs(current - m_volume) > 0.001f) {
                m_volume = current;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
        POINT cursor{};
        GetCursorPos(&cursor);
        RECT popup{};
        GetWindowRect(m_hwnd, &popup);
        if (m_dragging || PtInRect(&popup, cursor) ||
            PtInRect(&m_anchorScreen, cursor)) {
            m_lastInsideTick = GetTickCount64();
        } else if (GetTickCount64() - m_lastInsideTick > 900) {
            Hide();
        }
        return 0;
    }
    default:
        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}

} // namespace MusicBar
