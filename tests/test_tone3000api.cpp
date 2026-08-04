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

TEST_CASE("buildAuthorizeUrl omits prompt entirely when empty (connect flow)") {
    auto u = buildAuthorizeUrl("t3k_pub_x", "http://127.0.0.1:8912/callback",
                               "CHALLENGE", "STATE123", "");
    REQUIRE(u.find("prompt=") == std::string::npos);
    // everything else is still present
    REQUIRE(u.find("client_id=t3k_pub_x") != std::string::npos);
    REQUIRE(u.find("state=STATE123") != std::string::npos);
}

TEST_CASE("buildTokenFormBody has the authorization_code grant fields") {
    auto b = buildTokenFormBody("t3k_pub_x", "http://127.0.0.1:8912/callback", "CODE", "VERIFIER");
    REQUIRE(b.find("grant_type=authorization_code") != std::string::npos);
    REQUIRE(b.find("code=CODE") != std::string::npos);
    REQUIRE(b.find("code_verifier=VERIFIER") != std::string::npos);
    REQUIRE(b.find("client_id=t3k_pub_x") != std::string::npos);
}

TEST_CASE("buildRefreshFormBody has the refresh_token grant fields") {
    auto b = buildRefreshFormBody("t3k_pub_x", "REFRESH_TOK");
    REQUIRE(b.find("grant_type=refresh_token") != std::string::npos);
    REQUIRE(b.find("refresh_token=REFRESH_TOK") != std::string::npos);
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

// Mirrors the REAL TONE3000 /models response shape (verified live): `id` is
// a JSON integer (not a string), `architecture_version` is a numeric
// string, and `size` is null for A2 models but a string like "standard" for
// A1 models. parseModelList must tolerate all of this rather than throwing
// a type_error and silently returning an empty list.
TEST_CASE("pickBestModel treats architecture_version \"2\" as A2 (real API shape)") {
    const char* json = R"({"data":[
      {"id":381630,"name":"VOX_AC30_08","architecture_version":"1","model_url":"https://x/m1.nam","size":"standard"},
      {"id":664349,"name":"RR AC30 NRM 160","architecture_version":"2","model_url":"https://x/m2.nam","size":null}
    ],"total":2,"page":1,"page_size":50,"total_pages":1})";
    auto ms = parseModelList(json);
    REQUIRE(ms.size() == 2);
    ModelInfo best;
    REQUIRE(pickBestModel(ms, best));
    REQUIRE(best.id == "664349");
    REQUIRE(best.modelUrl == "https://x/m2.nam");
}

TEST_CASE("parseModelList tolerates a mixed/oddly-typed entry without dropping the list") {
    // id as an integer, size as a string: a real, differently-shaped entry
    // (not garbage) must still parse rather than triggering the empty-list
    // fallback.
    const char* json = R"({"data":[
      {"id":381630,"name":"VOX_AC30_08","architecture_version":"1","model_url":"https://x/m1.nam","size":"standard"}
    ],"total":1})";
    auto ms = parseModelList(json);
    REQUIRE_FALSE(ms.empty());
    REQUIRE(ms[0].id == "381630");
    REQUIRE(ms[0].modelUrl == "https://x/m1.nam");
    REQUIRE(ms[0].size == 0); // "standard" isn't numeric; asLong() falls back to 0
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
    REQUIRE(u.find("query=") == std::string::npos);   // empty query is omitted
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
