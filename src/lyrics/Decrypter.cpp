#include "Decrypter.hpp"
#include "miniz/miniz.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace MusicBar {

std::vector<uint8_t> Decrypter::Base64Decode(std::string_view input) {
    if (input.empty()) return {};

    DWORD outLen = 0;
    if (!CryptStringToBinaryA(
        input.data(),
        static_cast<DWORD>(input.size()),
        CRYPT_STRING_BASE64,
        nullptr,
        &outLen,
        nullptr,
        nullptr
    )) {
        return {};
    }

    std::vector<uint8_t> result(outLen);
    if (!CryptStringToBinaryA(
        input.data(),
        static_cast<DWORD>(input.size()),
        CRYPT_STRING_BASE64,
        result.data(),
        &outLen,
        nullptr,
        nullptr
    )) {
        return {};
    }

    return result;
}

std::string Decrypter::MinizInflate(const uint8_t* compressedData, size_t compressedSize) {
    if (!compressedData || compressedSize == 0) return "";

    // Start with 64KB initial estimate, expand if needed
    mz_ulong destLen = static_cast<mz_ulong>(std::max(compressedSize * 4, size_t(65536)));
    std::vector<uint8_t> dest(destLen);

    for (int attempt = 0; attempt < 5; ++attempt) {
        mz_ulong currentDestLen = destLen;
        int status = mz_uncompress(
            dest.data(),
            &currentDestLen,
            compressedData,
            static_cast<mz_ulong>(compressedSize)
        );

        if (status == MZ_OK) {
            return std::string(reinterpret_cast<const char*>(dest.data()), currentDestLen);
        } else if (status == MZ_BUF_ERROR) {
            destLen *= 2;
            dest.resize(destLen);
        } else {
            break;
        }
    }

    return "";
}

std::string Decrypter::DecryptKugouKrc(const std::vector<uint8_t>& krcBytes) {
    // Check "krc1" header (4 bytes)
    if (krcBytes.size() < 4) return "";
    if (krcBytes[0] != 'k' || krcBytes[1] != 'r' || krcBytes[2] != 'c' || krcBytes[3] != '1') {
        // Might already be raw XML or LRC
        return std::string(reinterpret_cast<const char*>(krcBytes.data()), krcBytes.size());
    }

    // 16-byte fixed XOR key
    static const uint8_t KRC_KEY[16] = {
        0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
        0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69
    };

    size_t cipherLen = krcBytes.size() - 4;
    std::vector<uint8_t> xorDecrypted(cipherLen);
    for (size_t i = 0; i < cipherLen; ++i) {
        xorDecrypted[i] = krcBytes[i + 4] ^ KRC_KEY[i % 16];
    }

    // Zlib decompress
    return MinizInflate(xorDecrypted.data(), xorDecrypted.size());
}

std::string Decrypter::DecryptQQMusicQrc(const std::vector<uint8_t>& qrcBytes) {
    if (qrcBytes.empty()) return "";

    // Check if plain text
    if (qrcBytes.size() >= 5 && (
        (qrcBytes[0] == '<' && qrcBytes[1] == '?' && qrcBytes[2] == 'x') ||
        (qrcBytes[0] == '[' && (qrcBytes[1] == 't' || qrcBytes[1] == '0' || qrcBytes[1] == '1'))
    )) {
        return std::string(reinterpret_cast<const char*>(qrcBytes.data()), qrcBytes.size());
    }

    // Triple-DES decryption with key "!@#)(*$%123ZXC!@!@#)(NHL"
    static const char KEY_3DES[] = "!@#)(*$%123ZXC!@!@#)(NHL";

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_3DES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return "";

    status = BCryptSetProperty(
        hAlg,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
        sizeof(BCRYPT_CHAIN_MODE_ECB),
        0
    );

    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = BCryptGenerateSymmetricKey(
        hAlg,
        &hKey,
        nullptr,
        0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(KEY_3DES)),
        static_cast<ULONG>(strlen(KEY_3DES)),
        0
    );

    std::string result;
    if (BCRYPT_SUCCESS(status)) {
        std::vector<uint8_t> decrypted(qrcBytes.size());
        ULONG cbResult = 0;
        status = BCryptDecrypt(
            hKey,
            const_cast<PUCHAR>(qrcBytes.data()),
            static_cast<ULONG>(qrcBytes.size()),
            nullptr,
            nullptr,
            0,
            decrypted.data(),
            static_cast<ULONG>(decrypted.size()),
            &cbResult,
            0
        );

        if (BCRYPT_SUCCESS(status)) {
            decrypted.resize(cbResult);
            // Decompress result
            result = MinizInflate(decrypted.data(), decrypted.size());
            if (result.empty()) {
                result = std::string(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
            }
        }
        BCryptDestroyKey(hKey);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

std::wstring Decrypter::Utf8ToWide(std::string_view utf8Str) {
    if (utf8Str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), wide.data(), count);
    return wide;
}

std::string Decrypter::WideToUtf8(std::wstring_view wideStr) {
    if (wideStr.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return "";
    std::string utf8(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), static_cast<int>(wideStr.size()), utf8.data(), count, nullptr, nullptr);
    return utf8;
}

std::wstring Decrypter::UrlEncode(std::wstring_view str) {
    std::string utf8 = WideToUtf8(str);
    std::wostringstream escaped;
    escaped.fill(L'0');
    escaped << std::hex;

    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << static_cast<wchar_t>(c);
        } else {
            escaped << L'%' << std::setw(2) << std::uppercase << static_cast<int>(c);
        }
    }

    return escaped.str();
}

} // namespace MusicBar
