# AGENTS.md — Contributor & AI Agent Operational Guide

> Project: **MusicBar (C++ Edition)**  
> Architecture: **Modern C++20 / Win32 / Direct2D / C++/WinRT / WASAPI**  
> Target OS: **Windows 10 1809+ / Windows 11 (21H2 - 24H2)**

---

## 1. Project Philosophy & Core Invariants

1. **Zero External Runtime Dependency**:
   - The binary must be 100% self-contained native code (`.exe`).
   - Do NOT introduce bulky runtime frameworks (.NET, Electron, Qt, or heavy C++ GUI kits).
   - Use Windows built-in system libraries: Direct2D (`d2d1`), DirectWrite (`dwrite`), WIC (`windowscodecs`), WinHTTP (`winhttp`), BCrypt (`bcrypt`), WASAPI (`mmdeviceapi`), and C++/WinRT (`windowsapp`).
2. **Lightweight & High Performance**:
   - Memory footprint target: **< 15 MB RAM** in steady-state operation.
   - Binary size target: **< 2.5 MB** release executable.
   - CPU usage: **< 0.1%** during 60 FPS audio spectrum playback; **0.0%** when paused/idle.
3. **Rock-Solid Taskbar Integration**:
   - Do NOT attach small floating windows or raw popup child windows directly without clipping.
   - Invariant: The window covers the entire taskbar canvas (`(0, 0)` to `(taskbarWidth, taskbarHeight)`), and GDI `SetWindowRgn` is applied to restrict interactivity and visual drawing strictly to the music bar bounding box (e.g. 280×36 or 320×36).
   - All regions outside the bounding box must allow click-through directly to Explorer (Start menu, taskbar icons, system tray).

---

## 2. Directory Layout & Module Responsibilities

```
MusicBar/
├── CMakeLists.txt                # Root CMake project configuration
├── AGENTS.md                     # This agent guide
├── PLAN.md                       # Comprehensive architectural design plan
├── TODO.md                       # Phase-by-phase task tracking checklist
├── assets/
│   ├── app.manifest              # Per-Monitor V2 DPI and Win10/11 compatibility
│   └── icon.ico                  # Application & system tray icon
├── third_party/
│   ├── nlohmann/json.hpp         # Fast, modern C++ JSON library (single header)
│   └── miniz/miniz.h / .c        # Single-file zlib/deflate decompressor
└── src/
    ├── main.cpp                  # WinMain entry point, single-instance mutex
    ├── core/
    │   ├── App.hpp / .cpp        # Application coordinator, message pump
    │   └── Types.hpp             # Shared structs (MediaInfo, LyricsData, SpectrumData)
    ├── shell/
    │   ├── TaskbarDock.hpp / .cpp# Win32 taskbar discovery, SetParent, SetWindowRgn
    │   └── TrayIcon.hpp / .cpp   # Shell_NotifyIconW, context menu
    ├── media/
    │   └── MediaSession.hpp / .cpp # C++/WinRT GSMTC transport listener & controls
    ├── audio/
    │   ├── WasapiLoopback.hpp / .cpp # WASAPI loopback audio stream capture
    │   └── FftAnalyzer.hpp / .cpp    # 1024-point FFT, Hann window, 16/24 band physics
    ├── lyrics/
    │   ├── LyricsService.hpp / .cpp  # Query orchestrator, LRU cache, timeline sync
    │   ├── LyricsParser.hpp / .cpp   # LRC line & KRC/QRC/YRC syllable parser
    │   ├── Decrypter.hpp / .cpp      # Kugou KRC (XOR+inflate), QQ QRC (3DES+inflate)
    │   └── providers/                # NetEase, QQ, Kugou, Soda, LRCLIB providers
    ├── ui/
    │   ├── Direct2DRenderer.hpp / .cpp # Direct2D 1.1, DirectWrite, WIC rendering
    │   ├── Layout.hpp                # Coordinate geometry and DPI calculations
    │   └── ImageLoader.hpp / .cpp    # WIC stream to ID2D1Bitmap decoder
    └── net/
        └── HttpClient.hpp / .cpp     # WinHTTP asynchronous client
```

---

## 3. Toolchain & Build Instructions

### Prerequisites (already installed on this machine)
- **Visual Studio 2022 / 2026** (MSVC toolset `14.4x` / `14.5x`)
- **Windows 10/11 SDK** (`10.0.26100.0` or higher) with C++/WinRT headers
- **CMake** (version 3.28+)

### Command-Line Build Commands

```powershell
# 1. Generate CMake build tree with MSVC (from repository root)
cmake -B build -G "Visual Studio 18 2026" -A x64

# Alternatively, using Ninja if available:
# cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# 2. Compile Release executable
cmake --build build --config Release --parallel

# 3. Output executable location
# .\build\Release\MusicBar.exe
```

---

## 4. Coding Standards & Architectural Patterns

1. **C++20 & Modern Idioms**:
   - Use `std::unique_ptr` and `std::shared_ptr` for resource management.
   - Use `Microsoft::WRL::ComPtr<T>` for all Direct2D, DirectWrite, WIC, and WASAPI COM interfaces.
   - Use `winrt::com_ptr<T>` and C++/WinRT value types (`winrt::hstring`, `winrt::Windows::...`) for WinRT APIs.
   - Never use manual `delete` on raw pointers.
2. **Threading & Concurrency Model**:
   - **Main UI Thread**: Runs the Win32 message loop (`GetMessage` / `DispatchMessage`), owns the window HWND, handles `WM_PAINT`, and drives Direct2D rendering.
   - **Audio Thread**: High-priority background thread pumping WASAPI loopback audio packets into a circular buffer and running FFT calculations.
   - **Network Threads**: WinHTTP asynchronous requests for lyrics run via thread pool (`std::async` or WinHTTP asynchronous callbacks).
   - **Thread Synchronization**: Background threads post notifications to the UI thread via `PostMessageW(hWnd, WM_APP + X, wParam, lParam)`. Never call Direct2D methods directly from background threads.
3. **Error Handling & Resilience**:
   - COM calls must be guarded with `SUCCEEDED(hr)` or `HR_CHECK` macros.
   - Network failures or decryption errors in lyric providers must fail gracefully without crashing or blocking the UI.
   - If `Shell_TrayWnd` restarts (Explorer crash/restart), a periodic 2-second retry timer must re-dock without crashing.

---

## 5. Agent Workflow for Subsequent Tasks

When making code edits or expanding features:
1. Always consult [PLAN.md](file:///C:/Users/huban/Storage/tool/Musci_Bar/PLAN.md) for architectural alignment and [TODO.md](file:///C:/Users/huban/Storage/tool/Musci_Bar/TODO.md) for progress tracking.
2. Maintain clean separation of concerns: UI rendering stays in `ui/`, audio in `audio/`, networking in `net/`, and media in `media/`.
3. Test builds with `cmake --build build --config Release` to ensure zero compilation errors and warnings.
4. After verifying new components, check off the corresponding items in [TODO.md](file:///C:/Users/huban/Storage/tool/Musci_Bar/TODO.md).
