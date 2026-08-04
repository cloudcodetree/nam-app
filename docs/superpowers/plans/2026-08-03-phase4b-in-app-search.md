# NAM Player — Phase 4b: In-App TONE3000 Search Browser

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Search and browse TONE3000's catalog *inside* the app (no browser hop for discovery), see results, and download a chosen tone's A2 model into the library — reusing the Phase 4a auth + download core.

**Architecture:** Add JUCE-free, tested search URL-builders + a tone-list parser to `Tone3000Api`; add a token-only "Connect" authenticate flow to `Tone3000Auth` (standard OAuth PKCE, no `select_tone` prompt); add a session method to fetch search results; and a JUCE `SearchPanel` (query box + results list) in `MainComponent`. Downloading a result reuses `Tone3000Session::downloadToneModel` (Phase 4a).

**Tech Stack:** C++17, JUCE 8.0.15, nlohmann::json, Catch2 v3.

## Global Constraints

- C++17. `Source/net` (Api additions) MUST NOT include JUCE. `Source/app` only for UI/transport.
- Bearer token only ever sent to `tone3000.com` hosts (reuse existing `isTone3000Host`); never logged.
- All new parsers are type-tolerant (real API uses integer `id`, mixed `size` types) and NEVER throw — reuse the `asString`/`asLong` helper pattern from the Phase 4a parser fix.
- Every async UI lambda `juce::Component::SafePointer<MainComponent>`-guarded.
- Publishable key only (build-injected); client secret never used.

## Verified TONE3000 API facts (from Phase 4a live debugging)

- Search: `GET /api/v1/tones/search?query=<q>&page=<n>&page_size=<n>&format=nam` (Bearer) → `{data:[Tone], page, page_size, total, total_pages}`.
- Trending/latest: `GET /api/v1/tones/trending`, `/api/v1/tones/latest` → `{data:[Tone]}`.
- Tone fields: `id` (INTEGER), `title` (string), `format` ("nam"/"ir"), `a2_models_count` (int), `a1_models_count` (int), `downloads_count`, `favorites_count`, `gear` (string), `user`, `images`, `tags`, `url`.
- Download path (Phase 4a): `GET /api/v1/models?tone_id=<id>&architecture=2&page_size=50` → models with `model_url`; `model_url` is a tone3000.com URL that 302s to storage.
- Auth: standard PKCE authorize WITHOUT `prompt=select_tone` returns a token (no `tone_id`) — used to make search calls.

## File Structure

```
Source/net/Tone3000Api.h/.cpp    # + ToneInfo, buildSearchUrl, buildTrendingUrl, parseToneList
Source/app/Tone3000Auth.h/.cpp   # + beginConnectFlow (authenticate, no select_tone)
Source/app/Tone3000Session.h/.cpp# + fetchSearch(query,page,done) returning tones (or a small SearchClient)
Source/app/SearchPanel.h/.cpp    # query box + results ListBox
Source/app/MainComponent.h/.cpp  # embed SearchPanel; Connect button; wire result -> download
tests/test_tone3000api.cpp       # + buildSearchUrl + parseToneList tests (real shapes)
```

---

### Task 1: `Tone3000Api` search URL builders + `parseToneList` (JUCE-free, tested)

**Files:** modify `Source/net/Tone3000Api.h/.cpp`, `tests/test_tone3000api.cpp`.

**Interfaces:**
```cpp
namespace nam {
struct ToneInfo {
    std::string id;      // stringified integer
    std::string title;
    std::string format;  // "nam" | "ir"
    std::string gear;
    long long   a2Count = 0;
    long long   a1Count = 0;
    long long   downloads = 0;
};
// query may be empty (server returns unfiltered/trending-like). namOnly adds &format=nam.
std::string buildSearchUrl(const std::string& query, int page, int pageSize, bool namOnly);
std::string buildTrendingUrl();
std::vector<ToneInfo> parseToneList(const std::string& json); // reads data[]; type-tolerant; never throws
}
```

- [ ] **Step 1: Tests** (use the REAL shapes — integer id, `a2_models_count`, `format`):
```cpp
TEST_CASE("buildSearchUrl encodes query + paging + nam filter") {
    auto u = nam::buildSearchUrl("vox ac30", 2, 25, true);
    REQUIRE(u.find("/api/v1/tones/search?") != std::string::npos);
    REQUIRE(u.find("query=vox%20ac30") != std::string::npos);
    REQUIRE(u.find("page=2") != std::string::npos);
    REQUIRE(u.find("page_size=25") != std::string::npos);
    REQUIRE(u.find("format=nam") != std::string::npos);
}
TEST_CASE("buildSearchUrl omits format when namOnly=false and query when empty") {
    auto u = nam::buildSearchUrl("", 1, 20, false);
    REQUIRE(u.find("format=") == std::string::npos);
    REQUIRE(u.find("page=1") != std::string::npos);
}
TEST_CASE("parseToneList reads real tone shape (integer id, counts)") {
    const char* j = R"({"data":[
      {"id":75774,"title":"RR AC30 TB","format":"nam","gear":"amp","a2_models_count":3,"a1_models_count":2,"downloads_count":11081},
      {"id":79866,"title":"IR only","format":"ir","a2_models_count":0}
    ],"total":2})";
    auto ts = nam::parseToneList(j);
    REQUIRE(ts.size() == 2);
    REQUIRE(ts[0].id == "75774");
    REQUIRE(ts[0].title == "RR AC30 TB");
    REQUIRE(ts[0].a2Count == 3);
    REQUIRE(ts[1].format == "ir");
}
TEST_CASE("parseToneList tolerates garbage") { REQUIRE(nam::parseToneList("nope").empty()); }
```

- [ ] **Step 2: Implement** in Tone3000Api.cpp reusing the existing `urlEncode` + `asString`/`asLong` helpers (already added in the Phase 4a parser fix). `buildSearchUrl` base `https://www.tone3000.com/api/v1/tones/search?` + encoded `query` (only if non-empty), `page`, `page_size`, and `format=nam` only when `namOnly`. `parseToneList` reads `data[]`, per-entry try/catch, tolerant field reads. Never throw.

- [ ] **Step 3:** run tests + full suite. Commit: `feat: TONE3000 search URL builders + tone-list parser`.

---

### Task 2: `Tone3000Auth::beginConnectFlow` — token-only authenticate (no select_tone)

Search needs a Bearer token but no tone pick. Add a connect flow = the existing PKCE flow with the `prompt` omitted.

**Files:** modify `Source/app/Tone3000Auth.h/.cpp`.

- [ ] **Step 1:** Generalize the internal flow so the `prompt` is a parameter. Keep `beginSelectToneFlow(done)` (prompt="select_tone") and add `beginConnectFlow(std::function<void(Result)> done)` (prompt=""). When prompt is empty, `buildAuthorizeUrl` must omit the `prompt` param — update `buildAuthorizeUrl` (and its test) to skip `prompt` when the argument is empty. The connect Result has `ok` + no `toneId`; on success a token is stored (reuse `storeTokens`). Reuse the same loopback server + token exchange.

- [ ] **Step 2:** Build both app presets (compile gate). No new unit test for the flow itself (network glue); the `buildAuthorizeUrl` empty-prompt change IS unit-tested. Commit: `feat: Tone3000Auth connect flow (authenticate without select_tone)`.

---

### Task 3: `Tone3000Session` (or `Tone3000SearchClient`) — fetch search results

Reuse the authenticated-GET machinery to fetch + parse a tone list.

**Files:** modify `Source/app/Tone3000Session.h/.cpp` (add a static/instance search helper) OR a small new `Tone3000SearchClient`.

- [ ] **Step 1:** Add a method that, given an access token + query + page, does an authenticated GET of `nam::buildSearchUrl(...)` (Bearer, tone3000 host) on a background thread and delivers `std::vector<nam::ToneInfo>` to a `done` callback via `callAsync`. Reuse the existing `authenticatedGet` + cross-host-redirect discipline. Empty query → optionally use `buildTrendingUrl`. Never log the token.

- [ ] **Step 2:** Build gate + full suite. Commit: `feat: TONE3000 authenticated search fetch`.

---

### Task 4: `SearchPanel` UI + MainComponent integration

**Files:** create `Source/app/SearchPanel.h/.cpp`; modify `MainComponent.h/.cpp`, `CMakeLists.txt`.

- [ ] **Step 1:** `SearchPanel` (juce::Component): a `juce::TextEditor` query box + a "Search" button + a `juce::ListBox` of results (each row: title, gear, format, "A2:n"). `std::function<void(const nam::ToneInfo&)> onPick`. A `refresh(std::vector<ToneInfo>)` to populate. Results shown are copies (no dangling), mirroring `LibraryPanel`.

- [ ] **Step 2:** MainComponent: add a **"Connect TONE3000"** button (visible when `!t3kAuth_.hasValidToken()`), and the `SearchPanel`. Connect → `t3kAuth_.beginConnectFlow` (SafePointer-guarded); once a token exists, enable search. Search button → session search fetch → `searchPanel_.refresh(results)`. `onPick(tone)` → reuse the Phase 4a download path: `t3kSession_->downloadToneModel(tone.id, tempDir, ...)` → `importIntoLibrary` → `libraryPanel_.refresh()` + load. All async lambdas SafePointer-guarded; never log tokens. Lay out in `resized()`; grow window if needed.

- [ ] **Step 3:** Both app presets link + full suite passes. Commit: `feat: in-app TONE3000 search panel (search -> download -> library)`.

---

### Task 5: Manual verification + finish

- [ ] Full suite regression. Manual: Connect (or reuse token) → type "AC30" → results list → pick one → downloads into library + loads. Merge to main.

---

## Self-Review

- **Scope:** in-app search/browse + download, reusing 4a auth/download + 3 library. In-app *filters beyond query + nam-only* deferred; trending/latest optional. ✅
- **Reuse:** download path, `authenticatedGet`, `isTone3000Host`, `importIntoLibrary`, type-tolerant parser helpers all reused. ✅
- **Security:** token-only connect flow; Bearer host-scoped; no token logging; publishable key only. ✅
- **Testability:** search URL builders + `parseToneList` + empty-prompt `buildAuthorizeUrl` are JUCE-free unit tests using REAL API shapes. ✅
