#include "core/SettingsStore.hpp"
#include <windows.h>
#include <filesystem>
#include <iterator>
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
        (L"MusicBar便携配置测试-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directory(testDirectory)) return 10;

    // User profile and shortcut working directories must not control storage.
    const auto executableSettings = SettingsStore::GetPath();
    if (executableSettings.empty()) return 11;
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA",
        testDirectory.wstring().c_str())) return 15;
    if (SettingsStore::GetPath() != executableSettings) return 16;
    const auto originalDirectory = std::filesystem::current_path();
    std::filesystem::current_path(testDirectory);
    if (SettingsStore::GetPath() != executableSettings) return 17;
    std::filesystem::current_path(originalDirectory);

    if (!SetEnvironmentVariableW(L"MUSICBAR_TEST_EXECUTABLE_DIRECTORY",
            testDirectory.wstring().c_str())) return 18;
    if (std::filesystem::path(SettingsStore::GetPath()) !=
            testDirectory / L"data" / L"settings.json") return 19;
    if (!SettingsStore::Save(settings)) return 12;
    AppSettings fromFile{};
    if (!SettingsStore::Load(fromFile)) return 13;
    if (fromFile.fftCaptureMode != SpectrumCaptureMode::System ||
        fromFile.lyricsSource != LyricsSource::QQMusic ||
        fromFile.showLyrics || !fromFile.positionLocked ||
        SettingsStore::GetFileStamp() == 0) return 14;

    // Atomic-save temporary files must be local and removed on success.
    if (std::distance(std::filesystem::directory_iterator(testDirectory / L"data"),
            std::filesystem::directory_iterator{}) != 1) return 20;
    if (std::filesystem::exists(testDirectory / L"MusicBar")) return 21;

    // A blocked portable destination must fail rather than use AppData.
    const auto blocked = testDirectory / L"blocked";
    std::filesystem::create_directory(blocked);
    {
        const HANDLE file = CreateFileW((blocked / L"data").c_str(), GENERIC_WRITE,
            0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 22;
        CloseHandle(file);
    }
    if (!SetEnvironmentVariableW(L"MUSICBAR_TEST_EXECUTABLE_DIRECTORY",
            blocked.wstring().c_str())) return 23;
    if (SettingsStore::Save(settings)) return 24;
    if (std::filesystem::exists(testDirectory / L"MusicBar")) return 25;
    if (!SetEnvironmentVariableW(L"MUSICBAR_TEST_EXECUTABLE_DIRECTORY",
            testDirectory.wstring().c_str())) return 26;

    std::error_code error;
    std::filesystem::remove(SettingsStore::GetPath(), error);
    std::filesystem::remove(testDirectory / L"data", error);
    std::filesystem::remove(blocked / L"data", error);
    std::filesystem::remove(blocked, error);
    std::filesystem::remove(testDirectory, error);
    return 0;
}
