#include "net/Tone3000Api.h"

#include "json.hpp"

#include <cctype>
#include <cstdio>

namespace nam {

namespace {

constexpr char kBaseUrl[] = "https://www.tone3000.com/api/v1";

bool isUnreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

// True if `s` looks like an A2 architecture_version: either the "a2..."
// prefix (case-insensitive) used by some responses, or a bare "2..." prefix
// (the numeric-string form the API actually returns, e.g. "2").
bool looksLikeA2(const std::string& s) {
    if (s.empty())
        return false;
    if (s[0] == '2')
        return true;
    if (s.size() < 2)
        return false;
    return std::tolower(static_cast<unsigned char>(s[0])) == 'a' &&
           s[1] == '2';
}

// Reads `key` from `j` as a string regardless of the underlying JSON type.
// The real TONE3000 API mixes types across fields (e.g. `id` is a JSON
// integer, not a string) so callers must not assume a field's type from its
// name. Returns "" if the key is missing or null.
std::string asString(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null())
        return {};
    const auto& v = j[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_boolean())
        return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    if (v.is_number_unsigned())
        return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float())
        return std::to_string(v.get<double>());
    return {};
}

// Reads `key` from `j` as a long long, tolerating both numeric and
// stringified-numeric representations (the real API sends `size` as either
// null, a number, or a string like "standard" depending on model
// architecture). Returns 0 if missing, null, or unparsable.
long long asLong(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null())
        return 0;
    const auto& v = j[key];
    if (v.is_number()) {
        try {
            return v.get<long long>();
        } catch (...) {
            return 0;
        }
    }
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

} // namespace

std::string urlEncode(const std::string& s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isUnreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string buildAuthorizeUrl(const std::string& publishableKey,
                               const std::string& redirectUri,
                               const std::string& challenge,
                               const std::string& state,
                               const std::string& prompt) {
    std::string url = std::string(kBaseUrl) + "/oauth/authorize?";
    url += "client_id=" + urlEncode(publishableKey);
    url += "&redirect_uri=" + urlEncode(redirectUri);
    url += "&response_type=" + urlEncode("code");
    url += "&code_challenge=" + urlEncode(challenge);
    url += "&code_challenge_method=" + urlEncode("S256");
    url += "&state=" + urlEncode(state);
    if (!prompt.empty())
        url += "&prompt=" + urlEncode(prompt);
    return url;
}

std::string buildTokenFormBody(const std::string& publishableKey,
                                const std::string& redirectUri,
                                const std::string& code,
                                const std::string& codeVerifier) {
    std::string body;
    body += "grant_type=" + urlEncode("authorization_code");
    body += "&code=" + urlEncode(code);
    body += "&code_verifier=" + urlEncode(codeVerifier);
    body += "&redirect_uri=" + urlEncode(redirectUri);
    body += "&client_id=" + urlEncode(publishableKey);
    return body;
}

std::string buildRefreshFormBody(const std::string& publishableKey,
                                  const std::string& refreshToken) {
    std::string body;
    body += "grant_type=" + urlEncode("refresh_token");
    body += "&refresh_token=" + urlEncode(refreshToken);
    body += "&client_id=" + urlEncode(publishableKey);
    return body;
}

std::string modelsUrl(const std::string& toneId, const std::string& architecture) {
    std::string url = std::string(kBaseUrl) + "/models?tone_id=" + urlEncode(toneId);
    // architecture is an optional filter; omitting it returns the tone's
    // full file list (all NAM architectures, IR file sets).
    if (!architecture.empty())
        url += "&architecture=" + urlEncode(architecture);
    return url + "&page_size=50";
}

TokenResponse parseTokenResponse(const std::string& json) {
    TokenResponse result;
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("access_token"))
            return result;
        result.accessToken = j.value("access_token", std::string());
        result.refreshToken = j.value("refresh_token", std::string());
        result.tokenType = j.value("token_type", std::string());
        result.expiresIn = j.value("expires_in", static_cast<long long>(0));
        result.ok = true;
    } catch (...) {
        // Malformed JSON: fall through with ok=false.
    }
    return result;
}

std::vector<ModelInfo> parseModelList(const std::string& json) {
    std::vector<ModelInfo> models;
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("data") || !j["data"].is_array())
            return models;
        for (const auto& entry : j["data"]) {
            // Each entry is parsed independently: a single malformed/odd
            // entry must not drop the rest of an otherwise-usable list.
            try {
                ModelInfo m;
                m.id = asString(entry, "id");
                m.name = asString(entry, "name");
                m.modelUrl = asString(entry, "model_url");
                m.architectureVersion = asString(entry, "architecture_version");
                m.size = asLong(entry, "size");
                models.push_back(std::move(m));
            } catch (...) {
                // Skip this entry; keep whatever else parsed successfully.
            }
        }
    } catch (...) {
        return {};
    }
    return models;
}

bool pickBestModel(const std::vector<ModelInfo>& models, ModelInfo& out) {
    if (models.empty())
        return false;
    for (const auto& m : models) {
        if (looksLikeA2(m.architectureVersion)) {
            out = m;
            return true;
        }
    }
    out = models[0];
    return true;
}

std::string buildSearchUrl(const std::string& query, int page, int pageSize, bool namOnly) {
    std::string url = std::string(kBaseUrl) + "/tones/search?";
    if (!query.empty())
        url += "query=" + urlEncode(query) + "&";
    url += "page=" + std::to_string(page);
    url += "&page_size=" + std::to_string(pageSize);
    if (namOnly)
        url += "&format=nam";
    return url;
}

std::string buildTrendingUrl() {
    return std::string(kBaseUrl) + "/tones/trending";
}

std::vector<ToneInfo> parseToneList(const std::string& json) {
    std::vector<ToneInfo> tones;
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("data") || !j["data"].is_array())
            return tones;
        for (const auto& entry : j["data"]) {
            // Each entry is parsed independently: a single malformed/odd
            // entry must not drop the rest of an otherwise-usable list.
            try {
                ToneInfo t;
                t.id = asString(entry, "id");
                t.title = asString(entry, "title");
                t.format = asString(entry, "format");
                t.gear = asString(entry, "gear");
                t.a2Count = asLong(entry, "a2_models_count");
                t.a1Count = asLong(entry, "a1_models_count");
                t.downloads = asLong(entry, "downloads_count");
                if (entry.contains("images") && entry["images"].is_array())
                    for (const auto& img : entry["images"])
                        if (img.is_string()) { t.imageUrl = img.get<std::string>(); break; }
                tones.push_back(std::move(t));
            } catch (...) {
                // Skip this entry; keep whatever else parsed successfully.
            }
        }
    } catch (...) {
        return {};
    }
    return tones;
}

} // namespace nam
