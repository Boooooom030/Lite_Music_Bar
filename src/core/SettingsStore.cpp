#include "SettingsStore.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <nlohmann/json.hpp>

namespace MusicBar {
namespace {

std::string_view SourceName(LyricsSource source) {
    switch (source) {
    case LyricsSource::Auto: return "auto";
    case LyricsSource::NetEase: return "netease";
    case LyricsSource::QQMusic: return "qq";
    case LyricsSource::Kugou: return "kugou";
    case LyricsSource::Soda: return "soda";
    case LyricsSource::Lrclib: return "lrclib";
    }
    return "auto";
}

bool ParseSource(const std::string& value, LyricsSource& source) {
    if (value == "auto") source = LyricsSource::Auto;
    else if (value == "netease") source = LyricsSource::NetEase;
    else if (value == "qq") source = LyricsSource::QQMusic;
    else if (value == "kugou") source = LyricsSource::Kugou;
    else if (value == "soda") source = LyricsSource::Soda;
    else if (value == "lrclib") source = LyricsSource::Lrclib;
    else return false;
    return true;
}

} // namespace

std::wstring SettingsStore::GetPath() {
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (length == 0) return {};
    std::wstring localAppData(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), length);
    if (copied == 0 || copied >= length) return {};
    localAppData.resize(copied);
    return (std::filesystem::path(localAppData) /
        L"MusicBar" / L"settings.json").wstring();
}

uint64_t SettingsStore::GetFileStamp() {
    const std::wstring path = GetPath();
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (path.empty() ||
        !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return 0;
    }
    return (static_cast<uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
        attributes.ftLastWriteTime.dwLowDateTime;
}

bool SettingsStore::TryParse(std::string_view jsonText, AppSettings& settings) {
    try {
        const auto document = nlohmann::json::parse(jsonText.begin(), jsonText.end());
        if (!document.is_object() || !document.contains("version") ||
            !document["version"].is_number_integer() ||
            document["version"].get<int>() != 1) {
            return false;
        }

        AppSettings parsed{};
        if (document.contains("showLyrics")) {
            if (!document["showLyrics"].is_boolean()) return false;
            parsed.showLyrics = document["showLyrics"].get<bool>();
        }
        if (document.contains("positionLocked")) {
            if (!document["positionLocked"].is_boolean()) return false;
            parsed.positionLocked = document["positionLocked"].get<bool>();
        }
        if (document.contains("lyricsSource")) {
            if (!document["lyricsSource"].is_string() ||
                !ParseSource(document["lyricsSource"].get<std::string>(),
                    parsed.lyricsSource)) {
                return false;
            }
        }
        if (document.contains("fftCaptureMode")) {
            if (!document["fftCaptureMode"].is_string()) return false;
            const auto mode = document["fftCaptureMode"].get<std::string>();
            if (mode == "player") parsed.fftCaptureMode = SpectrumCaptureMode::Player;
            else if (mode == "system") parsed.fftCaptureMode = SpectrumCaptureMode::System;
            else return false;
        }
        settings = parsed;
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::string SettingsStore::Serialize(const AppSettings& settings) {
    const nlohmann::json document = {
        {"version", 1},
        {"showLyrics", settings.showLyrics},
        {"positionLocked", settings.positionLocked},
        {"lyricsSource", SourceName(settings.lyricsSource)},
        {"fftCaptureMode", settings.fftCaptureMode == SpectrumCaptureMode::System
            ? "system" : "player"}
    };
    return document.dump(2) + "\n";
}

bool SettingsStore::Load(AppSettings& settings) {
    const std::wstring path = GetPath();
    if (path.empty()) return false;

    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 65536) return false;

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return false;
    const std::string data(std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{});
    if (file.bad()) return false;
    return TryParse(data, settings);
}

bool SettingsStore::Save(const AppSettings& settings) {
    const std::wstring path = GetPath();
    if (path.empty()) return false;
    const std::filesystem::path directory = std::filesystem::path(path).parent_path();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;

    wchar_t temporaryPath[MAX_PATH]{};
    if (!GetTempFileNameW(directory.c_str(), L"mbr", 0, temporaryPath)) return false;
    const HANDLE file = CreateFileW(temporaryPath, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(temporaryPath);
        return false;
    }

    const std::string data = Serialize(settings);
    DWORD written = 0;
    const bool succeeded = WriteFile(file, data.data(),
        static_cast<DWORD>(data.size()), &written, nullptr) != FALSE &&
        written == data.size() && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!succeeded || !MoveFileExW(temporaryPath, path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath);
        return false;
    }
    return true;
}

} // namespace MusicBar
