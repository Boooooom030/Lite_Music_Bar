#include "SettingsWindow.hpp"

#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <cmath>


namespace MusicBar {
namespace {

D2D1_COLOR_F Color(unsigned rgb, float alpha = 1.0f) {
    return D2D1::ColorF(static_cast<float>((rgb >> 16) & 255) / 255.0f,
        static_cast<float>((rgb >> 8) & 255) / 255.0f,
        static_cast<float>(rgb & 255) / 255.0f, alpha);
}

constexpr std::array<const wchar_t*, 6> kSources = {
    L"自动选择", L"网易云音乐", L"QQ 音乐", L"酷狗音乐", L"汽水音乐", L"LRCLIB"
};
constexpr std::array<const wchar_t*, 2> kCaptureModes = {
    L"当前播放器", L"系统声音"
};

bool InRect(float x, float y, const D2D1_RECT_F& rect) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

SettingsWindow::~SettingsWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
}

void SettingsWindow::Show(HINSTANCE instance, const AppSettings& settings,
    std::function<void(const AppSettings&)> onChanged) {
    m_settings = settings;
    m_onChanged = std::move(onChanged);
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"MusicBar_Settings_Window";
    RegisterClassExW(&wc);

    const UINT dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(940, dpi, 96), MulDiv(610, dpi, 96)};
    AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"设置 - MusicBar",
        WS_OVERLAPPEDWINDOW, x, y, width, height,
        nullptr, nullptr, instance, this);
    if (!hwnd) {
        MessageBoxW(nullptr, L"无法打开设置窗口。", L"MusicBar", MB_OK | MB_ICONWARNING);
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

void SettingsWindow::SetSettings(const AppSettings& settings) {
    m_settings = settings;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT message,
    WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    return self ? self->HandleMessage(hwnd, message, wParam, lParam)
        : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        UpdateTheme();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {MulDiv(720, GetDpiForWindow(hwnd), 96),
            MulDiv(480, GetDpiForWindow(hwnd), 96)};
        return 0;
    }
    case WM_SIZE:
        ResizeTarget();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeTarget();
        return 0;
    }
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        UpdateTheme();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_MOUSEMOVE: {
        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            m_trackingMouse = true;
        }
        const auto hit = HitTest(GET_X_LPARAM(lParam) / m_scale,
            GET_Y_LPARAM(lParam) / m_scale);
        if (hit.type != m_hovered.type || hit.index != m_hovered.index) {
            m_hovered = hit;
            SetCursor(LoadCursorW(nullptr,
                MAKEINTRESOURCEW(hit.type == Hit::None ? 32512 : 32649)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        m_hovered = {};
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        Activate(HitTest(GET_X_LPARAM(lParam) / m_scale,
            GET_Y_LPARAM(lParam) / m_scale));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && m_dropdown != Dropdown::None) {
            m_dropdown = Dropdown::None;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && m_dropdown != Dropdown::None) {
            m_dropdown = Dropdown::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        m_hwnd = nullptr;
        m_target.Reset();
        m_brush.Reset();
        m_dropdown = Dropdown::None;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void SettingsWindow::UpdateTheme() {
    DWORD light = 1;
    DWORD bytes = sizeof(light);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
            reinterpret_cast<BYTE*>(&light), &bytes);
        RegCloseKey(key);
    }
    m_light = light != 0;
    if (m_hwnd) {
        const BOOL dark = m_light ? FALSE : TRUE;
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        const DWORD backdrop = 2; // DWMSBT_MAINWINDOW (Mica), ignored by older Windows.
        DwmSetWindowAttribute(m_hwnd, static_cast<DWMWINDOWATTRIBUTE>(38),
            &backdrop, sizeof(backdrop));
        const DWORD corners = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(m_hwnd, static_cast<DWMWINDOWATTRIBUTE>(33),
            &corners, sizeof(corners));
    }
}

void SettingsWindow::ResizeTarget() {
    if (!m_hwnd) return;
    RECT rect{};
    GetClientRect(m_hwnd, &rect);
    const UINT width = std::max(1L, rect.right);
    const UINT height = std::max(1L, rect.bottom);
    m_scale = GetDpiForWindow(m_hwnd) / 96.0f;
    m_width = width / m_scale;
    m_height = height / m_scale;
    if (m_target) m_target->Resize(D2D1::SizeU(width, height));
}

bool SettingsWindow::EnsureResources() {
    if (!m_factory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_factory.ReleaseAndGetAddressOf()))) return false;
    if (!m_writeFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_writeFactory.ReleaseAndGetAddressOf()))))
        return false;
    if (!m_bodyFormat) {
        Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
        m_writeFactory->GetSystemFontCollection(&fonts);
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts) fonts->FindFamilyName(L"Segoe UI Variable", &index, &exists);
        const wchar_t* family = exists ? L"Segoe UI Variable" : L"Segoe UI";
        auto makeFormat = [&](float size, DWRITE_FONT_WEIGHT weight,
            Microsoft::WRL::ComPtr<IDWriteTextFormat>& output) {
            return m_writeFactory->CreateTextFormat(family, nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"zh-CN", output.ReleaseAndGetAddressOf());
        };
        if (FAILED(makeFormat(15, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_brandFormat)) ||
            FAILED(makeFormat(29, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_titleFormat)) ||
            FAILED(makeFormat(17, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_sectionFormat)) ||
            FAILED(makeFormat(14, DWRITE_FONT_WEIGHT_NORMAL, m_bodyFormat)) ||
            FAILED(makeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, m_smallFormat))) return false;

        // Segoe UI Variable does not supply Chinese glyphs. Map the Simplified
        // Chinese ranges explicitly so DirectWrite never picks a JP/TC fallback.
        Microsoft::WRL::ComPtr<IDWriteFactory2> factory2;
        if (SUCCEEDED(m_writeFactory.As(&factory2))) {
            Microsoft::WRL::ComPtr<IDWriteFontFallbackBuilder> builder;
            Microsoft::WRL::ComPtr<IDWriteFontFallback> fallback;
            if (SUCCEEDED(factory2->CreateFontFallbackBuilder(&builder))) {
                constexpr DWRITE_UNICODE_RANGE ranges[] = {
                    {0x2E80, 0x2FFF}, {0x3000, 0x303F},
                    {0x3400, 0x4DBF}, {0x4E00, 0x9FFF},
                    {0xF900, 0xFAFF}, {0xFF00, 0xFFEF}
                };
                const wchar_t* families[] = {L"DengXian", L"Microsoft YaHei UI"};
                if (SUCCEEDED(builder->AddMapping(ranges, _countof(ranges),
                    families, _countof(families))) &&
                    SUCCEEDED(builder->CreateFontFallback(&fallback))) {
                    for (auto* format : {m_brandFormat.Get(), m_titleFormat.Get(),
                        m_sectionFormat.Get(), m_bodyFormat.Get(), m_smallFormat.Get()}) {
                        Microsoft::WRL::ComPtr<IDWriteTextFormat1> format1;
                        if (SUCCEEDED(format->QueryInterface(IID_PPV_ARGS(&format1))))
                            format1->SetFontFallback(fallback.Get());
                    }
                }
            }
        }
    }
    if (!m_target) {
        RECT rect{};
        GetClientRect(m_hwnd, &rect);
        if (FAILED(m_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd,
                D2D1::SizeU(std::max(1L, rect.right), std::max(1L, rect.bottom))),
            m_target.ReleaseAndGetAddressOf()))) return false;
        // Keep the render target in physical pixels; the explicit transform
        // below handles DPI exactly once for both layout and text.
        m_target->SetDpi(96.0f, 96.0f);
        if (FAILED(m_target->CreateSolidColorBrush(Color(0xffffff),
            m_brush.ReleaseAndGetAddressOf()))) {
            m_target.Reset();
            return false;
        }
        ResizeTarget();
    }
    return true;
}

void SettingsWindow::DrawText(const wchar_t* content, const D2D1_RECT_F& rect,
    IDWriteTextFormat* format, D2D1_COLOR_F color) {
    m_brush->SetColor(color);
    m_target->DrawText(content, static_cast<UINT32>(wcslen(content)),
        format, rect, m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void SettingsWindow::DrawCard(float top, const wchar_t* title, bool toggle,
    bool checked, const wchar_t* value, bool hovered) {
    const float left = 298.0f;
    const float right = m_width - 42.0f;
    const auto rect = D2D1::RoundedRect(D2D1::RectF(left, top, right, top + 64), 8, 8);
    m_brush->SetColor(Color(m_light ? (hovered ? 0xffffff : 0xfafafa)
        : (hovered ? 0x393939 : 0x303030)));
    m_target->FillRoundedRectangle(rect, m_brush.Get());
    m_brush->SetColor(Color(m_light ? 0xe4e4e4 : 0x454545));
    m_target->DrawRoundedRectangle(rect, m_brush.Get(), 1);
    DrawText(title, D2D1::RectF(left + 20, top + 20, right - 145, top + 51),
        m_bodyFormat.Get(), Color(m_light ? 0x1b1b1b : 0xf3f3f3));
    if (toggle) {
        const float x = right - 70;
        const float y = top + 21;
        m_brush->SetColor(Color(checked ? 0x0067c0 : (m_light ? 0x888888 : 0x777777)));
        m_target->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(x, y, x + 42, y + 22), 11, 11), m_brush.Get());
        m_brush->SetColor(Color(0xffffff));
        m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(
            x + (checked ? 31.0f : 11.0f), y + 11), 8, 8), m_brush.Get());
    } else if (value) {
        DrawText(value, D2D1::RectF(right - 180, top + 20, right - 32, top + 50),
            m_bodyFormat.Get(), Color(m_light ? 0x444444 : 0xd6d6d6));
        m_brush->SetColor(Color(m_light ? 0x555555 : 0xdddddd));
        m_target->DrawLine(D2D1::Point2F(right - 30, top + 30),
            D2D1::Point2F(right - 25, top + 35), m_brush.Get(), 1.5f);
        m_target->DrawLine(D2D1::Point2F(right - 25, top + 35),
            D2D1::Point2F(right - 20, top + 30), m_brush.Get(), 1.5f);
    }
}

D2D1_RECT_F SettingsWindow::DropdownRect() const {
    const float top = (m_page == Page::Lyrics) ? 342.0f : 270.0f;
    const int count = m_page == Page::Lyrics ? 6 : 2;
    return D2D1::RectF(m_width - 254, top, m_width - 42,
        top + 8 + count * 38.0f);
}

void SettingsWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(m_hwnd, &paint);
    if (!EnsureResources()) {
        EndPaint(m_hwnd, &paint);
        return;
    }
    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Scale(m_scale, m_scale));
    m_target->Clear(Color(m_light ? 0xf3f3f3 : 0x202020));

    m_brush->SetColor(Color(m_light ? 0xededed : 0x272727));
    m_target->FillRectangle(D2D1::RectF(0, 0, 264, m_height), m_brush.Get());
    m_brush->SetColor(Color(m_light ? 0xdedede : 0x3a3a3a));
    m_target->DrawLine(D2D1::Point2F(264, 0),
        D2D1::Point2F(264, m_height), m_brush.Get());
    DrawText(L"MusicBar", D2D1::RectF(32, 30, 230, 62),
        m_brandFormat.Get(), Color(m_light ? 0x222222 : 0xf5f5f5));
    DrawText(L"设置", D2D1::RectF(32, 92, 230, 122),
        m_smallFormat.Get(), Color(m_light ? 0x6e6e6e : 0xb9b9b9));

    const wchar_t* nav[] = {L"任务栏", L"歌词", L"音频频谱"};
    for (int i = 0; i < 3; ++i) {
        const float top = 136.0f + i * 52.0f;
        const bool selected = static_cast<int>(m_page) == i;
        const Hit navHit = static_cast<Hit>(static_cast<int>(Hit::NavTaskbar) + i);
        const bool hovered = m_hovered.type == navHit;
        if (selected || hovered) {
            m_brush->SetColor(Color(m_light ? (selected ? 0xffffff : 0xf6f6f6)
                : (selected ? 0x3a3a3a : 0x323232)));
            m_target->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(16, top, 248, top + 42), 6, 6), m_brush.Get());
        }
        if (selected) {
            m_brush->SetColor(Color(0x0067c0));
            m_target->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(16, top + 10, 19, top + 32), 2, 2), m_brush.Get());
        }
        DrawText(nav[i], D2D1::RectF(48, top + 10, 238, top + 38),
            m_bodyFormat.Get(), Color(m_light ? 0x242424 : 0xf1f1f1));
    }
    const float footer = m_height - 104;
    m_brush->SetColor(Color(m_light ? 0xdedede : 0x444444));
    m_target->DrawLine(D2D1::Point2F(22, footer - 12),
        D2D1::Point2F(242, footer - 12), m_brush.Get());
    DrawText(L"重新读取", D2D1::RectF(48, footer, 238, footer + 30),
        m_bodyFormat.Get(), Color(m_light ? 0x3a3a3a : 0xdadada));
    DrawText(L"恢复默认", D2D1::RectF(48, footer + 44, 238, footer + 74),
        m_bodyFormat.Get(), Color(m_light ? 0x3a3a3a : 0xdadada));

    const wchar_t* titles[] = {L"任务栏", L"歌词", L"音频频谱"};
    const wchar_t* sections[] = {L"位置", L"显示与来源", L"采集"};
    DrawText(titles[static_cast<int>(m_page)],
        D2D1::RectF(298, 54, m_width - 42, 102), m_titleFormat.Get(),
        Color(m_light ? 0x1c1c1c : 0xf7f7f7));
    DrawText(sections[static_cast<int>(m_page)],
        D2D1::RectF(298, 148, m_width - 42, 178), m_sectionFormat.Get(),
        Color(m_light ? 0x282828 : 0xececec));
    if (m_page == Page::Taskbar) {
        DrawCard(192, L"锁定音乐栏位置", true, m_settings.positionLocked,
            nullptr, m_hovered.type == Hit::LockPosition);
    } else if (m_page == Page::Lyrics) {
        DrawCard(192, L"显示同步歌词", true, m_settings.showLyrics,
            nullptr, m_hovered.type == Hit::ShowLyrics);
        const int source = std::clamp(static_cast<int>(m_settings.lyricsSource), 0, 5);
        DrawCard(270, L"歌词来源", false, false, kSources[source],
            m_hovered.type == Hit::LyricsSource);
    } else {
        const int mode = m_settings.fftCaptureMode == SpectrumCaptureMode::Player ? 0 : 1;
        DrawCard(192, L"频谱音频来源", false, false, kCaptureModes[mode],
            m_hovered.type == Hit::CaptureMode);
    }

    if (m_dropdown != Dropdown::None) {
        const auto rect = DropdownRect();
        m_brush->SetColor(Color(m_light ? 0xffffff : 0x333333));
        m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, 8, 8), m_brush.Get());
        m_brush->SetColor(Color(m_light ? 0xd5d5d5 : 0x555555));
        m_target->DrawRoundedRectangle(D2D1::RoundedRect(rect, 8, 8), m_brush.Get());
        const bool sources = m_dropdown == Dropdown::LyricsSource;
        const int count = sources ? 6 : 2;
        for (int i = 0; i < count; ++i) {
            const float top = rect.top + 4 + i * 38.0f;
            if ((m_hovered.type == Hit::DropdownItem && m_hovered.index == i) ||
                m_dropdownSelection == i) {
                m_brush->SetColor(Color(m_light ? 0xeeeeee : 0x484848));
                m_target->FillRoundedRectangle(D2D1::RoundedRect(
                    D2D1::RectF(rect.left + 5, top, rect.right - 5, top + 36), 4, 4),
                    m_brush.Get());
            }
            DrawText(sources ? kSources[i] : kCaptureModes[i],
                D2D1::RectF(rect.left + 16, top + 7, rect.right - 12, top + 35),
                m_bodyFormat.Get(), Color(m_light ? 0x222222 : 0xf1f1f1));
        }
    }
    if (!m_error.empty()) {
        DrawText(m_error.c_str(), D2D1::RectF(298, m_height - 64,
            m_width - 42, m_height - 20), m_smallFormat.Get(), Color(0xc42b1c));
    }

    const HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        m_brush.Reset();
        m_target.Reset();
    }
    EndPaint(m_hwnd, &paint);
}

SettingsWindow::HitResult SettingsWindow::HitTest(float x, float y) const {
    if (m_dropdown != Dropdown::None) {
        const auto rect = DropdownRect();
        if (InRect(x, y, rect)) {
            return {Hit::DropdownItem, std::clamp(
                static_cast<int>((y - rect.top - 4) / 38), 0,
                m_dropdown == Dropdown::LyricsSource ? 5 : 1)};
        }
        return {};
    }
    if (x >= 16 && x < 248) {
        for (int i = 0; i < 3; ++i) {
            const float top = 136.0f + i * 52;
            if (y >= top && y < top + 42)
                return {static_cast<Hit>(static_cast<int>(Hit::NavTaskbar) + i), -1};
        }
        const float footer = m_height - 104;
        if (y >= footer && y < footer + 32) return {Hit::Reload, -1};
        if (y >= footer + 44 && y < footer + 76) return {Hit::Reset, -1};
    }
    if (x >= 298 && x < m_width - 42) {
        if (m_page == Page::Taskbar && y >= 192 && y < 256)
            return {Hit::LockPosition, -1};
        if (m_page == Page::Lyrics) {
            if (y >= 192 && y < 256) return {Hit::ShowLyrics, -1};
            if (y >= 270 && y < 334) return {Hit::LyricsSource, -1};
        }
        if (m_page == Page::Audio && y >= 192 && y < 256)
            return {Hit::CaptureMode, -1};
    }
    return {};
}

void SettingsWindow::Activate(HitResult hit) {
    if (m_dropdown != Dropdown::None) {
        const Dropdown open = m_dropdown;
        m_dropdown = Dropdown::None;
        if (hit.type == Hit::DropdownItem) {
            if (open == Dropdown::LyricsSource)
                m_settings.lyricsSource = static_cast<LyricsSource>(hit.index);
            else m_settings.fftCaptureMode = hit.index == 0
                ? SpectrumCaptureMode::Player : SpectrumCaptureMode::System;
            Commit();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    switch (hit.type) {
    case Hit::NavTaskbar: m_page = Page::Taskbar; break;
    case Hit::NavLyrics: m_page = Page::Lyrics; break;
    case Hit::NavAudio: m_page = Page::Audio; break;
    case Hit::LockPosition:
        m_settings.positionLocked = !m_settings.positionLocked;
        Commit();
        break;
    case Hit::ShowLyrics:
        m_settings.showLyrics = !m_settings.showLyrics;
        Commit();
        break;
    case Hit::LyricsSource:
        m_dropdown = Dropdown::LyricsSource;
        m_dropdownSelection = static_cast<int>(m_settings.lyricsSource);
        break;
    case Hit::CaptureMode:
        m_dropdown = Dropdown::CaptureMode;
        m_dropdownSelection = m_settings.fftCaptureMode == SpectrumCaptureMode::Player ? 0 : 1;
        break;
    case Hit::Reload: LoadFromFile(); break;
    case Hit::Reset:
        m_settings = {};
        Commit();
        break;
    default: break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::Commit() {
    m_error.clear();
    if (m_onChanged) m_onChanged(m_settings);
}

void SettingsWindow::LoadFromFile() {
    AppSettings loaded{};
    if (SettingsStore::GetFileStamp() != 0 && !SettingsStore::Load(loaded)) {
        m_error = L"设置文件无法读取，请检查内容。";
        return;
    }
    m_settings = loaded;
    m_error.clear();
    if (m_onChanged) m_onChanged(m_settings);
}

} // namespace MusicBar
