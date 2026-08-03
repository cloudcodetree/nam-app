#include "app/Tone3000Session.h"

#include "net/Tone3000Api.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cctype>

namespace nam {

namespace {

constexpr int kConnectTimeoutMs = 15000;

// True if `url`'s host is tone3000.com or a subdomain of it (case-insensitive).
// Used to decide whether it's safe to attach our Bearer access token: model_url
// may be a pre-signed third-party storage URL (S3/CDN), and the token must
// never be sent to a host that isn't TONE3000 itself.
bool isTone3000Host(const std::string& url) {
    const auto schemeEnd = url.find("://");
    const std::string afterScheme = (schemeEnd == std::string::npos) ? url : url.substr(schemeEnd + 3);
    const auto slashPos = afterScheme.find('/');
    std::string host = (slashPos == std::string::npos) ? afterScheme : afterScheme.substr(0, slashPos);
    const auto colonPos = host.find(':'); // strip a port, if any
    if (colonPos != std::string::npos)
        host = host.substr(0, colonPos);

    std::transform(host.begin(), host.end(), host.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::string domain = "tone3000.com";
    if (host == domain)
        return true;
    const std::string dotSuffix = "." + domain;
    return host.size() > dotSuffix.size() &&
           host.compare(host.size() - dotSuffix.size(), dotSuffix.size(), dotSuffix) == 0;
}

// Strips path separators and other filesystem-hostile characters from a
// model name so it's safe to use as a filename component. Falls back to
// "model" if nothing usable remains.
std::string sanitizeFileName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20) continue; // control characters
        switch (c) {
            case '/': case '\\': case ':': case '*': case '?':
            case '"': case '<': case '>': case '|':
                out.push_back('_');
                break;
            default:
                out.push_back(c);
        }
    }
    const size_t begin = out.find_first_not_of(" \t.");
    if (begin == std::string::npos)
        return "model";
    const size_t end = out.find_last_not_of(" \t.");
    out = out.substr(begin, end - begin + 1);
    return out.empty() ? "model" : out;
}

} // namespace

// Runs entirely on a background thread; delivers its result to the
// caller's `done` via juce::MessageManager::callAsync so UI code touched by
// `done` never runs off the message thread.
class Tone3000Session::DownloadThread : public juce::Thread {
public:
    DownloadThread(std::string accessToken, std::string toneId, juce::File destDir,
                    std::function<void(bool, juce::File, juce::String)> done)
        : juce::Thread("Tone3000Download"),
          accessToken_(std::move(accessToken)),
          toneId_(std::move(toneId)),
          destDir_(std::move(destDir)),
          done_(std::move(done)) {}

    void run() override {
        juce::File resultFile;
        juce::String resultName;
        juce::String error;
        const bool ok = doDownload(resultFile, resultName, error);

        auto callback = std::move(done_);
        if (callback) {
            juce::MessageManager::callAsync([callback, ok, resultFile, resultName, error] {
                callback(ok, resultFile, ok ? resultName : error);
            });
        }
    }

private:
    // Performs a GET, optionally with a Bearer Authorization header, and
    // returns the raw response bytes. On an HTTP error status, `outError`
    // is set to the numeric status code as text (e.g. "401") so callers can
    // detect the auth-retry case; on a connection failure it's a generic,
    // token-free message. Never logs or includes the access token.
    bool authenticatedGet(const juce::URL& url, bool withAuth, juce::MemoryBlock& outBytes,
                           juce::String& outError) {
        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        juce::WebInputStream stream(url, false);
        if (withAuth)
            stream.withExtraHeaders("Authorization: Bearer " + juce::String(accessToken_));
        stream.withConnectionTimeout(kConnectTimeoutMs);

        if (!stream.connect(nullptr) || stream.isError()) {
            outError = "connection failed";
            return false;
        }
        const int status = stream.getStatusCode();
        if (status < 200 || status >= 300) {
            outError = juce::String(status);
            return false;
        }
        outBytes.reset();
        stream.readIntoMemoryBlock(outBytes);
        return true;
    }

    bool doDownload(juce::File& outFile, juce::String& outName, juce::String& outError) {
        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        // --- Step 1: fetch the model list for this tone. ---
        const juce::URL modelsUrl{juce::String(nam::modelsUrl(toneId_))};
        juce::MemoryBlock listBytes;
        juce::String listError;
        if (!authenticatedGet(modelsUrl, true, listBytes, listError)) {
            outError = (listError == "401" || listError == "403")
                ? "TONE3000 authentication failed (please try Browse TONE3000 again)"
                : "could not reach TONE3000 to list models";
            return false;
        }
        const juce::String body =
            juce::String::createStringFromData(listBytes.getData(), (int) listBytes.getSize());

        const auto models = nam::parseModelList(body.toStdString());
        nam::ModelInfo best;
        if (!nam::pickBestModel(models, best)) {
            outError = "TONE3000 returned no models for this tone";
            return false;
        }

        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        // --- Step 2: download the chosen model's bytes. model_url may be a
        // pre-signed third-party storage URL (S3/CDN) rather than a
        // tone3000.com URL; only attach our bearer token when the host is
        // actually tone3000.com, so the token is never leaked to a
        // third-party host. For the tone3000.com case, retry once without
        // the header if it's rejected (401/403). ---
        const juce::URL modelUrl{juce::String(best.modelUrl)};
        const bool modelUrlIsT3k = isTone3000Host(best.modelUrl);
        juce::MemoryBlock bytes;
        juce::String downloadError;
        bool downloaded = authenticatedGet(modelUrl, modelUrlIsT3k, bytes, downloadError);
        if (!downloaded && modelUrlIsT3k && (downloadError == "401" || downloadError == "403"))
            downloaded = authenticatedGet(modelUrl, false, bytes, downloadError);
        if (!downloaded) {
            outError = "failed to download the model file";
            return false;
        }
        if (bytes.getSize() == 0) {
            outError = "downloaded model was empty";
            return false;
        }

        if (!destDir_.isDirectory() && destDir_.createDirectory().failed()) {
            outError = "could not create the destination directory";
            return false;
        }

        const std::string fileName =
            sanitizeFileName(best.name.empty() ? best.id : best.name) + ".nam";
        const juce::File destFile = destDir_.getChildFile(juce::String(fileName));
        if (!destFile.replaceWithData(bytes.getData(), bytes.getSize())) {
            outError = "could not write the downloaded model to disk";
            return false;
        }

        outFile = destFile;
        outName = best.name.empty() ? juce::String(best.id) : juce::String(best.name);
        return true;
    }

    std::string accessToken_;
    std::string toneId_;
    juce::File destDir_;
    std::function<void(bool, juce::File, juce::String)> done_;
};

Tone3000Session::Tone3000Session(std::string accessToken) : accessToken_(std::move(accessToken)) {}

Tone3000Session::~Tone3000Session() {
    if (downloadThread_)
        downloadThread_->stopThread(20000);
}

void Tone3000Session::downloadToneModel(const std::string& toneId, juce::File destDir,
                                         std::function<void(bool, juce::File, juce::String)> done) {
    if (downloadThread_) {
        // A prior download is still in flight (or just finished): stop it
        // before starting a new one so we never have two DownloadThreads
        // (or a dangling one) alive at once. Its `done` is dropped, never
        // called.
        downloadThread_->stopThread(20000);
        downloadThread_.reset();
    }
    downloadThread_ = std::make_unique<DownloadThread>(accessToken_, toneId, std::move(destDir),
                                                        std::move(done));
    downloadThread_->startThread();
}

} // namespace nam
