#include "core/SettingsStore.hpp"
#include <windows.h>
#include <filesystem>
#include <string>

int main() {
    using namespace MusicBar;

    AppSettings settings{};
    if (!SettingsStore::TryParse(
        R"({"version":1,"showLyrics":false,"positionLocked":true,"lyricsSource":"qq","fftCaptureMode":"system"})",
        settings)) return 1;
    if (settings.showLyrics || !settings.positionLocked ||
        settings.lyricsSource != LyricsSource::QQMusic ||
        settings.fftCaptureMode != SpectrumCaptureMode::System) return 2;

    AppSettings roundTrip{};
    if (!SettingsStore::TryParse(SettingsStore::Serialize(settings), roundTrip))
        return 3;
    if (roundTrip.showLyrics != settings.showLyrics ||
        roundTrip.positionLocked != settings.positionLocked ||
        roundTrip.lyricsSource != settings.lyricsSource ||
        roundTrip.fftCaptureMode != settings.fftCaptureMode) return 4;

    if (SettingsStore::TryParse(R"({"version":2})", roundTrip)) return 5;
    if (SettingsStore::TryParse(
        R"({"version":1,"fftCaptureMode":"unknown"})", roundTrip)) return 6;
    if (SettingsStore::TryParse(
        R"({"version":1,"showLyrics":"false"})", roundTrip)) return 7;
    if (SettingsStore::TryParse("{", roundTrip)) return 8;

    wchar_t temporaryDirectory[MAX_PATH]{};
    const DWORD tempLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    if (tempLength == 0 || tempLength >= MAX_PATH) return 9;
    const auto testDirectory = std::filesystem::path(temporaryDirectory) /
        (L"MusicBarSettingsTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directory(testDirectory)) return 10;
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA",
        testDirectory.wstring().c_str())) return 11;
    if (!SettingsStore::Save(settings)) return 12;
    AppSettings fromFile{};
    if (!SettingsStore::Load(fromFile)) return 13;
    if (fromFile.fftCaptureMode != SpectrumCaptureMode::System ||
        fromFile.lyricsSource != LyricsSource::QQMusic ||
        fromFile.showLyrics || !fromFile.positionLocked ||
        SettingsStore::GetFileStamp() == 0) return 14;

    std::error_code error;
    std::filesystem::remove(SettingsStore::GetPath(), error);
    std::filesystem::remove(testDirectory / L"MusicBar", error);
    std::filesystem::remove(testDirectory, error);
    return 0;
}
