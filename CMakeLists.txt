cmake_minimum_required(VERSION 3.28)
project(MusicBar LANGUAGES C CXX)

if(CMAKE_TOOLCHAIN_FILE)
    message(STATUS "SDK toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# The settings window ships its own icon, DPI awareness and version resource.
enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the MSVC runtime into the executable so releases need no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Enforce UTF-8 source & execution charset and warnings on MSVC
if(MSVC)
    add_compile_options(
        /utf-8
        /permissive-
        /W4
        /Zc:__cplusplus
        /EHsc
    )
    # Optimize release builds
    add_compile_options("$<$<CONFIG:Release>:/O2;/Oi;/Ot;/Gy>")
endif()

add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Source files
set(SOURCES
    # Core
    src/main.cpp
    src/core/App.cpp
    src/core/App.hpp
    src/core/AnimationClock.cpp
    src/core/SettingsStore.cpp
    
    # Shell & Taskbar
    src/shell/TaskbarDock.cpp
    src/shell/TrayIcon.cpp
    
    # Media & GSMTC
    src/media/MediaSession.cpp
    src/media/NetEaseMediaSource.cpp
    src/media/ProcessResolver.cpp
    
    # Audio & FFT
    src/audio/WasapiLoopback.cpp
    src/audio/FftAnalyzer.cpp
    src/audio/VolumeController.cpp
    src/audio/OutputDeviceService.cpp
    
    # Lyrics & Net
    src/net/HttpClient.cpp
    src/lyrics/Decrypter.cpp
    src/lyrics/LyricsParser.cpp
    src/lyrics/LyricsService.cpp
    src/lyrics/providers/NetEaseProvider.cpp
    src/lyrics/providers/QQMusicProvider.cpp
    src/lyrics/providers/KugouProvider.cpp
    src/lyrics/providers/SodaProvider.cpp
    src/lyrics/providers/LrclibProvider.cpp
    
    # UI & Rendering
    src/ui/Direct2DRenderer.cpp
    src/ui/ImageLoader.cpp
    src/ui/VolumePopup.cpp
    src/ui/SettingsWindow.cpp
    src/ui/SettingsWindow.hpp
    src/ui/qt/SettingsQtWindow.cpp
    src/ui/qt/SettingsQtWindow.hpp
    
    # Third party
    third_party/miniz/miniz.c
    
    # Resources
    assets/resource.rc
    assets/app.manifest
)

# The local/CI SDK location arrives through CMAKE_PREFIX_PATH; no machine path
# is hard-coded here (see scripts/build-qt-release.cmd and scripts/ci/).
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Widgets)
find_package(FluentQt CONFIG REQUIRED)

qt_add_executable(MusicBar WIN32 ${SOURCES})
set_source_files_properties(assets/resource.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico"
)

set_target_properties(MusicBar PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Windows libraries
target_link_libraries(MusicBar PRIVATE
    Qt6::Widgets
    FluentQt::FluentQt
    d2d1
    d3d11
    dxgi
    dcomp
    dwrite
    windowscodecs
    winhttp
    bcrypt
    ole32
    shlwapi
    windowsapp
    shell32
    user32
    gdi32
    dwmapi
    comctl32
)

# Static Qt needs its platform, native style and icon image plugins registered
# explicitly; the default plugin set stays in place so text and font handling
# remain complete.
qt_import_plugins(MusicBar INCLUDE
    Qt6::QWindowsIntegrationPlugin
    Qt6::QModernWindowsStylePlugin
    Qt6::QICOPlugin
)

# The window icon is compiled into the executable, so the release directory only
# needs MusicBar.exe.
qt_add_resources(MusicBar "musicbar_resources" PREFIX "/" FILES assets/icon.ico)

# Set subsystem to Windows GUI
set_target_properties(MusicBar PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(CTest)
if(BUILD_TESTING)
    add_executable(LyricScrollTests tests/LyricScrollTests.cpp)
    add_test(NAME LyricScrollTests COMMAND LyricScrollTests)

    add_executable(FftAnalyzerTests
        tests/FftAnalyzerTests.cpp
        src/audio/FftAnalyzer.cpp
    )
    add_test(NAME FftAnalyzerTests COMMAND FftAnalyzerTests)

    add_executable(LyricsParserTests
        tests/LyricsParserTests.cpp
        src/lyrics/LyricsParser.cpp
        src/lyrics/Decrypter.cpp
        third_party/miniz/miniz.c
    )
    target_link_libraries(LyricsParserTests PRIVATE bcrypt crypt32)
    add_test(NAME LyricsParserTests COMMAND LyricsParserTests)

    add_executable(LyricsFitTests tests/LyricsFitTests.cpp)
    add_test(NAME LyricsFitTests COMMAND LyricsFitTests)

    add_executable(LyricsServiceTests
        tests/LyricsServiceTests.cpp
        src/lyrics/LyricsService.cpp
        src/lyrics/LyricsParser.cpp
        src/lyrics/Decrypter.cpp
        src/lyrics/providers/NetEaseProvider.cpp
        src/lyrics/providers/QQMusicProvider.cpp
        src/lyrics/providers/KugouProvider.cpp
        src/lyrics/providers/SodaProvider.cpp
        src/lyrics/providers/LrclibProvider.cpp
        src/net/HttpClient.cpp
        third_party/miniz/miniz.c
    )
    target_link_libraries(LyricsServiceTests PRIVATE bcrypt crypt32 winhttp)
    add_test(NAME LyricsServiceTests COMMAND LyricsServiceTests)
    set_tests_properties(LyricsServiceTests PROPERTIES TIMEOUT 10)

    add_executable(SettingsStoreTests
        tests/SettingsStoreTests.cpp
        src/core/SettingsStore.cpp
    )
    target_compile_definitions(SettingsStoreTests PRIVATE MUSICBAR_TESTING)
    add_test(NAME SettingsStoreTests COMMAND SettingsStoreTests)

    # Development-only UI verification. The release package still ships only
    # MusicBar.exe; this test never docks or touches the user's configuration.
    qt_add_executable(SettingsUiTests
        tests/SettingsUiTests.cpp
        src/ui/qt/SettingsQtWindow.cpp
        src/ui/qt/SettingsQtWindow.hpp
        src/core/SettingsStore.cpp
    )
    set_target_properties(SettingsUiTests PROPERTIES AUTOMOC ON)
    target_compile_definitions(SettingsUiTests PRIVATE MUSICBAR_TESTING)
    target_link_libraries(SettingsUiTests PRIVATE Qt6::Widgets FluentQt::FluentQt advapi32)
    qt_import_plugins(SettingsUiTests INCLUDE
        Qt6::QWindowsIntegrationPlugin Qt6::QModernWindowsStylePlugin Qt6::QICOPlugin)
    qt_add_resources(SettingsUiTests "settings_test_resources" PREFIX "/" FILES assets/icon.ico)
    add_test(NAME SettingsUiTests COMMAND SettingsUiTests --artifacts settings-verification)
    set_tests_properties(SettingsUiTests PROPERTIES TIMEOUT 30)

    add_executable(Direct2DRendererTests
        tests/Direct2DRendererTests.cpp
        src/ui/Direct2DRenderer.cpp
        src/ui/ImageLoader.cpp
    )
    target_link_libraries(Direct2DRendererTests PRIVATE
        d2d1 d3d11 dxgi dcomp dwrite windowscodecs ole32 user32 advapi32
    )
    add_test(NAME Direct2DRendererTests COMMAND Direct2DRendererTests)
endif()
