# NAM Player — Phase 4a: TONE3000 Download (OAuth2 PKCE + select_tone)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user tap "Browse TONE3000," pick a tone on TONE3000's own site (logged in via OAuth), and have the app download that tone's A2 model straight into the local library and load it. No in-app search UI yet (Phase 4b).

**Architecture:** A JUCE-free, unit-tested core (`Pkce` + `Tone3000Api` request-builders/response-parsers) validated against RFC 7636 vectors and sample JSON, plus a JUCE `Tone3000Auth` (loopback OAuth2-PKCE flow: browser + local redirect server + token exchange + token storage) and `Tone3000Session` (authenticated GETs + model download). MainComponent gets a "Browse TONE3000" button that runs the flow and imports the result via Phase 3's `importIntoLibrary`.

**Tech Stack:** C++17, JUCE 8.0.15 (HTTP, browser, sockets), nlohmann::json (from NeuralAudio deps), picosha2 (vendored MIT SHA-256 for PKCE), Catch2 v3.

## Global Constraints

- **Language:** C++17.
- **JUCE isolation:** `Source/net/` (the testable auth/API core) MUST NOT include JUCE. Only `Source/app/` may use JUCE for transport/UI.
- **Not on the audio thread:** all networking is control/UI-thread and background-thread work; it never touches the audio callback. Downloaded files reach the engine only through the existing `importIntoLibrary` + `engine_.setModel` paths.
- **Security — publishable key vs secret:** the app uses ONLY the publishable key (`t3k_pub_…`) as the OAuth `client_id`. The client secret (`t3k_cs_…`) MUST NOT be read, referenced, compiled in, or logged anywhere. PKCE (S256) is mandatory — a public client with no secret.
- **PKCE correctness:** `code_verifier` is 43–128 chars from the unreserved set `[A-Za-z0-9-._~]`; `code_challenge = base64url(SHA256(verifier))` with no padding, `code_challenge_method=S256`. Validated against RFC 7636 Appendix B test vector.
- **Token storage:** persist `access_token`/`refresh_token`/`expiry` in a file under the app data dir with `0600` permissions. (Keychain/Keystore hardening is a tracked follow-up — noted, not in this phase.) Never log token values.
- **Redirect:** loopback `http://127.0.0.1:<port>/callback` (RFC 8252). The exact `redirect_uri` must be allowed/registered for the publishable key on TONE3000 — see Dependencies.
- **Rate limit:** 100 req/min; the app makes only a handful of calls per download, so no throttling logic needed, but never poll in a tight loop.
- **nlohmann:** reuse NeuralAudio's copy (include dir already wired in Phase 3); `#include "json.hpp"`.
- **No secrets in logs or errors:** redact tokens/codes in any diagnostic output.

## TONE3000 API reference (verified)

- Authorize: `GET https://www.tone3000.com/api/v1/oauth/authorize?client_id=<pub>&redirect_uri=<loopback>&response_type=code&code_challenge=<c>&code_challenge_method=S256&state=<s>&prompt=select_tone`
- Callback (to redirect_uri): query params `code`, `state`, `tone_id` (present because prompt=select_tone), or `error`.
- Token: `POST https://www.tone3000.com/api/v1/oauth/token` (form-urlencoded): `grant_type=authorization_code&code=<c>&code_verifier=<v>&redirect_uri=<loopback>&client_id=<pub>` → JSON `{access_token, refresh_token, expires_in, token_type:"bearer", scope}`. Refresh: `grant_type=refresh_token&refresh_token=<r>&client_id=<pub>`.
- List models: `GET https://www.tone3000.com/api/v1/models?tone_id=<id>&architecture=<opt>` with `Authorization: Bearer <access_token>` → `{data:[{id,name,size,architecture_version,tone_id,model_url,created_at}], page,page_size,total,total_pages}`.
- Download model: `GET <model_url>` with `Authorization: Bearer <access_token>` → raw `.nam` bytes.

## File Structure

```
extern/picosha2/picosha2.h        # vendored MIT SHA-256 (single header)
Source/net/
  Pkce.h  .cpp                    # verifier/challenge/state + base64url (JUCE-free)
  Tone3000Api.h  .cpp             # URL/body builders + JSON response parsers (JUCE-free)
Source/app/
  Tone3000Auth.h  .cpp            # JUCE: PKCE flow (browser + loopback server + token exchange + storage)
  Tone3000Session.h  .cpp         # JUCE: authenticated GET + model download bytes
  MainComponent.h/.cpp            # MODIFIED: "Browse TONE3000" button -> flow -> importIntoLibrary
tests/
  test_pkce.cpp  test_tone3000api.cpp
  CMakeLists.txt                  # MODIFIED
CMakeLists.txt                    # MODIFIED: read .env -> TONE3000_PUBLISHABLE_KEY compile def
```

---

### Task 1: Vendor picosha2 + `Pkce` (JUCE-free, testable)

PKCE primitives: random `code_verifier`, S256 `code_challenge`, random `state`, and base64url. Correctness pinned to the RFC 7636 test vector.

**Files:**
- Create: `extern/picosha2/picosha2.h` (download — Step 1)
- Create: `Source/net/Pkce.h`
- Create: `Source/net/Pkce.cpp`
- Create: `tests/test_pkce.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace nam {
  struct PkcePair { std::string verifier; std::string challenge; };
  std::string base64UrlNoPad(const unsigned char* data, size_t len);
  std::string sha256Challenge(const std::string& verifier);   // base64url(SHA256(verifier))
  PkcePair    generatePkce();     // random 64-char verifier + its S256 challenge
  std::string randomUrlToken(int nChars);   // for `state`; unreserved charset
  }
  ```

- [ ] **Step 1: Vendor picosha2 (MIT, header-only SHA-256)**

```bash
mkdir -p extern/picosha2
curl -fsSL -o extern/picosha2/picosha2.h \
  https://raw.githubusercontent.com/okdshin/PicoSHA2/master/picosha2.h
```

- [ ] **Step 2: Write Pkce.h** (the interface above; `#include <string>`).

- [ ] **Step 3: Write the failing tests (RFC 7636 Appendix B vector)**

```cpp
// tests/test_pkce.cpp
#include <catch2/catch_all.hpp>
#include <set>
#include "net/Pkce.h"
using namespace nam;

TEST_CASE("sha256Challenge matches RFC 7636 Appendix B vector") {
    // verifier and expected challenge straight from the RFC.
    const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    const std::string expected = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
    REQUIRE(sha256Challenge(verifier) == expected);
}

TEST_CASE("base64UrlNoPad uses URL-safe alphabet without padding") {
    const unsigned char in[] = { 0xFB, 0xFF, 0xBF };   // would be +/ and = in std base64
    auto s = base64UrlNoPad(in, 3);
    REQUIRE(s.find('+') == std::string::npos);
    REQUIRE(s.find('/') == std::string::npos);
    REQUIRE(s.find('=') == std::string::npos);
}

TEST_CASE("generatePkce yields a spec-legal verifier and matching challenge") {
    auto p = generatePkce();
    REQUIRE(p.verifier.size() >= 43);
    REQUIRE(p.verifier.size() <= 128);
    const std::string allowed =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    for (char c : p.verifier) REQUIRE(allowed.find(c) != std::string::npos);
    REQUIRE(p.challenge == sha256Challenge(p.verifier));
}

TEST_CASE("randomUrlToken is the requested length and url-safe") {
    auto t = randomUrlToken(32);
    REQUIRE(t.size() == 32);
}
```

- [ ] **Step 4: Write Pkce.cpp**

- `#include "picosha2.h"`, `<random>`, `<array>`.
- `base64UrlNoPad`: standard base64 with alphabet `A-Za-z0-9-_`, no `=` padding.
- `sha256Challenge`: `std::vector<unsigned char> hash(32); picosha2::hash256(verifier.begin(), verifier.end(), hash.begin(), hash.end()); return base64UrlNoPad(hash.data(), 32);`
- `generatePkce`: build a 64-char verifier by drawing from the unreserved charset using `std::mt19937` seeded from `std::random_device`; challenge = `sha256Challenge(verifier)`.
- `randomUrlToken(n)`: n chars from the same charset.

- [ ] **Step 5: Wire CMake, run tests (fail → pass)**

Append `test_pkce.cpp` and `${CMAKE_SOURCE_DIR}/Source/net/Pkce.cpp` to `nam_tests`; add include dir `${CMAKE_SOURCE_DIR}/extern/picosha2`. Run `cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests "*RFC 7636*,*base64Url*,*generatePkce*,*randomUrlToken*"` then full suite.

- [ ] **Step 6: Commit**

```bash
git add extern/picosha2/picosha2.h Source/net/Pkce.h Source/net/Pkce.cpp tests/test_pkce.cpp tests/CMakeLists.txt
git commit -m "feat: PKCE primitives (S256 challenge, base64url) vetted against RFC 7636"
```

---

### Task 2: `Tone3000Api` — URL/body builders + JSON parsers (JUCE-free, testable)

All the pure string/JSON logic of the TONE3000 client: build the authorize URL and token form body, and parse the token, model-list, and error responses. No HTTP here (that's Task 3).

**Files:**
- Create: `Source/net/Tone3000Api.h`
- Create: `Source/net/Tone3000Api.cpp`
- Create: `tests/test_tone3000api.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nlohmann (`"json.hpp"`).
- Produces:
  ```cpp
  namespace nam {
  struct TokenResponse { std::string accessToken, refreshToken, tokenType; long long expiresIn = 0; bool ok = false; };
  struct ModelInfo { std::string id, name, modelUrl, architectureVersion; long long size = 0; };

  std::string buildAuthorizeUrl(const std::string& publishableKey,
                                const std::string& redirectUri,
                                const std::string& challenge,
                                const std::string& state,
                                const std::string& prompt);   // e.g. "select_tone"
  std::string buildTokenFormBody(const std::string& publishableKey,
                                 const std::string& redirectUri,
                                 const std::string& code,
                                 const std::string& codeVerifier);
  std::string modelsUrl(const std::string& toneId);   // full /api/v1/models?tone_id=...
  std::string urlEncode(const std::string& s);

  TokenResponse           parseTokenResponse(const std::string& json);
  std::vector<ModelInfo>  parseModelList(const std::string& json);   // reads data[]
  // Picks the best A2 model from a list (architecture_version starting "a2"),
  // falling back to the first model if none are A2. Returns nullptr-equivalent
  // via bool.
  bool pickBestModel(const std::vector<ModelInfo>& models, ModelInfo& out);
  }
  ```
- Base URL constant: `https://www.tone3000.com/api/v1`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_tone3000api.cpp
#include <catch2/catch_all.hpp>
#include "net/Tone3000Api.h"
using namespace nam;

TEST_CASE("buildAuthorizeUrl includes all required PKCE params") {
    auto u = buildAuthorizeUrl("t3k_pub_x", "http://127.0.0.1:8912/callback",
                               "CHALLENGE", "STATE123", "select_tone");
    REQUIRE(u.find("client_id=t3k_pub_x") != std::string::npos);
    REQUIRE(u.find("response_type=code") != std::string::npos);
    REQUIRE(u.find("code_challenge=CHALLENGE") != std::string::npos);
    REQUIRE(u.find("code_challenge_method=S256") != std::string::npos);
    REQUIRE(u.find("state=STATE123") != std::string::npos);
    REQUIRE(u.find("prompt=select_tone") != std::string::npos);
    // redirect_uri must be percent-encoded
    REQUIRE(u.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A8912%2Fcallback") != std::string::npos);
}

TEST_CASE("buildTokenFormBody has the authorization_code grant fields") {
    auto b = buildTokenFormBody("t3k_pub_x", "http://127.0.0.1:8912/callback", "CODE", "VERIFIER");
    REQUIRE(b.find("grant_type=authorization_code") != std::string::npos);
    REQUIRE(b.find("code=CODE") != std::string::npos);
    REQUIRE(b.find("code_verifier=VERIFIER") != std::string::npos);
    REQUIRE(b.find("client_id=t3k_pub_x") != std::string::npos);
}

TEST_CASE("parseTokenResponse reads tokens") {
    auto t = parseTokenResponse(R"({"access_token":"AT","refresh_token":"RT","expires_in":3600,"token_type":"bearer","scope":"read"})");
    REQUIRE(t.ok);
    REQUIRE(t.accessToken == "AT");
    REQUIRE(t.refreshToken == "RT");
    REQUIRE(t.expiresIn == 3600);
}

TEST_CASE("parseTokenResponse handles an error body without throwing") {
    auto t = parseTokenResponse(R"({"error":"invalid_grant"})");
    REQUIRE_FALSE(t.ok);
}

TEST_CASE("parseModelList + pickBestModel prefers an A2 model") {
    const char* json = R"({"data":[
      {"id":"m1","name":"Amp A1","architecture_version":"a1","model_url":"https://x/m1.nam","size":100},
      {"id":"m2","name":"Amp A2","architecture_version":"a2-full","model_url":"https://x/m2.nam","size":50}
    ],"total":2})";
    auto ms = parseModelList(json);
    REQUIRE(ms.size() == 2);
    ModelInfo best;
    REQUIRE(pickBestModel(ms, best));
    REQUIRE(best.id == "m2");     // the a2 one
    REQUIRE(best.modelUrl == "https://x/m2.nam");
}

TEST_CASE("parseModelList tolerates garbage without throwing") {
    REQUIRE(parseModelList("not json").empty());
    ModelInfo best;
    REQUIRE_FALSE(pickBestModel({}, best));
}
```

- [ ] **Step 2: Write Tone3000Api.h/.cpp**

- `urlEncode`: percent-encode everything except unreserved `[A-Za-z0-9-._~]`.
- `buildAuthorizeUrl`: `https://www.tone3000.com/api/v1/oauth/authorize?` + urlencoded params (client_id, redirect_uri, response_type=code, code_challenge, code_challenge_method=S256, state, prompt).
- `buildTokenFormBody`: urlencoded `grant_type=authorization_code&code=..&code_verifier=..&redirect_uri=..&client_id=..`.
- `modelsUrl`: `https://www.tone3000.com/api/v1/models?tone_id=` + urlEncode(toneId).
- Parsers use nlohmann with try/catch → on any error return `ok=false` / empty vector (never throw). `parseModelList` reads `data[]` with `value(key, default)` per field.
- `pickBestModel`: first entry whose `architectureVersion` starts with "a2" (case-insensitive); else `models[0]`; false if list empty.

- [ ] **Step 3: Wire CMake, run tests (fail → pass)**

Append `test_tone3000api.cpp` + `${CMAKE_SOURCE_DIR}/Source/net/Tone3000Api.cpp` to `nam_tests` (nlohmann include dir already present from Phase 3). Run tests + full suite.

- [ ] **Step 4: Commit**

```bash
git add Source/net/Tone3000Api.h Source/net/Tone3000Api.cpp tests/test_tone3000api.cpp tests/CMakeLists.txt
git commit -m "feat: TONE3000 API url/body builders and JSON parsers"
```

---

### Task 3: Publishable-key injection + `Tone3000Auth` (JUCE OAuth flow)

Build-time key injection, then the JUCE side of the PKCE flow: launch the browser, catch the loopback redirect, exchange the code for tokens, and persist them.

**Files:**
- Modify: `CMakeLists.txt` (read `.env`, define `TONE3000_PUBLISHABLE_KEY`)
- Create: `Source/app/Tone3000Auth.h`
- Create: `Source/app/Tone3000Auth.cpp`

**Interfaces:**
- Consumes: `Pkce`, `Tone3000Api`, JUCE (`URL`, `StreamingSocket`, `WebInputStream`, `File`).
- Produces:
  ```cpp
  class Tone3000Auth {
  public:
      struct Result { bool ok=false; std::string toneId; std::string error; };
      explicit Tone3000Auth(juce::File tokenStoreFile);
      // Runs the whole select_tone flow on a background thread; calls `done` on
      // the message thread with the picked toneId (+ a valid access token stored).
      void beginSelectToneFlow(std::function<void(Result)> done);
      std::string accessToken() const;             // "" if not authed/expired
      bool hasValidToken() const;
      void loadTokens();  void clearTokens();
  private:
      // publishable key from TONE3000_PUBLISHABLE_KEY (build def); "" if unset.
  };
  ```

- [ ] **Step 1: Inject the publishable key via CMake**

In top-level `CMakeLists.txt`, before the app target, read `.env` if present and extract `TONE3000_PUBLISHABLE_KEY`:
```cmake
set(T3K_PUB "")
if(EXISTS "${CMAKE_SOURCE_DIR}/.env")
    file(STRINGS "${CMAKE_SOURCE_DIR}/.env" _envlines)
    foreach(_l ${_envlines})
        if(_l MATCHES "^TONE3000_PUBLISHABLE_KEY=(.*)$")
            set(T3K_PUB "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()
```
Then add to `NamPlayer` compile defs: `TONE3000_PUBLISHABLE_KEY="${T3K_PUB}"`. NEVER read/inject the client secret. If `T3K_PUB` is empty, the app compiles with an empty key and the UI shows "TONE3000 not configured."

- [ ] **Step 2: Write Tone3000Auth.h/.cpp**

Flow (on a background `juce::Thread`):
1. `auto pkce = nam::generatePkce(); auto state = nam::randomUrlToken(32);`
2. Bind a `juce::StreamingSocket` server on `127.0.0.1` to an ephemeral port `P` (try a small list of ports); `redirectUri = "http://127.0.0.1:" + P + "/callback"`.
3. `juce::URL(nam::buildAuthorizeUrl(pubKey, redirectUri, pkce.challenge, state, "select_tone")).launchInDefaultBrowser();`
4. Accept one connection, read the HTTP GET request line, parse the query (`code`, `state`, `tone_id`, `error`); write back a tiny `200 OK` HTML ("You can return to NAM Player") so the browser shows success; close.
5. Verify `state` matches (else Result.error). On `error` param → Result.error.
6. POST token exchange: `juce::URL(tokenUrl).withPOSTData(nam::buildTokenFormBody(pubKey, redirectUri, code, pkce.verifier))` with header `Content-Type: application/x-www-form-urlencoded`; read response; `nam::parseTokenResponse`. On `ok`, store tokens (file, 0600) with an absolute expiry (`now + expiresIn`).
7. Call `done({ok=true, toneId})` via `juce::MessageManager::callAsync`.
Token storage: write JSON `{access_token, refresh_token, expiry}` to `tokenStoreFile`; `chmod 0600` (via `juce::File` + `std::filesystem::permissions`). `hasValidToken()` = token present and `now < expiry - 60s`. (Refresh-token use can be added later; for v1, if expired, re-run the flow.)
NEVER log token/code values; redact in any error string.

- [ ] **Step 3: Build the app (compile+link gate)**

Add `Source/app/Tone3000Auth.cpp` (+ `Pkce.cpp`, `Tone3000Api.cpp`) and the picosha2 include dir to the `NamPlayer` target. `cmake --preset default && cmake --build --preset default --target NamPlayer -j4` and `--preset debug`. Both link.
NOTE: no unit test — network/browser/socket glue; verified manually in Task 5. Compile+link is the gate.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt Source/app/Tone3000Auth.h Source/app/Tone3000Auth.cpp
git commit -m "feat: TONE3000 OAuth2 PKCE flow (loopback redirect) + build-time publishable key"
```

---

### Task 4: `Tone3000Session` + MainComponent "Browse TONE3000" → download → library

Authenticated GET/download helpers, and the UI button that runs the flow, downloads the picked tone's A2 model, imports it into the library, and loads it.

**Files:**
- Create: `Source/app/Tone3000Session.h`
- Create: `Source/app/Tone3000Session.cpp`
- Modify: `Source/app/MainComponent.h`
- Modify: `Source/app/MainComponent.cpp`
- Modify: `CMakeLists.txt` (add Tone3000Session.cpp)

**Interfaces:**
- Produces:
  ```cpp
  class Tone3000Session {
  public:
      Tone3000Session(std::string accessToken);
      // Background: GET models for toneId, pick best A2, download bytes to destFile.
      // Calls done(ok, destFile, displayName) on the message thread.
      void downloadToneModel(const std::string& toneId, juce::File destDir,
                             std::function<void(bool, juce::File, juce::String)> done);
  };
  ```

- [ ] **Step 1: Write Tone3000Session.h/.cpp**

On a background thread: `juce::URL(nam::modelsUrl(toneId))` with header `Authorization: Bearer <token>` via `WebInputStream`; read body; `nam::parseModelList` + `nam::pickBestModel`; `juce::URL(best.modelUrl)` with Bearer → read bytes into `destDir/<sanitized best.name>.nam`; `done(true, file, best.name)` via callAsync. Any failure → `done(false, {}, error)`. Redact the token in logs.

- [ ] **Step 2: Wire MainComponent**

- Members: `Tone3000Auth t3kAuth_ { <appDataDir>/tone3000_tokens.json };`, `std::unique_ptr<Tone3000Session> t3kSession_;`, `juce::TextButton browseT3kButton_ { "Browse TONE3000" };`, `juce::Label t3kStatus_;`.
- If `TONE3000_PUBLISHABLE_KEY` is empty, disable the button and set status "TONE3000 not configured (.env)".
- `browseT3kButton_.onClick`: `t3kAuth_.beginSelectToneFlow(...)` (SafePointer-guard the completion). On `ok`: create `t3kSession_` with `t3kAuth_.accessToken()`, call `downloadToneModel(toneId, library_.subdir(Model), ...)`. On download `done(ok,file,name)` (SafePointer-guarded): `nam::importIntoLibrary(library_, file.getFullPathName().toStdString(), Model, nowSeconds())` (the file is already in the models dir; importIntoLibrary will register/copy — to avoid a double copy, register it directly OR import from the downloaded temp; simplest: download to a temp dir, then importIntoLibrary copies it into the library). Then `libraryPanel_.refresh()` and load it via the existing model-load path; update `t3kStatus_`.
- Lay out `browseT3kButton_` + `t3kStatus_` in `resized()`.

NOTE: download to a temp file (e.g. `juce::File::getSpecialLocation(tempDirectory)`), then `importIntoLibrary` copies into the library (consistent with the file-chooser import path) — avoids the "already in library dir" double-handling.

- [ ] **Step 3: Build both presets (compile+link gate) + full test suite**

`cmake --build --preset default --target NamPlayer -j4 && cmake --build --preset debug --target NamPlayer -j4 && cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests`. Both link; suite passes.

- [ ] **Step 4: Commit**

```bash
git add Source/app/Tone3000Session.h Source/app/Tone3000Session.cpp Source/app/MainComponent.h Source/app/MainComponent.cpp CMakeLists.txt
git commit -m "feat: Browse TONE3000 -> download A2 model -> import into library"
```

---

### Task 5: Manual verification + finish

- [ ] **Step 1: Full suite regression**

`cmake --build --preset default --target nam_tests -j4 && ./build/tests/nam_tests` — all pass.

- [ ] **Step 2: Manual OAuth + download (needs the user's TONE3000 login)**

Launch the Release bundle. Confirm:
1. "Browse TONE3000" is enabled (publishable key was injected from `.env`).
2. Click it → the system browser opens TONE3000's tone picker (logged in / prompts login).
3. Pick a tone → browser shows the "return to NAM Player" page; the app receives the callback.
4. The tone's A2 model downloads, appears in the library panel, and loads/plays.
5. Relaunch → the stored token means a second download doesn't require re-login (until expiry).

- [ ] **Step 3: Verify redirect_uri acceptance (dependency)**

If step 2 fails at the authorize screen with a redirect/URI error, the loopback `redirect_uri` isn't allowed for the publishable key — register `http://127.0.0.1/callback` (and/or the exact port) in the TONE3000 app settings, or adjust to the allowed value. Record the working redirect_uri.

- [ ] **Step 4: Commit marker**

```bash
git commit --allow-empty -m "chore: Phase 4a TONE3000 download verified"
```

---

## Dependencies to verify during implementation

- **redirect_uri registration:** TONE3000 must allow the loopback `redirect_uri` for the publishable key. Loopback (127.0.0.1) is the RFC 8252 standard for native apps and is commonly allowed with any port, but TONE3000 may require an exact registered value. The first real login (Task 5) confirms it; if it fails, register the URI. This is the main external unknown.
- **`prompt=select_tone` callback shape:** confirmed from docs to return `tone_id` + `code`; verify the exact param name (`tone_id`) on the first real callback and adjust the parser if needed (localized to `Tone3000Auth`'s query parse).
- **Model download auth:** `model_url` may be a TONE3000 API URL (needs Bearer) or a pre-signed storage URL (no auth). Try with Bearer first; if a 401/403 or double-auth issue occurs, retry without the header. Handle both in `Tone3000Session`.

## Self-Review (completed)

- **Spec coverage (§10.4, scoped to 4a):** OAuth2 PKCE login (Tasks 1/3), TONE3000 tone selection via hosted picker (Task 3, prompt=select_tone), download a model (Task 4), land it in the local library + play (Task 4, reuses Phase 3 `importIntoLibrary`). ✅ In-app search browser is explicitly Phase 4b.
- **Security:** publishable key only (Task 3 CMake reads only `TONE3000_PUBLISHABLE_KEY`); client secret never referenced; PKCE S256 mandatory and vetted vs RFC 7636 (Task 1); tokens stored 0600, never logged. ✅
- **JUCE isolation / testability:** PKCE + all URL/body/JSON logic are JUCE-free and unit-tested (Tasks 1–2); JUCE only wraps transport/browser/sockets/UI (Tasks 3–4), which are compile+manual. ✅
- **Reuse:** downloads flow through Phase 3 `importIntoLibrary` + the existing model-load path — no new engine/audio code. ✅
- **Placeholder scan:** full code for the tested core; network/UI tasks are structural with explicit steps, consistent with prior phases. The one external unknown (redirect_uri) is called out with a concrete fallback. ✅
- **Type consistency:** `PkcePair`, `TokenResponse`, `ModelInfo`, `buildAuthorizeUrl/buildTokenFormBody/parseTokenResponse/parseModelList/pickBestModel`, `Tone3000Auth`, `Tone3000Session` names consistent across tasks. ✅
- **Deferred (Phase 4b / later):** in-app search/trending/filter browser; refresh-token auto-renewal; Keychain/Keystore token storage; custom URL scheme redirect for mobile (Phase 5).
