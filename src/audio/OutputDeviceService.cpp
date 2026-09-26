#include "OutputDeviceService.hpp"
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>
#include <wrl/client.h>
#include <utility>

namespace MusicBar {

namespace {

using Microsoft::WRL::ComPtr;

// Windows does not publish a system-default endpoint setter in the Core Audio
// SDK. Keep the PolicyConfig ABI isolated here for Windows 10/11 compatibility.
// Vtable: https://github.com/amate/SetDefaultAudioDevice/blob/master/PolicyConfig.h
struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8"))
    IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT,
        WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*,
        WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT,
        INT64*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,
        const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,
        const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

constexpr GUID kPolicyConfigClient = {
    0x870AF99C, 0x171D, 0x4F9E,
    {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}
};

HRESULT CreateEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator) {
    return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));
}

std::wstring DeviceId(IMMDevice* device) {
    LPWSTR raw = nullptr;
    if (!device || FAILED(device->GetId(&raw)) || !raw) {
        CoTaskMemFree(raw);
        return {};
    }
    std::wstring id(raw);
    CoTaskMemFree(raw);
    return id;
}

std::wstring FriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return {};
    PROPVARIANT value{};
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

} // namespace

HRESULT OutputDeviceService::List(std::vector<OutputDevice>& devices) {
    devices.clear();
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateEnumerator(enumerator);
    if (FAILED(hr)) return hr;

    std::wstring defaultId;
    ComPtr<IMMDevice> current;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
        &current))) {
        defaultId = DeviceId(current.Get());
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
        &collection);
    if (FAILED(hr)) return hr;
    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) return hr;
    devices.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) continue;
        std::wstring id = DeviceId(device.Get());
        if (id.empty()) continue;
        std::wstring name = FriendlyName(device.Get());
        if (name.empty()) name = L"输出设备 " + std::to_wstring(index + 1);
        const bool isDefault = id == defaultId;
        devices.push_back({std::move(id), std::move(name), isDefault});
    }
    return S_OK;
}

HRESULT OutputDeviceService::SetDefault(const std::wstring& id) {
    if (id.empty()) return E_INVALIDARG;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateEnumerator(enumerator);
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) return hr;
    DWORD state = 0;
    hr = device->GetState(&state);
    if (FAILED(hr)) return hr;
    if (!(state & DEVICE_STATE_ACTIVE)) return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE);

    ComPtr<IPolicyConfig> policy;
    hr = CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&policy));
    if (FAILED(hr)) return hr;
    hr = policy->SetDefaultEndpoint(id.c_str(), eMultimedia);
    if (FAILED(hr)) return hr;
    hr = policy->SetDefaultEndpoint(id.c_str(), eConsole);
    if (FAILED(hr)) return hr;

    ComPtr<IMMDevice> selected;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &selected);
    if (FAILED(hr)) return hr;
    return DeviceId(selected.Get()) == id ? S_OK : E_FAIL;
}

} // namespace MusicBar
