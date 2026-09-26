#include "NetEaseMediaSource.hpp"
#include "net/HttpClient.hpp"
#include <nlohmann/json.hpp>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string_view>

namespace MusicBar {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view PlayerPattern =
    "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 90 "
    "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 8D A5 ? ? ? ? "
    "5F 5D C3 CC CC CC CC CC 48 89 4C 24 ? 55 57 48 81 EC ? ? ? ? "
    "48 8D 6C 24 ? 48 8D 7C 24";
constexpr std::string_view SchedulePattern =
    "66 0F 2E 0D ? ? ? ? 7A ? 75 ? 66 0F 2E 15";

struct TrackMetadata {
    MediaTrackInfo track;
    std::wstring coverUrl;
};

template<typename T>
std::optional<T> ReadValue(HANDLE process, uintptr_t address) {
    T value{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address),
        &value, sizeof(value), &read) || read != sizeof(value)) return std::nullopt;
    return value;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), length) != length) return {};
    return wide;
}

std::string JsonString(const nlohmann::json& value, const char* key) {
    if (!value.is_object()) return {};
    const auto field = value.find(key);
    return field != value.end() && field->is_string()
        ? field->get<std::string>() : std::string{};
}

std::string JsonId(const nlohmann::json& value) {
    if (!value.is_object()) return {};
    const auto id = value.find("id");
    if (id == value.end()) return {};
    if (id->is_string()) return id->get<std::string>();
    if (id->is_number_integer()) return std::to_string(id->get<int64_t>());
    return {};
}

std::wstring LocalAppData() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size < 2 || size > 32768) return {};
    std::wstring path(size, L'\0');
    const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), size);
    if (copied == 0 || copied >= size) return {};
    path.resize(copied);
    return path;
}

nlohmann::json ReadJsonFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    std::string text;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= 4 * 1024 * 1024) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read,
            nullptr) || read != static_cast<DWORD>(text.size())) text.clear();
    }
    CloseHandle(file);
    if (text.empty()) return {};
    return nlohmann::json::parse(text, nullptr, false);
}

TrackMetadata ParseTrack(const nlohmann::json& data) {
    TrackMetadata metadata;
    metadata.track.title = Utf8ToWide(JsonString(data, "name"));
    const auto duration = data.find("duration");
    if (duration != data.end() && duration->is_number_integer()) {
        metadata.track.durationMs = std::max<int64_t>(0, duration->get<int64_t>());
    }
    const auto artists = data.find("artists");
    if (artists != data.end() && artists->is_array()) {
        for (const auto& artist : *artists) {
            const auto name = Utf8ToWide(JsonString(artist, "name"));
            if (name.empty()) continue;
            if (!metadata.track.artist.empty()) metadata.track.artist += L" / ";
            metadata.track.artist += name;
        }
    }
    const auto album = data.find("album");
    if (album != data.end() && album->is_object()) {
        metadata.track.album = Utf8ToWide(JsonString(*album, "name"));
        std::string url = JsonString(*album, "cover");
        if (url.empty()) url = JsonString(*album, "picUrl");
        metadata.coverUrl = Utf8ToWide(url);
        if (metadata.coverUrl.starts_with(L"http://")) {
            metadata.coverUrl.replace(0, 7, L"https://");
        }
    }
    return metadata;
}

std::optional<TrackMetadata> ReadMetadata(const std::string& identity) {
    const auto appData = LocalAppData();
    if (appData.empty()) return std::nullopt;
    const auto directory = appData + L"\\NetEase\\CloudMusic\\WebData\\file\\";
    const auto playlist = ReadJsonFile(directory + L"playingList");
    if (playlist.is_object() && playlist.contains("list") &&
        playlist["list"].is_array()) {
        for (const auto& item : playlist["list"]) {
            if (JsonId(item) != identity || !item.is_object() ||
                !item.contains("track") || !item["track"].is_object()) continue;
            auto metadata = ParseTrack(item["track"]);
            if (!metadata.track.title.empty()) return metadata;
        }
    }

    // Private FM keeps its active track in fmPlay instead of playingList.
    const auto fm = ReadJsonFile(directory + L"fmPlay");
    if (fm.is_object() && fm.contains("queue") && fm["queue"].is_array()) {
        for (const auto& item : fm["queue"]) {
            if (JsonId(item) != identity || !item.is_object()) continue;
            auto metadata = ParseTrack(item);
            if (!metadata.track.title.empty()) return metadata;
        }
    }
    return std::nullopt;
}

bool IsCloudMusicProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH]{};
    DWORD length = MAX_PATH;
    const bool found = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!found) return false;
    const wchar_t* filename = wcsrchr(path, L'\\');
    return _wcsicmp(filename ? filename + 1 : path, L"cloudmusic.exe") == 0;
}

DWORD CurrentCloudMusicPid() {
    struct Search { DWORD pid = 0; } search;
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(value);
        wchar_t className[64]{};
        if (!GetClassNameW(window, className, 64) ||
            wcscmp(className, L"OrpheusBrowserHost") != 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!IsCloudMusicProcess(pid)) return TRUE;
        result.pid = pid;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.pid;
}

std::optional<std::pair<uintptr_t, size_t>> FindTextSection(
    HANDLE process, uintptr_t moduleBase) {
    std::array<uint8_t, 4096> header{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(moduleBase),
        header.data(), header.size(), &read) || read != header.size()) return std::nullopt;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, header.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        dos.e_lfanew > 2048) return std::nullopt;
    const auto nt = static_cast<size_t>(dos.e_lfanew);
    DWORD signature = 0;
    std::memcpy(&signature, header.data() + nt, sizeof(signature));
    if (signature != IMAGE_NT_SIGNATURE) return std::nullopt;
    IMAGE_FILE_HEADER fileHeader{};
    std::memcpy(&fileHeader, header.data() + nt + 4, sizeof(fileHeader));
    if (fileHeader.NumberOfSections == 0 || fileHeader.NumberOfSections > 32) {
        return std::nullopt;
    }
    const size_t sections = nt + 4 + sizeof(fileHeader) +
        fileHeader.SizeOfOptionalHeader;
    if (sections + static_cast<size_t>(fileHeader.NumberOfSections) *
        sizeof(IMAGE_SECTION_HEADER) > header.size()) return std::nullopt;
    for (unsigned i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, header.data() + sections +
            i * sizeof(section), sizeof(section));
        if (std::memcmp(section.Name, ".text", 5) == 0 &&
            section.Misc.VirtualSize > 0 &&
            section.Misc.VirtualSize <= 64 * 1024 * 1024) {
            return std::pair{moduleBase + section.VirtualAddress,
                static_cast<size_t>(section.Misc.VirtualSize)};
        }
    }
    return std::nullopt;
}

std::vector<int> ParsePattern(std::string_view signature) {
    std::istringstream input{std::string(signature)};
    std::vector<int> pattern;
    std::string token;
    while (input >> token) {
        pattern.push_back(token == "?" ? -1 : std::stoi(token, nullptr, 16));
    }
    return pattern;
}

uintptr_t FindPattern(HANDLE process, uintptr_t start, size_t size,
                      const std::vector<int>& pattern) {
    constexpr size_t Chunk = 64 * 1024;
    if (pattern.empty() || size < pattern.size()) return 0;
    std::vector<uint8_t> bytes(Chunk + pattern.size());
    for (size_t offset = 0; offset < size; offset += Chunk) {
        const size_t count = std::min(bytes.size(), size - offset);
        SIZE_T read = 0;
        if (!ReadProcessMemory(process,
            reinterpret_cast<const void*>(start + offset), bytes.data(),
            count, &read) || read != count) return 0;
        for (size_t i = 0; i < std::min(Chunk, size - offset) &&
            i + pattern.size() <= count; ++i) {
            if (bytes[i] != pattern[0]) continue;
            bool found = true;
            for (size_t j = 1; j < pattern.size(); ++j) {
                if (pattern[j] >= 0 && bytes[i + j] != pattern[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return start + offset + i;
        }
    }
    return 0;
}

std::optional<std::string> ReadIdentity(HANDLE process, uintptr_t player) {
    const auto info = ReadValue<uintptr_t>(process, player + 0x50);
    if (!info || *info == 0) return std::nullopt;
    const uintptr_t text = *info + 0x10;
    const auto length = ReadValue<uint64_t>(process, text + 0x10);
    if (!length || *length == 0 || *length > 256) return std::nullopt;
    uintptr_t address = text;
    if (*length > 15) {
        const auto pointer = ReadValue<uintptr_t>(process, text);
        if (!pointer || !*pointer) return std::nullopt;
        address = *pointer;
    }
    std::string identity(static_cast<size_t>(*length), '\0');
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(address),
        identity.data(), identity.size(), &read) || read != identity.size()) {
        return std::nullopt;
    }
    const auto zero = identity.find('\0');
    if (zero != std::string::npos) identity.resize(zero);
    const auto separator = identity.find('_');
    if (separator != std::string::npos) identity.resize(separator);
    while (!identity.empty() && identity.back() == ' ') identity.pop_back();
    if (identity.empty()) return std::nullopt;
    return identity;
}

std::wstring ThumbnailUrl(std::wstring url) {
    if (url.find(L".music.126.net/") != std::wstring::npos &&
        url.find(L"param=") == std::wstring::npos) {
        url += url.find(L'?') == std::wstring::npos
            ? L"?param=128y128" : L"&param=128y128";
    }
    return url;
}

bool LooksLikeImage(const std::vector<uint8_t>& bytes) {
    return (bytes.size() >= 3 && bytes[0] == 0xff &&
            bytes[1] == 0xd8 && bytes[2] == 0xff) ||
        (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
            bytes[2] == 'N' && bytes[3] == 'G') ||
        (bytes.size() >= 6 && bytes[0] == 'G' && bytes[1] == 'I' &&
            bytes[2] == 'F') ||
        (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
            bytes[8] == 'W' && bytes[9] == 'E');
}

} // namespace

void NetEaseMediaSource::ResetProcess() {
    m_process.reset();
    m_processId = 0;
    m_playerAddress = 0;
    m_scheduleAddress = 0;
    m_identity.clear();
    m_coverUrl.clear();
    m_track = {};
    m_nextMetadataAttempt = {};
    m_coverAttempts = 0;
    m_nextCoverAttempt = {};
}

bool NetEaseMediaSource::Attach(DWORD pid) {
    std::unique_ptr<void, HandleCloser> process(
        OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pid));
    if (!process) return false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
        TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    uintptr_t base = 0;
    size_t moduleSize = 0;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, L"cloudmusic.dll") == 0) {
                base = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                moduleSize = module.modBaseSize;
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    if (!base || !moduleSize) return false;
    const auto text = FindTextSection(process.get(), base);
    if (!text) return false;
    static const auto playerPattern = ParsePattern(PlayerPattern);
    static const auto schedulePattern = ParsePattern(SchedulePattern);
    const auto playerMatch = FindPattern(process.get(), text->first,
        text->second, playerPattern);
    const auto scheduleMatch = FindPattern(process.get(), text->first,
        text->second, schedulePattern);
    if (!playerMatch || !scheduleMatch) return false;
    const auto playerDisplacement = ReadValue<int32_t>(process.get(), playerMatch + 3);
    const auto scheduleDisplacement = ReadValue<int32_t>(process.get(), scheduleMatch + 4);
    if (!playerDisplacement || !scheduleDisplacement) return false;
    const auto player = static_cast<uintptr_t>(
        static_cast<int64_t>(playerMatch + 7) + *playerDisplacement);
    const auto schedule = static_cast<uintptr_t>(
        static_cast<int64_t>(scheduleMatch + 8) + *scheduleDisplacement);
    if (player < base || player >= base + moduleSize ||
        schedule < base || schedule >= base + moduleSize) return false;
    const auto status = ReadValue<int32_t>(process.get(), player + 0x60);
    if (!status || *status < 0 || *status > 4) return false;
    m_process = std::move(process);
    m_processId = pid;
    m_playerAddress = player;
    m_scheduleAddress = schedule;
    return true;
}

void NetEaseMediaSource::CollectCovers() {
    for (auto request = m_coverRequests.begin(); request != m_coverRequests.end();) {
        if (request->result.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            ++request;
            continue;
        }
        std::vector<uint8_t> bytes;
        try { bytes = request->result.get(); } catch (...) {}
        if (!bytes.empty()) {
            if (request->url == m_coverUrl && !m_identity.empty()) {
                m_track.thumbnailBytes = bytes;
            }
            if (bytes.size() <= 1024 * 1024) {
                const auto cached = std::find_if(m_coverCache.begin(),
                    m_coverCache.end(), [&](const auto& item) {
                        return item.first == request->url;
                    });
                if (cached != m_coverCache.end()) m_coverCache.erase(cached);
                m_coverCache.emplace_front(request->url, std::move(bytes));
                if (m_coverCache.size() > 8) m_coverCache.pop_back();
            }
        }
        request = m_coverRequests.erase(request);
    }
}

void NetEaseMediaSource::StartCoverIfNeeded(Clock::time_point now) {
    if (!m_track.thumbnailBytes.empty() ||
        !m_coverUrl.starts_with(L"https://") || m_coverRequests.size() >= 2 ||
        now < m_nextCoverAttempt) return;
    const bool pending = std::any_of(m_coverRequests.begin(),
        m_coverRequests.end(), [&](const CoverRequest& request) {
            return request.url == m_coverUrl;
        });
    if (pending) return;
    constexpr int RetrySeconds[] = {2, 5, 15, 60};
    const unsigned index = std::min(m_coverAttempts, 3u);
    m_coverAttempts = std::min(m_coverAttempts + 1, 4u);
    m_nextCoverAttempt = now + std::chrono::seconds(RetrySeconds[index]);
    const auto url = m_coverUrl;
    try {
        m_coverRequests.push_back({url, std::async(std::launch::async,
            [url] {
                auto image = HttpClient{2500}.Get(ThumbnailUrl(url));
                if (!image.success || image.rawBytes.size() > 1024 * 1024 ||
                    !LooksLikeImage(image.rawBytes)) return std::vector<uint8_t>{};
                return std::move(image.rawBytes);
            })});
    } catch (...) {}
}

std::optional<MediaTrackInfo> NetEaseMediaSource::Read() {
    const auto now = Clock::now();
    CollectCovers();
    const DWORD pid = CurrentCloudMusicPid();
    if (!pid) {
        ResetProcess();
        return std::nullopt;
    }
    if (pid != m_processId) {
        ResetProcess();
        m_processId = pid;
        m_nextAttachAttempt = {};
    }
    if (!m_process) {
        if (now < m_nextAttachAttempt) return std::nullopt;
        m_nextAttachAttempt = now + std::chrono::seconds(2);
        if (!Attach(pid)) return std::nullopt;
    }
    const auto status = ReadValue<int32_t>(m_process.get(), m_playerAddress + 0x60);
    if (!status || *status < 0 || *status > 4) {
        ResetProcess();
        m_nextAttachAttempt = now + std::chrono::seconds(2);
        return std::nullopt;
    }
    if (*status == 0) {
        m_identity.clear();
        m_coverUrl.clear();
        m_track = {};
        return std::nullopt;
    }
    const auto identity = ReadIdentity(m_process.get(), m_playerAddress);
    if (!identity) return std::nullopt;
    if (*identity != m_identity) {
        m_identity = *identity;
        m_track = {};
        m_coverUrl.clear();
        m_coverAttempts = 0;
        m_nextCoverAttempt = {};
        m_nextMetadataAttempt = {};
    }
    if (m_track.title.empty()) {
        const auto cachedTrack = std::find_if(m_trackCache.begin(),
            m_trackCache.end(), [&](const CachedTrack& item) {
                return item.identity == m_identity;
            });
        if (cachedTrack != m_trackCache.end()) {
            m_track = cachedTrack->track;
            m_coverUrl = cachedTrack->coverUrl;
            auto entry = std::move(*cachedTrack);
            m_trackCache.erase(cachedTrack);
            m_trackCache.push_front(std::move(entry));
        } else {
            if (now < m_nextMetadataAttempt) return std::nullopt;
            m_nextMetadataAttempt = now + std::chrono::milliseconds(500);
            const auto metadata = ReadMetadata(m_identity);
            if (!metadata) return std::nullopt;
            m_track = metadata->track;
            m_coverUrl = metadata->coverUrl;
            m_trackCache.push_front({m_identity, m_track, m_coverUrl});
            if (m_trackCache.size() > 32) m_trackCache.pop_back();
        }
        const auto cached = std::find_if(m_coverCache.begin(),
            m_coverCache.end(), [&](const auto& item) {
                return item.first == m_coverUrl;
            });
        if (cached != m_coverCache.end()) m_track.thumbnailBytes = cached->second;
    }
    const auto duration = ReadValue<double>(m_process.get(), m_playerAddress + 0xA8);
    if (duration && std::isfinite(*duration) && *duration > 0 &&
        *duration < 86400) {
        m_track.durationMs = static_cast<int64_t>(*duration * 1000);
    }
    const auto position = ReadValue<double>(m_process.get(), m_scheduleAddress);
    if (position && std::isfinite(*position) && *position >= 0) {
        m_track.positionMs = static_cast<int64_t>(*position * 1000);
        if (m_track.durationMs > 0) {
            m_track.positionMs = std::min(m_track.positionMs, m_track.durationMs);
        }
    }
    m_track.isPlaying = *status == 1;
    m_track.sourceAppId = L"cloudmusic.exe";
    m_track.sourceProcessId = m_processId;
    m_track.lastUpdatedTime = now;
    StartCoverIfNeeded(now);
    return m_track;
}

} // namespace MusicBar
