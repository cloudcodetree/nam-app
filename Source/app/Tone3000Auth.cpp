#include "app/Tone3000Auth.h"

#include "net/Pkce.h"

#include "json.hpp"

#include <juce_events/juce_events.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace nam {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// TONE3000's OAuth token endpoint (mirrors the /oauth/authorize base used by
// buildAuthorizeUrl() in Tone3000Api.cpp).
constexpr char kTokenUrl[] = "https://www.tone3000.com/api/v1/oauth/token";

// A handful of loopback ports to try in turn in case one is already in use.
constexpr int kCandidatePorts[] = { 49222, 49223, 49224, 49225, 49226 };

// Overall wait for the browser redirect. Generous because the user logs into
// TONE3000 and browses/picks a tone on their hosted site in between — a short
// window closes the loopback port before a real pick arrives (ERR_CONNECTION_REFUSED).
constexpr int kAcceptTimeoutMs = 600000;     // 10 minutes
constexpr int kAcceptPollMs = 250;           // poll granularity (also cancellation latency)
constexpr int kPostCallbackGraceMs = 1500;   // linger after capturing the code so the
                                             // browser's follow-up navigation gets served
constexpr int kRequestReadTimeoutMs = 5000;
constexpr int kMaxRequestLineBytes = 8192;
constexpr int kTokenConnectTimeoutMs = 15000;

long long nowEpochSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string urlDecode(const std::string& s) {
    auto hexVal = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]);
            const int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Parses the query string of an HTTP request target (e.g.
// "/callback?code=X&state=Y") into decoded key/value pairs.
std::map<std::string, std::string> parseQueryParams(const std::string& requestTarget) {
    std::map<std::string, std::string> result;
    const auto qpos = requestTarget.find('?');
    if (qpos == std::string::npos) return result;
    const std::string query = requestTarget.substr(qpos + 1);

    size_t start = 0;
    while (start <= query.size()) {
        const auto amp = query.find('&', start);
        const std::string pair =
            query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const auto eq = pair.find('=');
        if (eq != std::string::npos) {
            result[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            result[urlDecode(pair)] = "";
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return result;
}

// Splits an HTTP request line ("GET /callback?... HTTP/1.1") into method and
// request-target. Returns false if the line doesn't look like a request line.
bool parseRequestLine(const std::string& line, std::string& method, std::string& target) {
    std::istringstream iss(line);
    std::string httpVersion;
    return static_cast<bool>(iss >> method >> target >> httpVersion);
}

// Reads from `conn` until a full request line (terminated by "\r\n") has
// arrived, a byte cap is hit, or a timeout elapses. Only ever used to read
// the loopback redirect's GET line -- never any request body.
std::string readHttpRequestLine(juce::StreamingSocket& conn) {
    std::string buffer;
    char chunk[512];
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kRequestReadTimeoutMs);
    while (buffer.find("\r\n") == std::string::npos &&
           static_cast<int>(buffer.size()) < kMaxRequestLineBytes &&
           std::chrono::steady_clock::now() < deadline) {
        const int ready = conn.waitUntilReady(true, 250);
        if (ready != 1) continue;
        const int n = conn.read(chunk, static_cast<int>(sizeof(chunk)), false);
        if (n <= 0) break;
        buffer.append(chunk, static_cast<size_t>(n));
    }
    const auto pos = buffer.find("\r\n");
    return pos == std::string::npos ? buffer : buffer.substr(0, pos);
}

// Writes a minimal 200 OK HTML response so the browser tab shows something
// sensible, then the caller closes the connection. Includes permissive CORS +
// Private-Network-Access headers: modern OAuth providers (TONE3000's Next.js
// app) reach this loopback via a browser fetch() from an https origin, which
// Chrome treats as a public->private request needing these headers.
void writeRedirectLandingResponse(juce::StreamingSocket& conn) {
    static const char kBody[] = "<html><body>You can return to NAM Player.</body></html>";
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Private-Network: true\r\n"
             << "Content-Length: " << (sizeof(kBody) - 1) << "\r\n"
             << "Connection: close\r\n\r\n"
             << kBody;
    const std::string bytes = response.str();
    conn.write(bytes.data(), static_cast<int>(bytes.size()));
}

// Answers a CORS + Private-Network-Access preflight (OPTIONS) so the browser
// will then send the real callback request to our loopback server. Chrome
// requires Access-Control-Allow-Private-Network:true for https->127.0.0.1.
void writeCorsPreflightResponse(juce::StreamingSocket& conn) {
    std::ostringstream response;
    response << "HTTP/1.1 204 No Content\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: *\r\n"
             << "Access-Control-Allow-Private-Network: true\r\n"
             << "Access-Control-Max-Age: 600\r\n"
             << "Content-Length: 0\r\n"
             << "Connection: close\r\n\r\n";
    const std::string bytes = response.str();
    conn.write(bytes.data(), static_cast<int>(bytes.size()));
}

}   // namespace

// Runs entirely on the background FlowThread; delivers its Result to the
// caller's `done` callback via juce::MessageManager::callAsync.
class Tone3000Auth::FlowThread : public juce::Thread {
public:
    FlowThread(Tone3000Auth& owner, std::string prompt, std::function<void(Result)> done)
        : juce::Thread("Tone3000AuthFlow"), owner_(owner), prompt_(std::move(prompt)),
          done_(std::move(done)) {}

    void run() override {
        Result result = owner_.runFlowOnThread(*this, prompt_);
        auto callback = std::move(done_);
        if (callback) {
            // Hop back to the message thread before invoking the caller's
            // callback -- UI code must not run on this background thread.
            juce::MessageManager::callAsync([callback, result] { callback(result); });
        }
    }

private:
    Tone3000Auth& owner_;
    std::string prompt_;
    std::function<void(Result)> done_;
};

// Runs entirely on the background RefreshThread; delivers its bool result to
// the caller's `done` callback via juce::MessageManager::callAsync.
class Tone3000Auth::RefreshThread : public juce::Thread {
public:
    RefreshThread(Tone3000Auth& owner, std::string refreshToken, std::function<void(bool)> done)
        : juce::Thread("Tone3000AuthRefresh"), owner_(owner),
          refreshToken_(std::move(refreshToken)), done_(std::move(done)) {}

    void run() override {
        const bool ok = owner_.runRefreshOnThread(refreshToken_);
        auto callback = std::move(done_);
        if (callback) {
            // Hop back to the message thread before invoking the caller's
            // callback -- UI code must not run on this background thread.
            juce::MessageManager::callAsync([callback, ok] { callback(ok); });
        }
    }

private:
    Tone3000Auth& owner_;
    std::string refreshToken_;
    std::function<void(bool)> done_;
};

Tone3000Auth::Tone3000Auth(juce::File tokenStoreFile) : tokenStoreFile_(std::move(tokenStoreFile)) {
#if defined(TONE3000_PUBLISHABLE_KEY)
    publishableKey_ = TONE3000_PUBLISHABLE_KEY;
#endif
    loadTokens();
}

Tone3000Auth::~Tone3000Auth() {
    if (flowThread_) {
        // Ask run() to return (it polls threadShouldExit() at short
        // intervals throughout the accept loop) and block until it does, so
        // the FlowThread is never destroyed while still running.
        flowThread_->stopThread(20000);
    }
    if (refreshThread_) {
        // Same discipline as flowThread_ above: never destroy a running
        // thread out from under it.
        refreshThread_->stopThread(20000);
    }
}

bool Tone3000Auth::isConfigured() const { return !publishableKey_.empty(); }

void Tone3000Auth::beginSelectToneFlow(std::function<void(Result)> done) {
    beginFlow("select_tone", std::move(done));
}

void Tone3000Auth::beginConnectFlow(std::function<void(Result)> done) {
    beginFlow("", std::move(done));
}

void Tone3000Auth::beginFlow(std::string prompt, std::function<void(Result)> done) {
    if (flowThread_) {
        // A prior flow is still in flight (or just finished): stop it
        // before starting a new one so we never have two FlowThreads (or a
        // dangling one) alive at once. Its `done` is dropped, never called.
        flowThread_->stopThread(20000);
        flowThread_.reset();
    }
    flowThread_ = std::make_unique<FlowThread>(*this, std::move(prompt), std::move(done));
    flowThread_->startThread();
}

void Tone3000Auth::tryRefresh(std::function<void(bool ok)> done) {
    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(tokenMutex_);
        refreshToken = refreshToken_;
    }

    if (refreshToken.empty()) {
        if (done) {
            juce::MessageManager::callAsync([done] { done(false); });
        }
        return;
    }

    if (refreshThread_) {
        // Same supersede discipline as beginFlow(): stop any prior refresh
        // before starting a new one so we never have two RefreshThreads (or
        // a dangling one) alive at once. Its `done` is dropped, never
        // called.
        refreshThread_->stopThread(20000);
        refreshThread_.reset();
    }
    refreshThread_ =
        std::make_unique<RefreshThread>(*this, std::move(refreshToken), std::move(done));
    refreshThread_->startThread();
}

Tone3000Auth::Result Tone3000Auth::runFlowOnThread(juce::Thread& thread,
                                                   const std::string& prompt) {
    Result result;

    if (publishableKey_.empty()) {
        result.error = "TONE3000 is not configured (missing publishable key)";
        return result;
    }

    const auto pkce = generatePkce();
    const auto state = randomUrlToken(32);

    juce::StreamingSocket listener;
    int boundPort = 0;
    for (int port : kCandidatePorts) {
        if (listener.createListener(port, "127.0.0.1")) {
            boundPort = port;
            break;
        }
    }
    if (boundPort == 0) {
        result.error = "could not bind a local redirect server";
        return result;
    }

    const std::string redirectUri = "http://127.0.0.1:" + std::to_string(boundPort) + "/callback";
    const std::string authorizeUrl =
        buildAuthorizeUrl(publishableKey_, redirectUri, pkce.challenge, state, prompt);

    if (!juce::URL(authorizeUrl).launchInDefaultBrowser()) {
        result.error = "could not launch the system browser";
        return result;
    }

    // Accept loop. TONE3000's login page reaches the loopback via a browser
    // fetch(), so the browser sends a CORS + Private-Network-Access preflight
    // (OPTIONS) before the real callback request, and a Next.js soft navigation
    // may then hard-navigate to the same URL. So we (a) answer preflights with
    // the CORS/PNA headers, (b) keep the listener open across several
    // connections rather than closing after the first, and (c) once we've
    // captured the code, linger briefly so the follow-up navigation gets a
    // friendly landing page instead of a connection error.
    std::map<std::string, std::string> query;
    bool gotCallback = false;
    const auto acceptDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kAcceptTimeoutMs);
    auto graceDeadline = acceptDeadline;
    while (std::chrono::steady_clock::now() < acceptDeadline &&
           std::chrono::steady_clock::now() < graceDeadline) {
        if (thread.threadShouldExit()) {
            listener.close();
            result.error = "cancelled";
            return result;
        }
        const int ready = listener.waitUntilReady(true, kAcceptPollMs);
        if (ready < 0) {
            result.error = "redirect server socket error";
            listener.close();
            return result;
        }
        if (ready != 1) continue;

        std::unique_ptr<juce::StreamingSocket> conn(listener.waitForNextConnection());
        if (!conn) continue;

        const std::string requestLine = readHttpRequestLine(*conn);
        std::string method, target;
        if (parseRequestLine(requestLine, method, target)) {
            if (method == "OPTIONS") {
                writeCorsPreflightResponse(*conn);   // keep listening for the real request
            } else {
                if (!gotCallback) {
                    auto q = parseQueryParams(target);
                    if (q.count("code") > 0 || q.count("error") > 0) {
                        query = std::move(q);
                        gotCallback = true;
                        graceDeadline = std::chrono::steady_clock::now() +
                                        std::chrono::milliseconds(kPostCallbackGraceMs);
                    }
                }
                writeRedirectLandingResponse(*conn);
            }
        }
        conn->close();
    }
    listener.close();

    if (!gotCallback) {
        result.error = "timed out waiting for the browser redirect";
        return result;
    }

    // CSRF protection: the state we receive must exactly match the one we
    // sent, regardless of what else is in the callback.
    const auto stateIt = query.find("state");
    if (stateIt == query.end() || stateIt->second != state) {
        result.error = "state mismatch";
        return result;
    }

    const auto errorIt = query.find("error");
    if (errorIt != query.end()) {
        result.error = "authorization error: " + errorIt->second;
        return result;
    }

    const auto codeIt = query.find("code");
    if (codeIt == query.end() || codeIt->second.empty()) {
        result.error = "redirect was missing the authorization code";
        return result;
    }
    const std::string code = codeIt->second;

    std::string toneId;
    if (const auto toneIt = query.find("tone_id"); toneIt != query.end()) toneId = toneIt->second;

    // --- Token exchange ---
    const std::string formBody =
        buildTokenFormBody(publishableKey_, redirectUri, code, pkce.verifier);
    const juce::URL tokenUrl =
        juce::URL(juce::String(kTokenUrl)).withPOSTData(juce::String(formBody));

    juce::WebInputStream tokenStream(tokenUrl, true);
    tokenStream.withExtraHeaders("Content-Type: application/x-www-form-urlencoded");
    tokenStream.withConnectionTimeout(kTokenConnectTimeoutMs);

    if (!tokenStream.connect(nullptr) || tokenStream.isError()) {
        // Never interpolate `code` or `formBody` (contains code_verifier)
        // into diagnostics.
        result.error = "token exchange request failed";
        return result;
    }
    const int status = tokenStream.getStatusCode();
    const std::string responseBody = tokenStream.readEntireStreamAsString().toStdString();
    if (status < 200 || status >= 300) {
        result.error = "token exchange failed (HTTP " + std::to_string(status) + ")";
        return result;
    }

    const TokenResponse tokenResponse = parseTokenResponse(responseBody);
    if (!tokenResponse.ok || tokenResponse.accessToken.empty()) {
        result.error = "token exchange response was invalid";
        return result;
    }

    storeTokens(tokenResponse);

    result.ok = true;
    result.toneId = toneId;
    return result;
}

bool Tone3000Auth::runRefreshOnThread(const std::string& refreshToken) {
    if (publishableKey_.empty() || refreshToken.empty()) return false;

    const std::string formBody = buildRefreshFormBody(publishableKey_, refreshToken);
    const juce::URL tokenUrl =
        juce::URL(juce::String(kTokenUrl)).withPOSTData(juce::String(formBody));

    juce::WebInputStream tokenStream(tokenUrl, true);
    tokenStream.withExtraHeaders("Content-Type: application/x-www-form-urlencoded");
    tokenStream.withConnectionTimeout(kTokenConnectTimeoutMs);

    if (!tokenStream.connect(nullptr) || tokenStream.isError()) {
        // Never interpolate `refreshToken` or the response body into
        // diagnostics -- this function only ever returns a bool.
        return false;
    }
    const int status = tokenStream.getStatusCode();
    const std::string responseBody = tokenStream.readEntireStreamAsString().toStdString();
    if (status < 200 || status >= 300) return false;

    const TokenResponse tokenResponse = parseTokenResponse(responseBody);
    if (!tokenResponse.ok || tokenResponse.accessToken.empty()) return false;

    // CRITICAL ROTATION GOTCHA: if the server didn't return a new refresh
    // token, keep the one we just used -- overwriting refreshToken_ with
    // empty would strand us with no way to refresh again next time.
    const std::string newRefreshToken =
        tokenResponse.refreshToken.empty() ? refreshToken : tokenResponse.refreshToken;

    storeTokensResolved(tokenResponse.accessToken, newRefreshToken, tokenResponse.expiresIn);
    return true;
}

void Tone3000Auth::storeTokens(const TokenResponse& tokenResponse) {
    storeTokensResolved(tokenResponse.accessToken, tokenResponse.refreshToken,
                        tokenResponse.expiresIn);
}

void Tone3000Auth::storeTokensResolved(const std::string& accessToken,
                                       const std::string& refreshToken, long long expiresIn) {
    const long long expiry = nowEpochSeconds() + expiresIn;

    json j;
    j["access_token"] = accessToken;
    j["refresh_token"] = refreshToken;
    j["expiry"] = expiry;

    const fs::path path(tokenStoreFile_.getFullPathName().toStdString());
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);

    // Write to a temp file in the same directory, lock it down to 0600
    // while it is still empty, then write the token bytes -- this closes
    // the TOCTOU window where a world-readable file briefly holds secrets.
    // Once the write is confirmed good, atomically rename over the final
    // path so a crash mid-write can never corrupt (or half-write) the real
    // token file.
    fs::path tmpPath = path;
    tmpPath += ".tmp";

    bool wroteOk = false;
    {
        std::ofstream out(tmpPath, std::ios::trunc | std::ios::binary);
        if (out) {
            fs::permissions(tmpPath, fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace, ec);
            const std::string body = j.dump();
            out << body;
            out.flush();
            wroteOk = out.good();
        }
    }
    if (!wroteOk) {
        fs::remove(tmpPath, ec);
        return;
    }

    fs::rename(tmpPath, path, ec);
    if (ec) {
        fs::remove(tmpPath, ec);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tokenMutex_);
        accessToken_ = accessToken;
        refreshToken_ = refreshToken;
        expiryEpochSeconds_ = expiry;
    }
}

void Tone3000Auth::loadTokens() {
    const fs::path path(tokenStoreFile_.getFullPathName().toStdString());
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::lock_guard<std::mutex> lock(tokenMutex_);
        accessToken_.clear();
        refreshToken_.clear();
        expiryEpochSeconds_ = 0;
        return;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return;

    try {
        json j;
        in >> j;
        std::lock_guard<std::mutex> lock(tokenMutex_);
        accessToken_ = j.value("access_token", std::string());
        refreshToken_ = j.value("refresh_token", std::string());
        expiryEpochSeconds_ = j.value("expiry", static_cast<long long>(0));
    } catch (const std::exception&) {
        // Malformed token file: treat as if no tokens were stored.
        std::lock_guard<std::mutex> lock(tokenMutex_);
        accessToken_.clear();
        refreshToken_.clear();
        expiryEpochSeconds_ = 0;
    }
}

void Tone3000Auth::clearTokens() {
    {
        std::lock_guard<std::mutex> lock(tokenMutex_);
        accessToken_.clear();
        refreshToken_.clear();
        expiryEpochSeconds_ = 0;
    }
    const fs::path path(tokenStoreFile_.getFullPathName().toStdString());
    std::error_code ec;
    fs::remove(path, ec);
    if (!ec) return;

    // fs::remove failed (e.g. permissions weirdness): if the file is still
    // there, best-effort truncate it to empty so a later loadTokens() can't
    // resurrect stale tokens whose in-memory copy we just cleared. This is
    // deliberately best-effort and stays exception-free.
    std::error_code existsEc;
    if (fs::exists(path, existsEc)) {
        std::ofstream out(path, std::ios::trunc | std::ios::binary);
        // Nothing further to do if this also fails -- there is no
        // exception-free way to do better here.
    }
}

std::string Tone3000Auth::accessToken() const {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    if (accessToken_.empty()) return {};
    if (nowEpochSeconds() >= expiryEpochSeconds_ - 60) return {};
    return accessToken_;
}

bool Tone3000Auth::hasValidToken() const { return !accessToken().empty(); }

}   // namespace nam
