#pragma once

#include <string>

namespace nam {

struct PkcePair {
    std::string verifier;
    std::string challenge;
};

// Standard base64 alphabet with '-' and '_' in place of '+' and '/', and no
// '=' padding (RFC 4648 §5, "base64url").
std::string base64UrlNoPad(const unsigned char* data, size_t len);

// base64url(SHA256(verifier)) — the PKCE S256 code_challenge derivation
// (RFC 7636 §4.2).
std::string sha256Challenge(const std::string& verifier);

// Generates a random 64-char code_verifier (RFC 7636 §4.1) and its S256
// code_challenge.
PkcePair generatePkce();

// Generates nChars drawn from the PKCE unreserved charset
// [A-Za-z0-9-._~], suitable for an OAuth `state` parameter.
std::string randomUrlToken(int nChars);

}   // namespace nam
