#pragma once

#include <string>
#include <vector>

namespace nam {

struct TokenResponse {
    std::string accessToken, refreshToken, tokenType;
    long long expiresIn = 0;
    bool ok = false;
};

struct ModelInfo {
    std::string id, name, modelUrl, architectureVersion;
    long long size = 0;
};

struct ToneInfo {
    std::string id;      // stringified integer
    std::string title;
    std::string format;  // "nam" | "ir"
    std::string gear;
    long long   a2Count = 0;
    long long   a1Count = 0;
    long long   downloads = 0;
};

// Percent-encodes every byte except the unreserved set [A-Za-z0-9-._~]
// (RFC 3986 §2.3). Space becomes %20 (not '+'); hex digits are uppercase.
std::string urlEncode(const std::string& s);

// Builds the TONE3000 OAuth authorize URL with PKCE params. `prompt` is a
// TONE3000-specific hint, e.g. "select_tone".
std::string buildAuthorizeUrl(const std::string& publishableKey,
                               const std::string& redirectUri,
                               const std::string& challenge,
                               const std::string& state,
                               const std::string& prompt);

// Builds the application/x-www-form-urlencoded body for the
// authorization_code token exchange.
std::string buildTokenFormBody(const std::string& publishableKey,
                                const std::string& redirectUri,
                                const std::string& code,
                                const std::string& codeVerifier);

// Full /api/v1/models?tone_id=...&architecture=...&page_size=50 URL for a
// given tone id. `architecture` is the TONE3000 API's numeric-string
// architecture selector: "2" for A2 models, "1" for A1 models. Without this
// param the API returns the legacy (A1) model list, which is empty for
// A2-format tones.
std::string modelsUrl(const std::string& toneId, const std::string& architecture);

// Parses a token-endpoint JSON response. Never throws: malformed JSON or a
// missing access_token yields ok=false.
TokenResponse parseTokenResponse(const std::string& json);

// Parses the `data[]` array of a /models response into ModelInfo entries.
// Never throws: malformed JSON yields an empty vector.
std::vector<ModelInfo> parseModelList(const std::string& json);

// Picks the first model whose architectureVersion indicates A2 — either
// starting with "a2" (case-insensitive) or starting with "2" (the raw
// numeric architecture_version the API returns, e.g. "2") — falling back to
// the first model in the list. Returns false (leaving `out` untouched) if
// `models` is empty.
bool pickBestModel(const std::vector<ModelInfo>& models, ModelInfo& out);

// Builds the /api/v1/tones/search URL. `query` is percent-encoded and
// included only when non-empty (the server treats an unfiltered/empty query
// as a trending-like listing). `page` and `page_size` are always included.
// `format=nam` is appended only when `namOnly` is true.
std::string buildSearchUrl(const std::string& query, int page, int pageSize, bool namOnly);

// Full /api/v1/tones/trending URL.
std::string buildTrendingUrl();

// Parses the `data[]` array of a /tones search (or trending) response into
// ToneInfo entries. Each entry is parsed independently: a single
// malformed/odd entry must not drop the rest of an otherwise-usable list.
// Never throws: malformed JSON yields an empty vector.
std::vector<ToneInfo> parseToneList(const std::string& json);

} // namespace nam
