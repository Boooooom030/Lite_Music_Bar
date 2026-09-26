#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>

namespace MusicBar {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::vector<uint8_t> rawBytes;
    bool success = false;
};

class HttpClient {
public:
    explicit HttpClient(int timeoutMs = 5000);
    ~HttpClient();

    HttpResponse Get(
        const std::wstring& url,
        const std::map<std::wstring, std::wstring>& headers = {}
    );

    HttpResponse Post(
        const std::wstring& url,
        const std::string& postData,
        const std::wstring& contentType = L"application/x-www-form-urlencoded",
        const std::map<std::wstring, std::wstring>& headers = {}
    );

private:
    HINTERNET m_hSession = nullptr;

    HttpResponse Request(
        const std::wstring& verb,
        const std::wstring& url,
        const std::string& postData,
        const std::wstring& contentType,
        const std::map<std::wstring, std::wstring>& headers
    );
};

} // namespace MusicBar
