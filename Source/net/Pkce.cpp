#include "net/Pkce.h"

#include "picosha2.h"

#include <array>
#include <random>
#include <vector>

namespace nam {

namespace {

constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr char kUnreservedCharset[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

std::string randomFromCharset(const char* charset, int nChars) {
    const auto charsetLen = static_cast<int>(std::char_traits<char>::length(charset));
    std::uniform_int_distribution<int> dist(0, charsetLen - 1);
    std::string result;
    result.reserve(static_cast<size_t>(nChars));
    for (int i = 0; i < nChars; ++i)
        result.push_back(charset[dist(rng())]);
    return result;
}

} // namespace

std::string base64UrlNoPad(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);

    size_t i = 0;
    while (i + 3 <= len) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                                (static_cast<unsigned int>(data[i + 1]) << 8) |
                                static_cast<unsigned int>(data[i + 2]);
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 6) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[n & 0x3F]);
        i += 3;
    }

    const size_t remaining = len - i;
    if (remaining == 1) {
        const unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
    } else if (remaining == 2) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                                (static_cast<unsigned int>(data[i + 1]) << 8);
        out.push_back(kBase64UrlAlphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(n >> 6) & 0x3F]);
    }

    return out;
}

std::string sha256Challenge(const std::string& verifier) {
    std::vector<unsigned char> hash(32);
    picosha2::hash256(verifier.begin(), verifier.end(), hash.begin(), hash.end());
    return base64UrlNoPad(hash.data(), 32);
}

PkcePair generatePkce() {
    PkcePair pair;
    pair.verifier = randomFromCharset(kUnreservedCharset, 64);
    pair.challenge = sha256Challenge(pair.verifier);
    return pair;
}

std::string randomUrlToken(int nChars) {
    return randomFromCharset(kUnreservedCharset, nChars);
}

} // namespace nam
