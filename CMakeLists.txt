cmake_minimum_required(VERSION 3.28)
project(MusicBar LANGUAGES C CXX)

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
    src/ui/VolumePopup.cpp
    src/ui/ImageLoader.cpp
    
    # Third party
    third_party/miniz/miniz.c
    
    # Resources
    assets/resource.rc
    assets/app.manifest
)

add_executable(MusicBar WIN32 ${SOURCES})
set_source_files_properties(assets/resource.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico"
)

# Windows libraries
target_link_libraries(MusicBar PRIVATE
    d2d1
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

    add_executable(SettingsStoreTests
        tests/SettingsStoreTests.cpp
        src/core/SettingsStore.cpp
    )
    add_test(NAME SettingsStoreTests COMMAND SettingsStoreTests)
endif()
