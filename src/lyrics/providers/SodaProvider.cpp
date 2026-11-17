#include "SodaProvider.hpp"
#include "lyrics/Decrypter.hpp"
#include "lyrics/LyricsParser.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <random>
#include <string_view>
#include <vector>

namespace MusicBar {
namespace {

using json = nlohmann::json;

// 汽水音乐 guest access. The car-SDK open platform answers the track search
// with a client token and no account cookies; the H5 track page then carries
// the lyric body (KRC syllables or LRC) together with its translation.
constexpr wchar_t TokenUrl[] =
    L"https://open.qishui.com/api/luna/v1/platform/client/token/get/";
constexpr wchar_t SearchUrl[] =
    L"https://open.douyin.com/api/luna/v1/platform/search/";
constexpr wchar_t DetailUrl[] =
    L"https://beta-luna.douyin.com/luna/h5/seo_track";

constexpr wchar_t SdkUserAgent[] = L"LunaOpenSDK/1.0.10";
constexpr wchar_t WebUserAgent[] =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

constexpr size_t MaxResponseBytes = 8 * 1024 * 1024;
// Duration only breaks ties and gates the unknown-artist fallback: a cover can
// share the exact length of the original and still be an unrelated song, so it
// must never be what makes a hit acceptable on its own.
constexpr int64_t DurationMatchSlackMs = 3000;

std::mutex g_tokenMutex;
std::wstring g_clientToken;

struct Candidate {
    std::wstring id;
    std::wstring name;
    std::vector<std::wstring> artists;
    int64_t durationMs = 0;
};

std::wstring DeviceId() {
    static const std::wstring id = [] {
        std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> digit(0, 9);
        std::wstring value = L"7";
        for (int i = 0; i < 18; ++i) {
            value.push_back(static_cast<wchar_t>(L'0' + digit(generator)));
        }
        return value;
    }();
    return id;
}

std::wstring Guid() {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> hex(0, 15);
    std::wstring value;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) value.push_back(L'-');
        value.push_back(L"0123456789abcdef"[hex(generator)]);
    }
    return value;
}

// Car-SDK common parameters: the endpoint only needs them present and
// consistent, and the device ids stay fixed per process so repeated lookups
// look like one installation.
std::wstring PlatformQuery() {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int64_t> nonce(1, 999999999);
    const std::wstring device = DeviceId();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    struct Field {
        const wchar_t* key;
        std::wstring value;
    };
    const Field fields[] = {
        {L"aid", L"765213"},
        {L"app_id", L"765213"},
        {L"app_name", L"byte_autoservice_lite"},
        {L"version_code", L"100010"},
        {L"version_name", L"1.0.10"},
        {L"channel", L"bydSong"},
        {L"sdk_aid", L"753999"},
        {L"caller_name", L"luna_open_sdk"},
        {L"sdk_version_code", L"100000100"},
        {L"sdk_version_name", L"0.0.1"},
        {L"sdk_channel", L"Test"},
        {L"vehicle_type", L"BYD"},
        {L"device_brand", L"BYD"},
        {L"device_type", L"BYD"},
        {L"device_platform", L"android"},
        {L"resolution", L"1920x1080"},
        {L"os_version", L"13"},
        {L"ac", L"wifi"},
        {L"device_id", device},
        {L"did", device},
        {L"iid", device},
        {L"vehicle_id", L"qishui-" + device},
        {L"_timestamp", std::to_wstring(seconds)},
        {L"_nonce", std::to_wstring(nonce(generator))},
    };

    std::wstring query;
    for (const auto& field : fields) {
        if (!query.empty()) query.push_back(L'&');
        query += field.key;
        query.push_back(L'=');
        query += Decrypter::UrlEncode(field.value);
    }
    return query;
}

std::wstring ClientToken(HttpClient& http, bool refresh) {
    if (!refresh) {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        if (!g_clientToken.empty()) return g_clientToken;
    }
    auto response = http.Get(std::wstring(TokenUrl) + L"?" + PlatformQuery() +
            L"&trigger_type=cold_start",
        {{L"User-Agent", SdkUserAgent}, {L"Accept", L"application/json"}});
    if (!response.success || response.body.empty()) return {};
    auto parsed = json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return {};
    if (parsed.value("err_no", -1) != 0) return {};
    const auto token = parsed.value("client_token", std::string{});
    if (token.empty()) return {};
    std::wstring wide = Decrypter::Utf8ToWide(token);
    {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        g_clientToken = wide;
    }
    return wide;
}

// Lowercase alphanumerics only, cut at the first bracket: catalog titles carry
// suffixes the playing track never has ("(Explicit)", "（原唱）").
std::wstring Normalize(std::wstring_view text) {
    std::wstring normalized;
    for (const wchar_t ch : text) {
        if (ch == L'(' || ch == L'（' || ch == L'[' || ch == L'【') break;
        if (std::iswalnum(ch)) normalized.push_back(std::towlower(ch));
    }
    return normalized;
}

std::vector<std::wstring> ArtistTokens(std::wstring_view artist) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    auto flush = [&] {
        auto normalized = Normalize(current);
        if (!normalized.empty()) tokens.push_back(std::move(normalized));
        current.clear();
    };
    for (const wchar_t ch : artist) {
        if (ch == L'/' || ch == L'&' || ch == L',' || ch == L'、' || ch == L';') flush();
        else current.push_back(ch);
    }
    flush();
    return tokens;
}

// Rank of a search hit, or -1 when it is another song. The artist must match:
// 汽水 is full of uploads that reuse a well-known title for an unrelated track,
// and a cover legitimately named after the original ("稻香 (杰伦版)") carries
// that unrelated lyric too, so title and length alone cannot be trusted.
// Only an unknown artist falls back to an exact title with the same length.
int RankCandidate(const std::wstring& title, const std::vector<std::wstring>& artists,
                  int64_t durationMs, const Candidate& candidate) {
    const std::wstring candidateTitle = Normalize(candidate.name);
    if (candidateTitle.empty() || title.empty()) return -1;

    int score = 0;
    if (candidateTitle == title) {
        score = 100;
    } else if (candidateTitle.size() >= 2 && title.size() >= 2 &&
        (candidateTitle.find(title) != std::wstring::npos ||
         title.find(candidateTitle) != std::wstring::npos)) {
        score = 50;
    } else {
        return -1;
    }

    int64_t difference = -1;
    if (durationMs > 0 && candidate.durationMs > 0) {
        difference = std::abs(durationMs - candidate.durationMs);
    }

    bool artistMatched = false;
    for (const auto& token : artists) {
        for (const auto& name : candidate.artists) {
            if (name.empty()) continue;
            if (name == token) {
                artistMatched = true;
                score += 40;
                break;
            }
            if (name.find(token) != std::wstring::npos ||
                token.find(name) != std::wstring::npos) {
                artistMatched = true;
                score += 20;
                break;
            }
        }
        if (artistMatched) break;
    }

    if (artistMatched) {
        if (difference <= 1000) score += 30;
        else if (difference > DurationMatchSlackMs) score -= 40;
        return score;
    }

    if (!artists.empty()) return -1;
    if (score < 100 || difference < 0 || difference > DurationMatchSlackMs) return -1;
    return score + 20;
}

std::optional<json> Search(HttpClient& http, const std::wstring& keyword) {
    json body{
        {"q", Decrypter::WideToUtf8(keyword)},
        {"cursor", "0"},
        {"search_method", "input"},
        {"search_type", "track"},
        {"search_scene", "normal"},
        {"scene_name", "search"},
        {"ab_param", ""},
        {"last_viewed_media", json::object()},
        {"common_params", json::object()},
    };
    const std::string searchId = Decrypter::WideToUtf8(Guid());
    body["search_id"] = searchId;
    body["from_search_id"] = searchId;
    const std::string payload = body.dump();

    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::wstring token = ClientToken(http, attempt > 0);
        if (token.empty()) return std::nullopt;
        auto response = http.Post(std::wstring(SearchUrl) + L"?" + PlatformQuery(),
            payload,
            L"application/json",
            {{L"User-Agent", SdkUserAgent},
             {L"Accept", L"application/json"},
             {L"access-token", token}});
        // An expired token answers 200 with an empty body, so the second
        // attempt always re-fetches the token.
        if (!response.success || response.body.empty() ||
            response.body.size() > MaxResponseBytes) continue;
        auto parsed = json::parse(response.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) continue;
        if (parsed.value("err_no", 0) != 0) continue;
        const auto data = parsed.find("data");
        if (data == parsed.end() || !data->is_object()) continue;
        return *data;
    }
    return std::nullopt;
}

std::vector<Candidate> ReadCandidates(const json& data) {
    std::vector<Candidate> candidates;
    const auto groups = data.find("result_groups");
    if (groups == data.end() || !groups->is_array()) return candidates;
    for (const auto& group : *groups) {
        if (!group.is_object()) continue;
        const auto items = group.find("data");
        if (items == group.end() || !items->is_array()) continue;
        for (const auto& item : *items) {
            if (!item.is_object()) continue;
            const auto entity = item.find("entity");
            if (entity == item.end() || !entity->is_object()) continue;
            const auto track = entity->find("track");
            if (track == entity->end() || !track->is_object()) continue;
            const auto base = track->find("base_info");
            if (base == track->end() || !base->is_object()) continue;
            Candidate candidate;
            candidate.id = Decrypter::Utf8ToWide(base->value("id", std::string{}));
            candidate.name = Decrypter::Utf8ToWide(base->value("name", std::string{}));
            candidate.durationMs = base->value("duration_ms", static_cast<int64_t>(0));
            if (candidate.id.empty() || candidate.name.empty()) continue;
            const auto related = track->find("related_info");
            if (related != track->end() && related->is_object()) {
                const auto links = related->find("artist_links");
                if (links != related->end() && links->is_array()) {
                    for (const auto& link : *links) {
                        if (!link.is_object()) continue;
                        auto name = Normalize(Decrypter::Utf8ToWide(
                            link.value("name", std::string{})));
                        if (!name.empty()) candidate.artists.push_back(std::move(name));
                    }
                }
            }
            candidates.push_back(std::move(candidate));
        }
    }
    return candidates;
}

std::string ChineseTranslation(const json& lyric) {
    auto translations = lyric.find("translations");
    if (translations != lyric.end() && translations->is_object()) {
        const auto chinese = translations->find("cn");
        if (chinese != translations->end() && chinese->is_string()) {
            return chinese->get<std::string>();
        }
    }
    auto languages = lyric.find("lang_translations");
    if (languages != lyric.end() && languages->is_object()) {
        const auto zh = languages->find("zh");
        if (zh != languages->end() && zh->is_object()) {
            return zh->value("content", std::string{});
        }
    }
    return {};
}

} // namespace

std::optional<LyricDocument> SodaProvider::FetchLyrics(
    const std::wstring& title,
    const std::wstring& artist,
    int64_t durationMs
) {
    if (title.empty()) return std::nullopt;

    std::wstring keyword = title;
    if (!artist.empty()) keyword += L" " + artist;

    const auto data = Search(m_http, keyword);
    if (!data) return std::nullopt;
    const auto candidates = ReadCandidates(*data);
    if (candidates.empty()) return std::nullopt;

    const std::wstring normalizedTitle = Normalize(title);
    const auto artists = ArtistTokens(artist);
    const Candidate* best = nullptr;
    int bestRank = -1;
    for (const auto& candidate : candidates) {
        const int rank = RankCandidate(normalizedTitle, artists, durationMs, candidate);
        if (rank > bestRank) {
            bestRank = rank;
            best = &candidate;
        }
    }
    if (!best) return std::nullopt;

    auto response = m_http.Get(std::wstring(DetailUrl) +
            L"?device_platform=web&track_id=" + Decrypter::UrlEncode(best->id),
        {{L"User-Agent", WebUserAgent}, {L"Accept", L"application/json"}});
    if (!response.success || response.body.empty() ||
        response.body.size() > MaxResponseBytes) return std::nullopt;
    auto detail = json::parse(response.body, nullptr, false);
    if (detail.is_discarded() || !detail.is_object()) return std::nullopt;

    const auto lyric = detail.find("lyric");
    if (lyric == detail.end() || !lyric->is_object()) return std::nullopt;
    const std::string content = lyric->value("content", std::string{});
    if (content.empty()) return std::nullopt;

    // The page mixes KRC syllable lines and plain LRC, and the shared parser
    // reads both, so the reported type is not needed here.
    auto document = LyricsParser::Parse(content, ChineseTranslation(*lyric));
    if (document.lines.empty()) return std::nullopt;
    document.title = title;
    document.artist = artist;
    return document;
}

} // namespace MusicBar

