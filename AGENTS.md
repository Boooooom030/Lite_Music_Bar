# AGENTS.md — Contributor & AI Agent Operational Guide

> Project: **MusicBar (C++ Edition)**  
> Architecture: **Modern C++20 / Win32 / Direct2D / C++/WinRT / WASAPI**  
> Target OS: **Windows 10 1809+ / Windows 11 (21H2 - 24H2)**

---

## 1. Project Philosophy & Core Invariants

1. **Native Taskbar Runtime**:
   - `MusicBar.exe` must remain 100% self-contained native code.
   - The settings window runs inside `MusicBar.exe`; do not add a second runtime executable.
   - The settings window is C++ Qt Widgets on top of Fluent-Qt v1.10.0, statically linked with its platform/style/image plugins and resources into that same executable. Qt types stay in `src/ui/qt/` and the entry point.
   - Keep the taskbar renderer and media/audio/lyrics modules on Windows system APIs. Do not add unrelated frameworks or optional Qt QML, WebEngine, Python, or 3D runtimes.
   - Use Windows built-in system libraries for the taskbar runtime: Direct2D (`d2d1`), DirectWrite (`dwrite`), WIC (`windowscodecs`), WinHTTP (`winhttp`), BCrypt (`bcrypt`), WASAPI (`mmdeviceapi`), and C++/WinRT (`windowsapp`).
   - Distribute a portable ZIP containing one `MusicBar/` directory. Persist
     settings and their atomic-save temporary files only beside the EXE in
     `data/`; never fall back to AppData or the current working directory.
     Keep lyrics/artwork/render caches in memory. Do not add disk caches or
     logs just to populate the portable directory, and do not migrate old data.
2. **Lightweight & High Performance** (targets; verify measurements before claiming them):
   - Memory footprint target: **< 15 MB RAM** in steady-state operation.
   - Binary size limit: **< 100 MB** release executable (user revised this requirement on 2026-10-12).
   - CPU usage: **< 0.1%** during 60 FPS audio spectrum playback; **0.0%** when paused/idle.
3. **Rock-Solid Taskbar Integration**:
   - Do NOT attach small floating windows or raw popup child windows directly without clipping.
   - Invariant: The window covers the entire taskbar canvas (`(0, 0)` to `(taskbarWidth, taskbarHeight)`), and GDI `SetWindowRgn` is applied to restrict interactivity and visual drawing strictly to the music bar bounding box (e.g. 280×36 or 320×36).
   - All regions outside the bounding box must allow click-through directly to Explorer (Start menu, taskbar icons, system tray).
4. **Errors Must Be Explicit; Fallback Requires User Approval**:
   - The default response to a failed requirement, unavailable dependency,
     unsupported capability, invalid input, or failed operation is to report
     the specific error. Do not silently hide the failure or switch behavior.
   - **Every fallback design requires the user's explicit approval before it
     is implemented or enabled.** This includes alternate storage paths,
     renderers, data sources, runtimes, compatibility implementations, and
     degraded behavior. Do not design or add fallback "just in case".
   - Describe the concrete failure and proposed fallback when requesting that
     approval. General authorization to fix a bug or implement a feature is
     not approval for a fallback; its existence in old code is not approval.
   - Do not add compatibility or migration behavior unless the user requests
     it. Failed saves must report the error, never relocate user data.

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
│   ├── FLUENT_QT_SETTINGS_IMPLEMENTATION.md  # Settings rewrite specification
│   ├── FLUENT_QT_SETTINGS_ACCEPTANCE.md      # Build, test and acceptance evidence
│   ├── FLUENT_QT_ENVIRONMENT.md   # Local Qt / Fluent-Qt SDK install record
│   └── THIRD_PARTY.md            # Vendored dependencies and licenses
├── scripts/                       # build-qt-release.cmd and start scripts
├── src/
│   ├── core/                      # App coordinator and shared types
│   ├── shell/                     # Taskbar docking and tray
│   ├── media/                     # GSMTC, NetEase source, process resolution
│   ├── audio/                     # WASAPI, FFT, volume, output devices
│   ├── lyrics/                    # Service, parsing, providers
│   ├── ui/                        # Direct2D, images, lyric scroll, popup
│   ├── ui/qt/                     # Fluent-Qt settings window (Qt Widgets)
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
- **Qt 6.11.2 + Fluent-Qt v1.10.0 static SDK** at `%USERPROFILE%\Storage\tool\Qt-FluentQt`;
  the settings window links it, so this SDK is required, not optional.
  Qt Gui must include HarfBuzz; disabling it makes the supplementary-plane Fluent
  icons invisible. The application build checks this feature and SettingsUiTests
  verifies actual icon pixels, not just successful font registration.
  The Windows CI SDK must build Qt with `FEATURE_cxx20=ON` (set in the custom
  vcpkg triplet). MSVC 19.4x otherwise links a C++17 Qt WinRT implementation
  against the C++20 app and fails with `_COROUTINE_ABI` / LNK2038. Do not suppress
  the mismatch or lower the application's language standard; build a matching SDK.

### Command-Line Build Commands

```bat
:: 1. Release build with the Qt / Fluent-Qt static SDK (Ninja, static CRT /MT).
::    Builds into build-qt-release, never starts the program and never replaces a
::    running MusicBar.exe. Override QT_FLUENT_ROOT / QT_FLUENT_PREFIX /
::    FLUENTQT_PREFIX / QT_FLUENT_TOOLCHAIN from the environment if needed.
scripts\build-qt-release.cmd
ctest --test-dir build-qt-release --output-on-failure

:: 2. Output executable location
::    .\build-qt-release\MusicBar.exe
```

`build-vs` / `build-ninja` are the pre-Qt trees; they no longer configure, because
`CMakeLists.txt` now requires `find_package(Qt6)` and `find_package(FluentQt)`.
Use a fresh directory for the Qt build.

---

## 4. Coding Standards & Architectural Patterns

1. **C++20 & Modern Idioms**:
   - Use `std::unique_ptr` and `std::shared_ptr` for resource management.
   - Use `Microsoft::WRL::ComPtr<T>` for all Direct2D, DirectWrite, WIC, and WASAPI COM interfaces.
   - Use `winrt::com_ptr<T>` and C++/WinRT value types (`winrt::hstring`, `winrt::Windows::...`) for WinRT APIs.
   - Never use manual `delete` on raw pointers.
2. **Threading & Concurrency Model**:
   - **Main UI Thread**: Runs one `QApplication` event loop (`QCoreApplication::exec()`), owns the taskbar HWND and its WndProc, handles `WM_PAINT`, and drives Direct2D rendering. Qt dispatches the messages posted to the native taskbar window to that WndProc; do not add a second GUI thread, a second `QApplication`, or a `processEvents()` loop next to it. The settings window is the only Qt Widgets surface.
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
3. Build with `scripts\build-qt-release.cmd` and run CTest. Keep warnings visible and fix new warnings. Include FluentQt headers before `windows.h`: `rpcndr.h` defines `small` as a macro and collides with FluentElement's spacing tokens.
4. When changing settings, verify the native window builds and the settings store tests pass.
5. After verifying new components, update [ROADMAP.md](docs/ROADMAP.md) as appropriate.
