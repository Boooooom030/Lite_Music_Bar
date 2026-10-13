#include "ui/Direct2DRenderer.hpp"
#include "lyrics/LyricsTimeline.hpp"
#include <d2d1_1helper.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace MusicBar {
struct RendererTestAccess {
    static void UseReadableLayer(Direct2DRenderer& renderer) {
        // Cover-only hover exercises the normal readable content layer without
        // fading or blurring the lyric pixels that the test inspects.
        renderer.m_coverHoverProgress = 0.5f;
    }
    static std::vector<uint32_t> LyricPixels(Direct2DRenderer& renderer) {
        const auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            renderer.m_pContentBitmap->GetPixelFormat());
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> readable;
        HRESULT hr = renderer.m_pRenderTarget->CreateBitmap(
            renderer.m_pContentBitmap->GetPixelSize(), nullptr, 0, properties, &readable);
        if (SUCCEEDED(hr)) hr = readable->CopyFromBitmap(nullptr, renderer.m_pContentBitmap.Get(), nullptr);
        D2D1_MAPPED_RECT mapped{};
        if (SUCCEEDED(hr)) hr = readable->Map(D2D1_MAP_OPTIONS_READ, &mapped);
        if (FAILED(hr)) throw std::runtime_error("Cannot read rendered lyric pixels");
        std::vector<uint32_t> pixels;
        for (int y = 24; y < 43; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(mapped.bits + y * mapped.pitch);
            for (int x = static_cast<int>(renderer.m_rcMiddle.left) + 1;
                    x < static_cast<int>(renderer.m_rcMiddle.right) - 1; ++x) {
                pixels.push_back(row[x]);
            }
        }
        if (FAILED(readable->Unmap())) throw std::runtime_error("Cannot release lyric readback");
        return pixels;
    }
};
} // namespace MusicBar

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int ColoredInk(const std::vector<uint32_t>& pixels) {
    return static_cast<int>(std::count_if(pixels.begin(), pixels.end(), [](uint32_t pixel) {
        const int r = (pixel >> 16) & 255;
        const int g = (pixel >> 8) & 255;
        const int b = pixel & 255;
        return (pixel >> 24) > 30 && std::max({r, g, b}) - std::min({r, g, b}) > 20;
    }));
}
int VisibleInk(const std::vector<uint32_t>& pixels) {
    return static_cast<int>(std::count_if(pixels.begin(), pixels.end(), [](uint32_t pixel) {
        return (pixel >> 24) > 30;
    }));
}
float ColoredCenterY(const std::vector<uint32_t>& pixels) {
    const size_t width = pixels.size() / 19; // readback covers rows 24 through 42
    float weightedY = 0.0f;
    int count = 0;
    for (size_t index = 0; index < pixels.size(); ++index) {
        const uint32_t pixel = pixels[index];
        const int r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
        if ((pixel >> 24) > 30 && std::max({r, g, b}) - std::min({r, g, b}) > 20) {
            weightedY += static_cast<float>(index / width);
            ++count;
        }
    }
    return count > 0 ? weightedY / static_cast<float>(count) : -1.0f;
}
void CheckLyricRendering(MusicBar::Direct2DRenderer& renderer) {
    MusicBar::MediaTrackInfo track{};
    track.title = L"Lyric pixel regression";
    track.durationMs = 60000;
    // Paused playback freezes each synthetic position while testing the frame.
    MusicBar::LyricDocument lyrics{};
    lyrics.lines = {{0, 5000, L" \t"}, {5000, 4000, L"MMMM MMMM"}, {9000, 21000, L""},
        {30000, 4000, L"WWWW WWWW"}};
    auto draw = [&](int64_t position) {
        track.positionMs = position;
        MusicBar::UpdateLyricPosition(lyrics, position);
        MusicBar::RendererTestAccess::UseReadableLayer(renderer);
        Require(SUCCEEDED(renderer.Render({}, RECT{0, 0, 600, 48}, 1.0f,
            track, lyrics, {}, MusicBar::ButtonType::None, true, false, true)),
            "Lyric frame failed");
        return MusicBar::RendererTestAccess::LyricPixels(renderer);
    };
    const auto intro = draw(0);
    Require(VisibleInk(intro) > 40 && ColoredInk(intro) == 0 && draw(4999) == intro,
        "First lyric was hidden or highlighted during the intro");
    const auto start = draw(5000);
    Require(start == intro, "First lyric jumped or restarted at its singing timestamp");
    const auto quarter = draw(6000);
    const auto halfway = draw(7000);
    const auto complete = draw(9000);
    Require(VisibleInk(start) > 40 && ColoredInk(start) == 0,
        "LRC was fully highlighted or invisible at its start");
    Require(ColoredInk(quarter) > 0 && ColoredInk(halfway) > ColoredInk(quarter) &&
        ColoredInk(complete) > ColoredInk(halfway), "LRC highlight did not advance with playback");
    Require(draw(29000) == complete && draw(29820) == complete,
        "Interlude lost the completed lyric before the transition window");
    const auto transitionHalf = draw(29910);
    Require(transitionHalf != complete && VisibleInk(transitionHalf) > 10,
        "Outgoing lyric did not slide/fade during the transition");
    Require(ColoredInk(transitionHalf) > 0 && ColoredInk(transitionHalf) < ColoredInk(complete) &&
        ColoredCenterY(transitionHalf) + 2.0f < ColoredCenterY(complete),
        "Outgoing lyric did not visibly move upwards while fading");
    Require(draw(29910) == transitionHalf, "Paused playback did not freeze the transition");
    const auto nextFirstFrame = draw(30000);
    Require(VisibleInk(nextFirstFrame) > 40 && ColoredInk(nextFirstFrame) == 0,
        "Next line was invisible or inherited the completed highlight");
    Sleep(450);
    Require(draw(30000) == nextFirstFrame, "Next line still animated after its timestamp");
    Require(draw(5000) == start, "Seeking backwards did not reset the highlight");
    Require(draw(1000) == intro, "Seeking into the intro retained the old completed lyric");

    lyrics.lines = {{10000, 2500, L"MMMM MMMM", L"",
        {{10500, 1000, L"MMMM "}, {11500, 1000, L"MMMM"}}}};
    const auto karaokeIntro = draw(0);
    Require(VisibleInk(karaokeIntro) > 40 && ColoredInk(karaokeIntro) == 0 &&
        draw(10000) == karaokeIntro, "Karaoke intro preview was hidden or advanced early");
    Require(ColoredInk(draw(10000)) == 0, "Karaoke highlighted before the first syllable");
    const int partialWord = ColoredInk(draw(11000));
    Require(partialWord > 0 && ColoredInk(draw(12500)) > partialWord,
        "Karaoke word timing regressed");

    lyrics = {};
    lyrics.isInstrumental = true;
    const auto instrumental = draw(0);
    Require(VisibleInk(instrumental) > 40 && ColoredInk(instrumental) == 0,
        "Confirmed instrumental did not show a neutral notice");
    Require(draw(50000) == instrumental, "Instrumental notice scrolled or became karaoke");
    MusicBar::RendererTestAccess::UseReadableLayer(renderer);
    Require(SUCCEEDED(renderer.Render({}, RECT{0, 0, 600, 48}, 1.0f, track, lyrics, {},
        MusicBar::ButtonType::None, false, false, true)), "Hidden notice render failed");
    Require(VisibleInk(MusicBar::RendererTestAccess::LyricPixels(renderer)) == 0,
        "Show lyrics setting did not hide the instrumental notice");
    lyrics = {};
    Require(VisibleInk(draw(5000)) == 0, "Missing lyrics were mislabeled as instrumental");
    std::cout << "Lyric pixels: progressive LRC, interlude hold, timed slide/fade, paused transition, "
        "on-time next line, seek, karaoke and instrumental notice passed.\n";
}
} // namespace

// Exercise the actual D2D/DXGI draw state. In particular, a frame without a
// hover must not leave BeginDraw open and make the next frame fail.
int main() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return 1;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"MusicBar_RendererTests";
    if (!RegisterClassW(&windowClass)) {
        CoUninitialize();
        return 2;
    }
    // Hidden windows keep this test from taking over the desktop or taskbar.
    HWND parent = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,
        windowClass.lpszClassName, L"", WS_POPUP,
        0, 0, 1920, 96, nullptr, nullptr, windowClass.hInstance, nullptr);
    HWND host = parent ? CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,
        windowClass.lpszClassName, L"", WS_CHILD,
        0, 0, 1920, 96, parent, nullptr, windowClass.hInstance, nullptr) : nullptr;

    int result = 0;
    if (!host) {
        result = 3;
    } else {
        MusicBar::Direct2DRenderer renderer;
        if (!renderer.Initialize(host)) {
            result = 4;
        } else {
            try {
                CheckLyricRendering(renderer);
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                result = 9;
            }
            MusicBar::MediaTrackInfo track{};
            track.title = L"Renderer regression";
            track.artist = L"MusicBar";
            track.isPlaying = true;
            track.durationMs = 120000;
            MusicBar::LyricDocument lyrics{};
            lyrics.lines.push_back({0, 120000, L"A long lyric line for rendering and scrolling"});
            lyrics.activeIndex = 0;
            MusicBar::SpectrumData spectrum{};
            spectrum.hasAudio = true;
            spectrum.bands.fill(0.5f);

            // Nonzero offsets, DPI changes and resizing after blur all need to
            // succeed, while hit-test coordinates remain relative to the bar.
            for (const RECT bar : {RECT{1300, 0, 1600, 48},
                RECT{900, 8, 1350, 80}, RECT{1100, 0, 1420, 48}}) {
                if (result != 0) break;
                const float scale = (bar.bottom - bar.top) / 48.0f;
                for (int frame = 0; frame < 24; ++frame) {
                    const bool middle = frame >= 4 && frame < 12;
                    const bool cover = frame >= 8 && frame < 16;
                    lyrics.syllableProgress = frame / 24.0f;
                    const HRESULT hr = renderer.Render({}, bar, scale, track,
                        lyrics, spectrum, MusicBar::ButtonType::None,
                        true, middle, cover);
                    if (FAILED(hr)) {
                        std::cerr << "Render failed: 0x" << std::hex
                            << static_cast<unsigned long>(hr) << '\n';
                        result = 5;
                        break;
                    }
                    if (!renderer.HitTestCover(static_cast<int>(20 * scale),
                            static_cast<int>(24 * scale)) ||
                        !renderer.HitTestMiddle(static_cast<int>(100 * scale),
                            static_cast<int>(24 * scale)) ||
                        renderer.HitTestCover(bar.left + 20, bar.top + 24)) {
                        result = 6;
                        break;
                    }
                    // Advance the transitions enough to exercise both blur paths.
                    Sleep(20);
                }
                if (result != 0) break;
            }
            if (result == 0 && renderer.Render({}, RECT{}, 1.0f, track,
                lyrics, spectrum, MusicBar::ButtonType::None,
                true, false, false) != E_INVALIDARG) result = 7;
            if (result == 0 && FAILED(renderer.Render({}, RECT{900, 0, 1200, 48},
                1.0f, {}, {}, {}, MusicBar::ButtonType::None,
                false, false, false))) result = 8;
        }
    }
    if (host) DestroyWindow(host);
    if (parent) DestroyWindow(parent);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    CoUninitialize();
    return result;
}
