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

std::string modelsUrl(const std::string& toneId, const std::string& architecture) {
    return std::string(kBaseUrl) + "/models?tone_id=" + urlEncode(toneId) +
           "&architecture=" + urlEncode(architecture) + "&page_size=50";
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
            ModelInfo m;
            m.id = entry.value("id", std::string());
            m.name = entry.value("name", std::string());
            m.modelUrl = entry.value("model_url", std::string());
            m.architectureVersion = entry.value("architecture_version", std::string());
            m.size = entry.value("size", static_cast<long long>(0));
            models.push_back(std::move(m));
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

} // namespace nam
