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

TEST_CASE("pickBestModel treats architecture_version \"2\" as A2") {
    const char* json = R"({"data":[
      {"id":"m1","name":"Amp A1","architecture_version":"1","model_url":"https://x/m1.nam","size":100},
      {"id":"m2","name":"Amp A2","architecture_version":"2","model_url":"https://x/m2.nam","size":50}
    ],"total":2})";
    auto ms = parseModelList(json);
    REQUIRE(ms.size() == 2);
    ModelInfo best;
    REQUIRE(pickBestModel(ms, best));
    REQUIRE(best.id == "m2");
    REQUIRE(best.modelUrl == "https://x/m2.nam");
}

TEST_CASE("parseModelList tolerates garbage without throwing") {
    REQUIRE(parseModelList("not json").empty());
    ModelInfo best;
    REQUIRE_FALSE(pickBestModel({}, best));
}

TEST_CASE("modelsUrl includes tone_id, architecture, and page_size") {
    auto u = modelsUrl("79865", "2");
    REQUIRE(u.find("tone_id=79865") != std::string::npos);
    REQUIRE(u.find("architecture=2") != std::string::npos);
    REQUIRE(u.find("page_size=50") != std::string::npos);
}
