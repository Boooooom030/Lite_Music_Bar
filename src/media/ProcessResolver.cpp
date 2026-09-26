#include "ProcessResolver.hpp"
#include <appmodel.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MusicBar {
namespace {

bool MatchesSource(DWORD pid, std::wstring_view sourceAppId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;

    bool matches = false;
    const bool executable = sourceAppId.size() >= 4 &&
        _wcsicmp(std::wstring(sourceAppId.substr(sourceAppId.size() - 4)).c_str(),
            L".exe") == 0;
    if (sourceAppId.find(L'\\') == std::wstring_view::npos &&
        sourceAppId.find(L'/') == std::wstring_view::npos &&
        !executable) {
        UINT32 length = 0;
        if (GetApplicationUserModelId(process, &length, nullptr) ==
            ERROR_INSUFFICIENT_BUFFER && length > 0 && length < 4096) {
            std::wstring appId(length, L'\0');
            if (GetApplicationUserModelId(process, &length, appId.data()) ==
                ERROR_SUCCESS) {
                appId.resize(wcsnlen_s(appId.c_str(), appId.size()));
                matches = _wcsicmp(appId.c_str(),
                    std::wstring(sourceAppId).c_str()) == 0;
            }
        }
    } else {
        wchar_t path[32768]{};
        DWORD length = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(process, 0, path, &length)) {
            const std::wstring source(sourceAppId);
            const wchar_t* name = wcsrchr(path, L'\\');
            matches = _wcsicmp(path, source.c_str()) == 0 ||
                _wcsicmp(name ? name + 1 : path, source.c_str()) == 0;
        }
    }
    CloseHandle(process);
    return matches;
}

DWORD FindWindowWithAppId(std::wstring_view sourceAppId) {
    struct Search {
        std::wstring source;
        DWORD pid = 0;
    } search{std::wstring(sourceAppId)};
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        Microsoft::WRL::ComPtr<IPropertyStore> store;
        if (FAILED(SHGetPropertyStoreForWindow(window,
            IID_PPV_ARGS(&store)))) return TRUE;
        const PROPERTYKEY key = INIT_PKEY_AppUserModel_ID;
        PROPVARIANT value{};
        if (SUCCEEDED(store->GetValue(key, &value))) {
            if (value.vt == VT_LPWSTR && value.pwszVal &&
                _wcsicmp(value.pwszVal, state.source.c_str()) == 0) {
                GetWindowThreadProcessId(window, &state.pid);
            }
            PropVariantClear(&value);
        }
        return state.pid ? FALSE : TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.pid;
}

} // namespace

DWORD ResolveMediaProcess(std::wstring_view sourceAppId) {
    if (sourceAppId.empty()) return 0;
    if (const DWORD windowPid = FindWindowWithAppId(sourceAppId)) {
        return windowPid;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    std::unordered_map<DWORD, DWORD> parents;
    std::vector<DWORD> matches;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            parents.emplace(entry.th32ProcessID, entry.th32ParentProcessID);
            if (MatchesSource(entry.th32ProcessID, sourceAppId)) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (matches.empty()) return 0;

    // Process loopback includes descendants. Select the app's root process
    // when a browser or player runs several processes with the same image.
    const std::unordered_set<DWORD> matched(matches.begin(), matches.end());
    for (DWORD pid : matches) {
        const auto parent = parents.find(pid);
        if (parent != parents.end() && !matched.contains(parent->second)) {
            return pid;
        }
    }
    return matches.front();
}

} // namespace MusicBar
