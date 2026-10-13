@echo off
rem Build MusicBar (Release, x64, static CRT) against the machine-local
rem Qt 6.11.2 + Fluent-Qt v1.10.0 static SDK.
rem
rem The script only configures and builds into build-qt-release; it never starts
rem the program and never replaces a running MusicBar.exe.
rem
rem Every path below can be overridden from the environment:
rem   QT_FLUENT_ROOT        SDK root        (default: %USERPROFILE%\Storage\tool\Qt-FluentQt)
rem   QT_FLUENT_PREFIX      Qt prefix       (default: <root>\installed\x64-windows-static-release)
rem   FLUENTQT_PREFIX       FluentQt SDK    (default: <root>\sdk\FluentQt)
rem   QT_FLUENT_TOOLCHAIN   CMake toolchain (default: <root>\toolchains\msvc-x64-static-release.cmake)
rem   MUSICBAR_BUILD_DIR    Output tree     (default: <repo>\build-qt-release)
setlocal enabledelayedexpansion

set "QT_FLUENT_ROOT=%QT_FLUENT_ROOT%"
if "%QT_FLUENT_ROOT%"=="" set "QT_FLUENT_ROOT=%USERPROFILE%\Storage\tool\Qt-FluentQt"
set "QT_FLUENT_PREFIX=%QT_FLUENT_PREFIX%"
if "%QT_FLUENT_PREFIX%"=="" set "QT_FLUENT_PREFIX=%QT_FLUENT_ROOT%\installed\x64-windows-static-release"
set "FLUENTQT_PREFIX=%FLUENTQT_PREFIX%"
if "%FLUENTQT_PREFIX%"=="" set "FLUENTQT_PREFIX=%QT_FLUENT_ROOT%\sdk\FluentQt"
set "QT_FLUENT_TOOLCHAIN=%QT_FLUENT_TOOLCHAIN%"
if "%QT_FLUENT_TOOLCHAIN%"=="" set "QT_FLUENT_TOOLCHAIN=%QT_FLUENT_ROOT%\toolchains\msvc-x64-static-release.cmake"

if not exist "%QT_FLUENT_PREFIX%\" (
    echo [build-qt-release] Qt prefix not found: %QT_FLUENT_PREFIX%
    exit /b 1
)
if not exist "%FLUENTQT_PREFIX%\" (
    echo [build-qt-release] FluentQt SDK not found: %FLUENTQT_PREFIX%
    exit /b 1
)
if not exist "%QT_FLUENT_TOOLCHAIN%" (
    echo [build-qt-release] Toolchain file not found: %QT_FLUENT_TOOLCHAIN%
    exit /b 1
)

rem Locate a Visual Studio installation that ships the x64 C++ toolset.
set "VS_INSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%i"
)
if "%VS_INSTALL%"=="" (
    echo [build-qt-release] Visual Studio with the x64 C++ toolset was not found.
    exit /b 1
)
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%ERRORLEVEL%"=="0" (
    echo [build-qt-release] Failed to initialize the MSVC x64 environment.
    exit /b 1
)

set "SOURCE_DIR=%~dp0.."
set "BUILD_DIR=%SOURCE_DIR%\build-qt-release"
if not "%MUSICBAR_BUILD_DIR%"=="" set "BUILD_DIR=%MUSICBAR_BUILD_DIR%"

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    "-DCMAKE_TOOLCHAIN_FILE=%QT_FLUENT_TOOLCHAIN%" ^
    "-DCMAKE_PREFIX_PATH=%FLUENTQT_PREFIX%;%QT_FLUENT_PREFIX%"
if not "%ERRORLEVEL%"=="0" exit /b 1

cmake --build "%BUILD_DIR%" --parallel 6
if not "%ERRORLEVEL%"=="0" exit /b 1

echo [build-qt-release] Output: %BUILD_DIR%\MusicBar.exe
endlocal
exit /b 0
