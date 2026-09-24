# MusicBar (C++ Edition) — Technical Architecture & Design Plan

> A high-performance, ultra-lightweight Windows Taskbar Music & Lyrics Bar
> Built with **Modern C++20**, **Win32**, **Direct2D / DirectWrite**, **C++/WinRT**, and **WASAPI**.
> Zero runtime dependencies. ~2 MB binary. ~10 MB RAM footprint. Instant startup.

---

## 1. Vision & Core Objectives

Create a native Windows 10/11 taskbar-embedded media control bar inspired by [AF-Media-Bar](https://github.com/Fervent-Tempo/AF-Media-Bar), but rewritten in pure, modern C++ to eliminate all CLR/.NET runtime overhead, WPF windowing quirks on Windows 11, and heavy memory usage:

1. **Native Taskbar Integration**: Anchors seamlessly inside the Windows Taskbar (`Shell_TrayWnd`) using Win32 child window attachment and GDI region clipping (`SetWindowRgn`).
2. **GSMTC System Media Transport Controls**: Uses `C++/WinRT` (`Windows.Media.Control`) to support Spotify, NetEase, QQ Music, Apple Music, Chrome, Edge, and all standard Windows media players.
3. **Hardware-Accelerated UI**: Built with **Direct2D** and **DirectWrite** for sub-pixel text rendering, smooth anti-aliased geometry, album art decoding via WIC, and interactive mini-buttons.
4. **Real-time FFT Audio Spectrum**: Uses Windows **WASAPI Loopback Capture** to sample system audio and a lightweight FFT algorithm to visualize 16/32-band spectrum bars at a smooth **60 FPS** (<0.1% CPU).
5. **Multi-Source Online Lyrics Engine**:
   - Supports **NetEase Music**, **QQ Music**, **Kugou Music**, **Soda Music**, and **LRCLIB**.
   - Word-by-word syllable karaoke timestamps (KRC, QRC, YRC, TTML) and line-level LRC synchronization.
   - Dual-line display option (original lyrics + Chinese translation / romanization).
6. **System Tray & In-Taskbar Dragging**: Quick repositioning via left-click drag within the taskbar, auto-hide when idle, and system tray menu.

---

## 2. Technology Stack & Dependencies

| Component | Choice | Reason & Details |
|---|---|---|
| **Language Standard** | C++20 | Modern features: `std::span`, coroutines, concepts, `<format>`, `<chrono>` |
| **Compiler & Build** | MSVC 19.4x (VS 2022/2026) + CMake 3.28+ | Native Windows toolchain, full Windows SDK integration |
| **Windowing & Shell** | Pure Win32 API (`user32.lib`) | Direct control of `WndProc`, message loop, no framework abstractions |
| **Graphics & Text** | Direct2D 1.1 + DirectWrite (`d2d1.lib`, `dwrite.lib`) | Hardware-accelerated GPU rendering, sub-pixel text, smooth 60 FPS |
| **Image Decoding** | Windows Imaging Component (`windowscodecs.lib`) | System-native decoder for PNG, JPEG, ICO album thumbnails |
| **Media Transport** | C++/WinRT (`winrt/Windows.Media.Control.h`) | Native modern projection for GSMTC with zero COM marshaling overhead |
| **Audio Capture** | WASAPI Loopback (`audioclient.h`, `mmdeviceapi.h`) | System-native COM audio loopback stream with lowest possible latency |
| **Spectrum Analysis** | Radix-2 / KissFFT with Hann Window | In-memory 1024-point FFT with logarithmic band aggregation and peak decay |
| **Network Client** | Windows HTTP Services (`winhttp.lib`) | System-native asynchronous HTTP client; no bulky OpenSSL or libcurl |
| **JSON Parser** | `nlohmann/json` (single-header) | Industry-standard modern C++ JSON parsing |
| **KRC / QRC Decryption** | `bcrypt.lib` (TripleDES) + `miniz` (zlib inflate) | 16-byte XOR for KRC, TripleDES for QRC, and fast in-memory decompression |

**Runtime Requirements**: Windows 10 (1809+, Build 17763) or Windows 11. **Zero extra runtime installs required.**

---

## 3. High-Level Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Windows Taskbar (Shell_TrayWnd)                 │
│                                                                        │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ ┌──────┐ Title - Artist                 [ 16-Band Spectrum ]⏮⏯⏭ │  │
│  │ │Cover │ Dynamic Synchronized Lyrics    ━━━━━━━━━━━━━━━━━━━━     │  │
│  │ └──────┘ (Hover / Drag / Volume)        (2px Accent Progress)    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
          ▲                                    ▲                ▲
          │ Render                             │ Audio          │ Metadata
┌─────────────────────┐              ┌──────────────────┐  ┌─────────────┐
│   Direct2D Engine   │              │ WASAPI Audio     │  │ C++/WinRT   │
│ - D2D RenderTarget  │              │ Loopback Capture │  │ GSMTC Media │
│ - DirectWrite Text  │              │ + 1024-point FFT │  │ Controller  │
│ - WIC Album Art     │              └──────────────────┘  └─────────────┘
└─────────────────────┘                        ▲                ▲
          ▲                                    │                │
┌─────────────────────┐              ┌──────────────────┐       │
│  TaskbarDock Host   │              │   Lyrics Engine  │       │
│ - Win32 Child HWND  │◀─────────────│ - NetEase/QQ/KG  │───────┘
│ - SetWindowRgn Clip │              │ - KRC/QRC/YRC    │ (Matches song/artist)
│ - Canvas Drag / Hook│              │ - Millisecond    │
└─────────────────────┘              │   Time Sync      │
                                     └──────────────────┘
```

---

## 4. Subsystems Detail

### 4.1 Taskbar Docking & Native Window Host (`TaskbarDock`)
- **Problem solved**: In Windows 11, the taskbar has an internal DirectComposition surface (`DesktopWindowContentBridge`). Small child windows are often occluded.
- **Solution**:
  1. The Win32 child window is sized to match the **entire taskbar canvas** (`taskbarWidth` × `taskbarHeight`), rooted at `(0, 0)`.
  2. GDI `SetWindowRgn` clips the window to **only** the active bar area (e.g. 320×36 at coordinates `(X, Y)`).
  3. All taskbar pixels outside the region remain 100% transparent and pass mouse clicks straight through to Explorer, the Start button, and system tray icons.
  4. Left-mouse drag simply updates the `(X, Y)` offset and re-applies `SetWindowRgn` — zero OS-level window destruction or re-parenting flickers.
  5. Monitors taskbar size/position changes (`EVENT_OBJECT_LOCATIONCHANGE` hook / `WM_DISPLAYCHANGE`).

### 4.2 Graphics & UI Renderer (`Renderer`)
- Uses `ID2D1HwndRenderTarget` bound to the window HWND.
- Frame elements:
  - **Background**: Rounded rectangle with subtle border and translucent dark acrylic-like tint (`#EE1E1E1E` + `#44FFFFFF` border).
  - **Album Cover**: 24×24 rounded bitmap decoded through WIC, falling back to a glyph note placeholder.
  - **Metadata & Lyrics**:
    - Mode A: Song Title (11pt SemiBold) + Artist (9.5pt Muted).
    - Mode B: Song Title + Real-time Synchronized Lyric line (with syllable highlight when available).
  - **Spectrum Visualizer**: 16 or 24 vertical bars with variable heights, rendered with `#1DB954` or theme accent color.
  - **Mini Controls**: SVG/Font glyphs for Previous (`\uE892`), Play/Pause (`\uE768`/`\uE769`), Next (`\uE893`).
  - **Progress Bar**: 2px bottom accent bar showing playback progress with millisecond interpolation.

### 4.3 GSMTC Media Session Manager (`MediaSession`)
- Built using modern `C++/WinRT` with coroutines (`co_await`).
- Subscribes to `CurrentSessionChanged`, `MediaPropertiesChanged`, `PlaybackInfoChanged`, `TimelinePropertiesChanged`.
- Asynchronously reads:
  - Title, Artist, Album Title.
  - Thumbnail stream (`IRandomAccessStreamReference` -> memory buffer -> WIC Bitmap).
  - Playback status (`Playing`, `Paused`, `Stopped`).
  - Timeline (Position, Duration, LastUpdatedTime for smooth interpolation).
- Playback commands: `TryTogglePlayPauseAsync()`, `TrySkipNextAsync()`, `TrySkipPreviousAsync()`.

### 4.4 WASAPI Audio Capture & FFT Spectrum (`AudioSpectrum`)
- Initializes `IMMDeviceEnumerator` to acquire default audio render endpoint (`eRender`, `eMultimedia`).
- Activates `IAudioClient` in `AUDCLNT_STREAMFLAGS_LOOPBACK` mode.
- Reads raw float/PCM audio buffer into a circular ring buffer.
- Performs 1024-point FFT:
  - Applies **Hann Window** function to prevent spectral leakage.
  - Computes magnitude spectrum.
  - Aggregates frequency bins into 16/24 logarithmic bands (20 Hz - 16 kHz).
  - Applies smooth gravity physics (rapid attack, smooth decay) for bouncy, responsive animation.

### 4.5 Online Lyrics Engine (`LyricsEngine`)
- **Query Flow**:
  1. Triggered whenever GSMTC detects a track change.
  2. Queries providers in prioritized order (or concurrently):
     - **NetEase**: Web endpoint (JSON, supporting original lyrics, translated lyrics, and YRC syllable format).
     - **QQ Music**: Web search + `fcg_query_lyric_new.fcg` (Base64/QRC TripleDES decrypt).
     - **Kugou**: Search hash + download endpoint (`fmt=lrc` or KRC XOR 16-byte key + zlib inflate).
     - **Soda Music**: REST search + lyric endpoint.
     - **LRCLIB**: Open REST API fallback.
  3. Caches result in-memory (LRU cache of 20 tracks).
- **Lyric Parser & Sync Engine**:
  - Parses timestamps into a sorted vector of lines and syllables:
    ```cpp
    struct Syllable { std::wstring text; int64_t startMs; int64_t durationMs; };
    struct LyricLine { int64_t startMs; int64_t durationMs; std::wstring text; std::wstring translation; std::vector<Syllable> syllables; };
    ```
  - Direct2D renders current active line with smooth text transition.

---

## 5. File & Directory Layout

```
MusicBar/
├── CMakeLists.txt                # Root CMake project configuration
├── src/
│   ├── main.cpp                  # Entry point, single-instance mutex, WinMain
│   ├── core/
│   │   ├── App.hpp / .cpp        # Application state, event coordination
│   │   └── Types.hpp             # Shared data models (MediaSnapshot, LyricData)
│   ├── shell/
│   │   ├── TaskbarDock.hpp / .cpp# Win32 taskbar embedding, SetWindowRgn, DPI
│   │   └── TrayIcon.hpp / .cpp   # Shell_NotifyIconW, tray context menu
│   ├── media/
│   │   └── MediaSession.hpp / .cpp # C++/WinRT GSMTC listener & controller
│   ├── audio/
│   │   ├── WasapiLoopback.hpp / .cpp # WASAPI loopback audio stream capture
│   │   └── FftAnalyzer.hpp / .cpp    # 1024-point FFT, Hann window, band physics
│   ├── lyrics/
│   │   ├── LyricsService.hpp / .cpp  # Lyrics search manager & caching
│   │   ├── LyricsParser.hpp / .cpp   # LRC, KRC, QRC, YRC time parser
│   │   ├── Decrypter.hpp / .cpp      # KRC (XOR+inflate), QRC (3DES+inflate)
│   │   └── providers/
│   │       ├── NetEaseProvider.cpp
│   │       ├── QQMusicProvider.cpp
│   │       ├── KugouProvider.cpp
│   │       ├── SodaProvider.cpp
│   │       └── LrclibProvider.cpp
│   ├── ui/
│   │   ├── Direct2DRenderer.hpp / .cpp # D2D1, DirectWrite, WIC rendering
│   │   ├── Layout.hpp                # Coordinate calculations inside taskbar
│   │   └── ImageLoader.hpp / .cpp    # WIC stream decoder
│   └── net/
│       └── HttpClient.hpp / .cpp     # WinHTTP asynchronous wrapper
├── third_party/
│   ├── nlohmann/json.hpp             # JSON library (header-only)
│   └── miniz/miniz.h / .c            # Ultra-lightweight zlib decompressor
└── assets/
    ├── app.manifest                  # DPI awareness, Windows 10/11 compatibility
    └── icon.ico                      # Application & tray icon
```

---

## 6. Performance & Quality Targets

- **Binary Size**: < 2.5 MB (Release build with MSVC `/O2 /GL /MD` or `/MT`).
- **RAM Footprint**: 8 MB – 15 MB in steady state.
- **CPU Utilization**:
  - Idle (Paused / No media): ~0.0% CPU.
  - Active Playback + 60 FPS Spectrum: < 0.2% CPU on typical modern multi-core x64.
- **Startup Latency**: < 50 milliseconds.
- **Taskbar Compatibility**: Tested on Windows 10 (1809–22H2) and Windows 11 (21H2–24H2).
