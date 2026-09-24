#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace MusicBar {

class Decrypter {
public:
    static std::vector<uint8_t> Base64Decode(std::string_view input);
    static std::string MinizInflate(const uint8_t* compressedData, size_t compressedSize);

    // Kugou KRC decryptor (Header "krc1" + 16-byte XOR + zlib inflate)
    static std::string DecryptKugouKrc(const std::vector<uint8_t>& krcBytes);

    // QQ Music QRC decryptor (Triple-DES + zlib inflate)
    static std::string DecryptQQMusicQrc(const std::vector<uint8_t>& qrcBytes);

    // Character set conversion & URL utilities
    static std::wstring Utf8ToWide(std::string_view utf8Str);
    static std::string WideToUtf8(std::wstring_view wideStr);
    static std::wstring UrlEncode(std::wstring_view str);
};

} // namespace MusicBar
