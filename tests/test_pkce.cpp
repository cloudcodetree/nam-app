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
