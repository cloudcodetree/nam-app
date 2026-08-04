#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <juce_core/juce_core.h>

#include "net/Tone3000Api.h"

namespace nam {

// Drives the TONE3000 OAuth2 PKCE "select_tone" flow: launches the system
// browser, catches the loopback redirect on 127.0.0.1, exchanges the
// authorization code for tokens, and persists them to disk.
//
// SECURITY: this class only ever handles the publishable key
// (TONE3000_PUBLISHABLE_KEY) -- never the confidential client secret, which
// a native PKCE client must not embed or transmit. Tokens, the authorization
// code, and the PKCE code_verifier are never logged; error strings redact
// them.
class Tone3000Auth {
public:
    struct Result {
        bool ok = false;
        std::string toneId;
        std::string error;
    };

    // `tokenStoreFile` is where tokens are persisted as JSON (0600
    // permissions). Existing tokens, if any, are loaded immediately.
    explicit Tone3000Auth(juce::File tokenStoreFile);
    ~Tone3000Auth();

    Tone3000Auth(const Tone3000Auth&) = delete;
    Tone3000Auth& operator=(const Tone3000Auth&) = delete;

    // Runs the whole select_tone flow on a background thread: generate PKCE
    // + state, bind a loopback redirect server, launch the browser, accept
    // the redirect, verify state, exchange the code for tokens, and store
    // them. `done` is invoked on the JUCE message thread exactly once, with
    // the picked toneId on success (and a valid access token now stored).
    //
    // Calling this again while a flow is already in progress cancels the
    // prior flow first (its `done` callback is dropped, never invoked).
    void beginSelectToneFlow(std::function<void(Result)> done);

    // Runs the same PKCE flow as beginSelectToneFlow(), but without the
    // "select_tone" prompt: a standard authenticate that returns a Bearer
    // token but no tone_id (Result.toneId is left empty). Used to get a
    // token for in-app search without sending the user through TONE3000's
    // tone-picker.
    //
    // Calling this again (or beginSelectToneFlow()) while a flow is already
    // in progress cancels the prior flow first (its `done` callback is
    // dropped, never invoked).
    void beginConnectFlow(std::function<void(Result)> done);

    // Attempts a silent (no browser) refresh using the stored refresh token:
    // POSTs a refresh_token grant on a background thread and, on success,
    // stores the new tokens (preserving the existing refresh token if the
    // server doesn't rotate it). `done` is invoked on the JUCE message
    // thread exactly once with true on success, false otherwise (no refresh
    // token stored, request/parse failure, or bad status).
    //
    // Calling this again while a prior refresh is still in flight supersedes
    // it first (its `done` callback is dropped, never invoked), mirroring
    // beginFlow()'s discipline for flowThread_.
    void tryRefresh(std::function<void(bool ok)> done);

    // The stored access token, or "" if none is stored or it has expired
    // (see hasValidToken()).
    std::string accessToken() const;

    // True if a token is stored and won't expire for at least 60 more
    // seconds.
    bool hasValidToken() const;

    void loadTokens();
    void clearTokens();

    // True if TONE3000_PUBLISHABLE_KEY was set at build time. When false,
    // beginSelectToneFlow() fails immediately with an explanatory error.
    bool isConfigured() const;

private:
    class FlowThread;
    class RefreshThread;

    // Starts a background FlowThread running the PKCE flow with the given
    // `prompt` ("select_tone" or "" for a plain authenticate). Shared by
    // beginSelectToneFlow() and beginConnectFlow().
    void beginFlow(std::string prompt, std::function<void(Result)> done);

    Result runFlowOnThread(juce::Thread& thread, const std::string& prompt);

    // Runs the refresh_token POST on the background RefreshThread. Returns
    // true and persists the new tokens on success. Never logs `refreshToken`
    // or the response body.
    bool runRefreshOnThread(const std::string& refreshToken);

    void storeTokens(const TokenResponse& tokenResponse);

    // Persists the given tokens (0600 atomic temp+rename) and updates the
    // in-memory copies. `refreshToken` is the CALLER-RESOLVED refresh token
    // to store -- storeTokens() passes tokenResponse.refreshToken through
    // unchanged, while runRefreshOnThread() resolves an empty
    // tokenResponse.refreshToken (server didn't rotate) to the refresh token
    // that was just used, so a refresh never strands us without one.
    void storeTokensResolved(const std::string& accessToken, const std::string& refreshToken,
                              long long expiresIn);

    // Publishable key from the TONE3000_PUBLISHABLE_KEY build def; "" if
    // unset. NEVER the client secret (t3k_cs_...) -- a native PKCE client
    // must not hold it.
    std::string publishableKey_;
    juce::File tokenStoreFile_;

    mutable std::mutex tokenMutex_;
    std::string accessToken_;
    std::string refreshToken_;
    long long expiryEpochSeconds_ = 0;

    std::unique_ptr<FlowThread> flowThread_;
    std::unique_ptr<RefreshThread> refreshThread_;
};

} // namespace nam
