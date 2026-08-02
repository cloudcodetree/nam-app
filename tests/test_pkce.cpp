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

    const std::string allowed =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    auto t64 = randomUrlToken(64);
    REQUIRE(t64.size() == 64);
    for (char c : t64) REQUIRE(allowed.find(c) != std::string::npos);

    REQUIRE(randomUrlToken(0) == "");
}

TEST_CASE("PKCE random generation has enough distinct outputs to smoke-test entropy") {
    // Not a proof of entropy, but a 32-bit-seeded generator would still
    // likely be distinct across only 100 draws, so this mainly documents
    // intent and guards against a degenerate (e.g. constant-output) RNG
    // regression. Distinctness of 100 draws from a space this large is
    // astronomically safe, so this should never be flaky.
    std::set<std::string> verifiers;
    for (int i = 0; i < 100; ++i)
        verifiers.insert(generatePkce().verifier);
    REQUIRE(verifiers.size() == 100);

    std::set<std::string> tokens;
    for (int i = 0; i < 100; ++i)
        tokens.insert(randomUrlToken(43));
    REQUIRE(tokens.size() == 100);
}
