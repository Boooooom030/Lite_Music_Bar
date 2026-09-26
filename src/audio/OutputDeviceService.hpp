#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace MusicBar {

struct OutputDevice {
    std::wstring id;
    std::wstring name;
    bool isDefault = false;
};

class OutputDeviceService {
public:
    static HRESULT List(std::vector<OutputDevice>& devices);
    static HRESULT SetDefault(const std::wstring& id);
};

} // namespace MusicBar
