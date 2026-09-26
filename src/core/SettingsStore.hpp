#pragma once

#include "Types.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace MusicBar {

enum class SpectrumCaptureMode {
    Player,
    System
};

struct AppSettings {
    bool showLyrics = true;
    bool positionLocked = false;
    LyricsSource lyricsSource = LyricsSource::Auto;
    SpectrumCaptureMode fftCaptureMode = SpectrumCaptureMode::Player;
};

class SettingsStore {
public:
    static std::wstring GetPath();
    static uint64_t GetFileStamp();
    static bool TryParse(std::string_view jsonText, AppSettings& settings);
    static std::string Serialize(const AppSettings& settings);
    static bool Load(AppSettings& settings);
    static bool Save(const AppSettings& settings);
};

} // namespace MusicBar
