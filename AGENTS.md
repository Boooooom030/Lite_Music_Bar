# AGENTS.md — Contributor & AI Agent Operational Guide

> Project: **MusicBar (C++ Edition)**  
> Architecture: **Modern C++20 / Win32 / Direct2D / C++/WinRT / WASAPI**  
> Target OS: **Windows 10 1809+ / Windows 11 (21H2 - 24H2)**

---

## 1. Project Philosophy & Core Invariants

1. **Native Taskbar Runtime**:
   - `MusicBar.exe` must remain 100% self-contained native code.
   - The settings window is native and runs inside `MusicBar.exe`; do not add a second runtime executable.
   - Do NOT introduce bulky runtime frameworks into `MusicBar.exe`.
   - Use Windows built-in system libraries: Direct2D (`d2d1`), DirectWrite (`dwrite`), WIC (`windowscodecs`), WinHTTP (`winhttp`), BCrypt (`bcrypt`), WASAPI (`mmdeviceapi`), and C++/WinRT (`windowsapp`).
2. **Lightweight & High Performance** (targets; verify measurements before claiming them):
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
├── CMakeLists.txt
├── README.md                      # User guide and build instructions
├── AGENTS.md                      # This contributor guide
├── .github/workflows/            # Windows build, tests, and release packaging
├── assets/                        # Icon, manifest, resource script, screenshot
├── docs/
│   ├── ARCHITECTURE.md            # Current implementation and module map
│   ├── ROADMAP.md                 # Outstanding work and manual verification
│   ├── SETTINGS_GUI.md            # Settings window and persistence schema
│   └── THIRD_PARTY.md            # Vendored dependencies and licenses
├── src/
│   ├── core/                      # App coordinator and shared types
│   ├── shell/                     # Taskbar docking and tray
│   ├── media/                     # GSMTC, NetEase source, process resolution
│   ├── audio/                     # WASAPI, FFT, volume, output devices
│   ├── lyrics/                    # Service, parsing, providers
│   ├── ui/                        # Direct2D, images, lyric scroll, popup, settings
│   └── net/                       # WinHTTP
├── tests/                         # CTest regression tests
└── third_party/                   # nlohmann/json and miniz
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
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON

# With Visual Studio 2026, use its x64 Native Tools Command Prompt and Ninja:
# cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
# cmake --build build-ninja --parallel
# ctest --test-dir build-ninja --output-on-failure

# 2. Compile Release executable
cmake --build build-vs --config Release --parallel
ctest --test-dir build-vs -C Release --output-on-failure

# 3. Output executable location
# .\build-vs\Release\MusicBar.exe
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
1. Consult [architecture](docs/ARCHITECTURE.md) for the current design and [roadmap](docs/ROADMAP.md) for outstanding work.
2. Maintain clean separation of concerns: UI rendering stays in `ui/`, audio in `audio/`, networking in `net/`, and media in `media/`.
3. Build with `cmake --build build-vs --config Release` and run CTest. Keep warnings visible and fix new warnings.
4. When changing settings, verify the native window builds and the settings store tests pass.
5. After verifying new components, update [ROADMAP.md](docs/ROADMAP.md) as appropriate.
