#pragma once

#include <windows.h>
#include <string_view>

namespace MusicBar {

// Resolve a GSMTC source AppUserModelId to a running application process.
// Returns zero when Windows does not expose a process matching that identity.
DWORD ResolveMediaProcess(std::wstring_view sourceAppId);

} // namespace MusicBar
