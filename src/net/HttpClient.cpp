#include "HttpClient.hpp"
#include <sstream>

namespace MusicBar {

HttpClient::HttpClient(int timeoutMs) {
    m_hSession = WinHttpOpen(
        L"MusicBar/1.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (m_hSession) {
        // Enable TLS 1.2 and TLS 1.3
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        // Lyrics use the default timeout; small cover requests can fail sooner.
        WinHttpSetTimeouts(m_hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    }
}

HttpClient::~HttpClient() {
    if (m_hSession) {
        WinHttpCloseHandle(m_hSession);
        m_hSession = nullptr;
    }
}

HttpResponse HttpClient::Get(
    const std::wstring& url,
    const std::map<std::wstring, std::wstring>& headers
) {
    return Request(L"GET", url, "", L"", headers);
}

HttpResponse HttpClient::Post(
    const std::wstring& url,
    const std::string& postData,
    const std::wstring& contentType,
    const std::map<std::wstring, std::wstring>& headers
) {
    return Request(L"POST", url, postData, contentType, headers);
}

HttpResponse HttpClient::Request(
    const std::wstring& verb,
    const std::wstring& url,
    const std::string& postData,
    const std::wstring& contentType,
    const std::map<std::wstring, std::wstring>& headers
) {
    HttpResponse response{};
    if (!m_hSession) return response;

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = static_cast<DWORD>(-1);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.length()), 0, &urlComp)) {
        return response;
    }

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        urlPath.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    HINTERNET hConnect = WinHttpConnect(m_hSession, hostName.c_str(), port, 0);
    if (!hConnect) return response;

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        verb.c_str(),
        urlPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return response;
    }

    // Add content type if specified
    if (!contentType.empty()) {
        std::wstring ctHeader = L"Content-Type: " + contentType + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, ctHeader.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Add custom headers
    for (const auto& [key, value] : headers) {
        std::wstring headerLine = key + L": " + value + L"\r\n";
        WinHttpAddRequestHeaders(hRequest, headerLine.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }

    // Send request
    LPVOID pData = postData.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(postData.data());
    DWORD dataLen = static_cast<DWORD>(postData.size());

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        pData,
        dataLen,
        dataLen,
        0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (bResults) {
        // Query status code
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &size,
            WINHTTP_NO_HEADER_INDEX
        );
        response.statusCode = static_cast<int>(statusCode);

        // Read response body
        DWORD dwSize = 0;
        bool readComplete = true;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                readComplete = false;
                break;
            }

            if (dwSize > 0) {
                std::vector<char> buffer(dwSize);
                DWORD dwDownloaded = 0;
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded) ||
                    dwDownloaded == 0) {
                    readComplete = false;
                    break;
                }
                response.rawBytes.insert(response.rawBytes.end(),
                    buffer.begin(), buffer.begin() + dwDownloaded);
            }
        } while (dwSize > 0);

        if (!response.rawBytes.empty()) {
            response.body.assign(reinterpret_cast<const char*>(response.rawBytes.data()),
                response.rawBytes.size());
        }
        response.success = readComplete && response.statusCode >= 200 &&
            response.statusCode < 300;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return response;
}

} // namespace MusicBar
