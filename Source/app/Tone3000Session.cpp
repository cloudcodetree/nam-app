#include "app/Tone3000Session.h"

#include "net/Tone3000Api.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cctype>

namespace nam {

namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr int kMaxRedirects = 5;

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

// Resolves a redirect's `Location` header against the URL that produced it.
// Returns an empty/invalid URL (toString() == "") if `location` is empty or
// the base URL has no scheme to resolve a relative location against.
juce::URL resolveRedirectLocation(const juce::URL& base, const juce::String& location) {
    if (location.isEmpty())
        return {};
    if (location.startsWithIgnoreCase("http://") || location.startsWithIgnoreCase("https://"))
        return juce::URL(location);

    const juce::String scheme = base.getScheme();
    if (scheme.isEmpty())
        return {};

    if (location.startsWith("//")) // protocol-relative: //host/path
        return juce::URL(scheme + ":" + location);

    const juce::String origin = base.getOrigin(); // scheme://domain[:port]
    if (location.startsWithChar('/'))              // absolute path on the same origin
        return juce::URL(origin + location);

    // Relative path: resolve against the base URL's directory (RFC 3986 merge).
    juce::String basePath = base.getSubPath(false);
    const int lastSlash = basePath.lastIndexOfChar('/');
    basePath = (lastSlash >= 0) ? basePath.substring(0, lastSlash + 1) : juce::String();
    return juce::URL(origin + "/" + basePath + location);
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

// Performs a GET, optionally with a Bearer Authorization header, and returns
// the raw response bytes. On an HTTP error status (from the *first* hop),
// `outError` is set to the numeric status code as text (e.g. "401") so
// callers can detect the auth-retry case; on a connection failure it's a
// generic, token-free message. Never logs or includes the access token.
//
// Redirects are never auto-followed by the underlying stream
// (withNumRedirectsToFollow(0)): a 3xx is instead followed manually, one hop
// at a time, re-evaluating isTone3000Host() at every hop. The Authorization
// header is only ever attached when the *current* hop's host is
// tone3000.com; a redirect to any other host (e.g. a pre-signed S3/CDN URL)
// is requested without it, so the access token can never be leaked
// off-domain via a redirect. The chain is capped at kMaxRedirects hops.
//
// `thread` is consulted via threadShouldExit() at the top of every hop so a
// superseded/stopped caller thread aborts promptly instead of blocking on
// the network. Shared by DownloadThread and SearchThread.
bool authenticatedGet(juce::Thread& thread, const std::string& accessToken, const juce::URL& url,
                      bool withAuth, juce::MemoryBlock& outBytes, juce::String& outError) {
    juce::URL currentUrl = url;
    bool currentAuth = withAuth;

    for (int hop = 0;; ++hop) {
        if (thread.threadShouldExit()) {
            outError = "cancelled";
            return false;
        }
        if (hop > kMaxRedirects) {
            outError = "too many redirects";
            return false;
        }

        juce::WebInputStream stream(currentUrl, false);
        if (currentAuth)
            stream.withExtraHeaders("Authorization: Bearer " + juce::String(accessToken));
        stream.withConnectionTimeout(kConnectTimeoutMs);
        stream.withNumRedirectsToFollow(0);

        if (!stream.connect(nullptr) || stream.isError()) {
            outError = "connection failed";
            return false;
        }
        const int status = stream.getStatusCode();

        if (status >= 300 && status < 400) {
            const juce::String location = stream.getResponseHeaders()["Location"];
            const juce::URL nextUrl = resolveRedirectLocation(currentUrl, location);
            if (nextUrl.isEmpty()) {
                outError = "redirect missing a usable Location header";
                return false;
            }
            currentUrl = nextUrl;
            currentAuth = isTone3000Host(currentUrl.toString(false).toStdString());
            continue;
        }

        if (status < 200 || status >= 300) {
            outError = juce::String(status);
            return false;
        }
        outBytes.reset();
        stream.readIntoMemoryBlock(outBytes);
        return true;
    }
}

} // namespace

// Runs entirely on a background thread; delivers its result to the
// caller's `done` via juce::MessageManager::callAsync so UI code touched by
// `done` never runs off the message thread.
class Tone3000Session::DownloadThread : public juce::Thread {
public:
    DownloadThread(std::string accessToken, std::string toneId, juce::File destDir,
                    bool preferSmallest,
                    std::function<void(bool, juce::File, juce::String)> done,
                    nam::ModelInfo explicitModel = {})
        : juce::Thread("Tone3000Download"),
          accessToken_(std::move(accessToken)),
          toneId_(std::move(toneId)),
          destDir_(std::move(destDir)),
          preferSmallest_(preferSmallest),
          explicitModel_(std::move(explicitModel)),
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
    bool doDownload(juce::File& outFile, juce::String& outName, juce::String& outError) {
        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        // A caller that already knows exactly which model it wants (Browse:
        // per-variant audition) skips the list fetch entirely.
        std::vector<nam::ModelInfo> models;
        if (!explicitModel_.modelUrl.empty())
            models.push_back(explicitModel_);

        // --- Step 1: fetch the model list for this tone. TONE3000 returns
        // the legacy A1 list when no `architecture` param is given (empty
        // for A2-format tones), so ask for A2 ("2") first and only fall
        // back to A1 ("1") if that comes back empty. ---
        if (models.empty())
        for (const char* architecture : {"2", "1"}) {
            const juce::URL modelsUrl{juce::String(nam::modelsUrl(toneId_, architecture))};
            juce::MemoryBlock listBytes;
            juce::String listError;
            if (!authenticatedGet(*this, accessToken_, modelsUrl, true, listBytes, listError)) {
                outError = (listError == "401" || listError == "403")
                    ? "TONE3000 authentication failed (please try Browse TONE3000 again)"
                    : "could not reach TONE3000 to list models";
                return false;
            }
            const juce::String body =
                juce::String::createStringFromData(listBytes.getData(), (int) listBytes.getSize());
            models = nam::parseModelList(body.toStdString());
            if (!models.empty())
                break;
            if (threadShouldExit()) {
                outError = "cancelled";
                return false;
            }
        }

        // Audition wants the lightest variant (nano/feather) — quickest to
        // download and cheapest to run; Keep wants the default best quality.
        nam::ModelInfo best;
        bool picked = false;
        if (preferSmallest_) {
            for (const auto& m : models)
                if (m.size > 0 && (!picked || m.size < best.size)) { best = m; picked = true; }
        }
        if (!picked && !nam::pickBestModel(models, best)) {
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
        bool downloaded = authenticatedGet(*this, accessToken_, modelUrl, modelUrlIsT3k, bytes, downloadError);
        if (!downloaded && modelUrlIsT3k && (downloadError == "401" || downloadError == "403"))
            downloaded = authenticatedGet(*this, accessToken_, modelUrl, false, bytes, downloadError);
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
    bool preferSmallest_ = false;
    nam::ModelInfo explicitModel_;
    std::function<void(bool, juce::File, juce::String)> done_;
};

// Runs an authenticated TONE3000 tone search entirely on a background
// thread; delivers its result to the caller's `done` via
// juce::MessageManager::callAsync so UI code touched by `done` never runs
// off the message thread. Mirrors DownloadThread's lifecycle exactly (own
// thread, stopThread()-and-drop on supersede, joined in the owner's
// destructor) but is otherwise independent of it: a search and a download
// may be in flight at the same time.
class Tone3000Session::SearchThread : public juce::Thread {
public:
    SearchThread(std::string accessToken, std::string query, int page,
                 std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done)
        : juce::Thread("Tone3000Search"),
          accessToken_(std::move(accessToken)),
          query_(std::move(query)),
          page_(page),
          done_(std::move(done)) {}

    void run() override {
        std::vector<nam::ToneInfo> resultTones;
        juce::String error;
        const bool ok = doSearch(resultTones, error);

        auto callback = std::move(done_);
        if (callback) {
            juce::MessageManager::callAsync([callback, ok, resultTones, error] {
                callback(ok, resultTones, error);
            });
        }
    }

private:
    bool doSearch(std::vector<nam::ToneInfo>& outTones, juce::String& outError) {
        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        const juce::URL searchUrl{
            juce::String(nam::buildSearchUrl(query_, page_, /*pageSize*/ 25, /*namOnly*/ true))};
        juce::MemoryBlock bytes;
        juce::String getError;
        if (!authenticatedGet(*this, accessToken_, searchUrl, true, bytes, getError)) {
            outError = (getError == "401" || getError == "403")
                ? "TONE3000 authentication failed (please try Connect TONE3000 again)"
                : "could not reach TONE3000 to search";
            return false;
        }

        if (threadShouldExit()) {
            outError = "cancelled";
            return false;
        }

        const juce::String body =
            juce::String::createStringFromData(bytes.getData(), (int) bytes.getSize());
        outTones = nam::parseToneList(body.toStdString());
        return true;
    }

    std::string accessToken_;
    std::string query_;
    int page_;
    std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done_;
};

// Fetches the model list for one tone (A2 first, A1 fallback) on its own
// thread. Same lifecycle discipline as SearchThread.
class Tone3000Session::ListModelsThread : public juce::Thread {
public:
    ListModelsThread(std::string accessToken, std::string toneId,
                     std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done)
        : juce::Thread("Tone3000ListModels"),
          accessToken_(std::move(accessToken)),
          toneId_(std::move(toneId)),
          done_(std::move(done)) {}

    void run() override {
        std::vector<nam::ModelInfo> models;
        juce::String error;
        bool ok = false;
        for (const char* architecture : {"2", "1"}) {
            if (threadShouldExit()) { error = "cancelled"; break; }
            const juce::URL url{juce::String(nam::modelsUrl(toneId_, architecture))};
            juce::MemoryBlock bytes;
            juce::String getError;
            if (!authenticatedGet(*this, accessToken_, url, true, bytes, getError)) {
                error = "could not reach TONE3000 to list models";
                continue;
            }
            const juce::String body =
                juce::String::createStringFromData(bytes.getData(), (int) bytes.getSize());
            models = nam::parseModelList(body.toStdString());
            if (!models.empty()) { ok = true; error.clear(); break; }
        }
        if (!ok && error.isEmpty()) error = "no models for this tone";

        auto callback = std::move(done_);
        if (callback) {
            juce::MessageManager::callAsync([callback, ok, models, error] {
                callback(ok, models, error);
            });
        }
    }

private:
    std::string accessToken_;
    std::string toneId_;
    std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done_;
};

Tone3000Session::Tone3000Session(std::string accessToken) : accessToken_(std::move(accessToken)) {}

Tone3000Session::~Tone3000Session() {
    if (downloadThread_)
        downloadThread_->stopThread(20000);
    if (searchThread_)
        searchThread_->stopThread(20000);
    if (listThread_)
        listThread_->stopThread(20000);
}

void Tone3000Session::listToneModels(const std::string& toneId,
        std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done) {
    if (listThread_) {
        listThread_->stopThread(20000);
        listThread_.reset();
    }
    listThread_ = std::make_unique<ListModelsThread>(accessToken_, toneId, std::move(done));
    listThread_->startThread();
}

void Tone3000Session::downloadModel(const nam::ModelInfo& model, juce::File destDir,
        std::function<void(bool, juce::File, juce::String)> done) {
    if (downloadThread_) {
        downloadThread_->stopThread(20000);
        downloadThread_.reset();
    }
    downloadThread_ = std::make_unique<DownloadThread>(accessToken_, model.id, std::move(destDir),
                                                        false, std::move(done), model);
    downloadThread_->startThread();
}

void Tone3000Session::downloadToneModel(const std::string& toneId, juce::File destDir,
                                         std::function<void(bool, juce::File, juce::String)> done,
                                         bool preferSmallest) {
    if (downloadThread_) {
        // A prior download is still in flight (or just finished): stop it
        // before starting a new one so we never have two DownloadThreads
        // (or a dangling one) alive at once. Its `done` is dropped, never
        // called.
        downloadThread_->stopThread(20000);
        downloadThread_.reset();
    }
    downloadThread_ = std::make_unique<DownloadThread>(accessToken_, toneId, std::move(destDir),
                                                        preferSmallest, std::move(done));
    downloadThread_->startThread();
}

void Tone3000Session::search(const std::string& query, int page,
                              std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
    if (searchThread_) {
        // A prior search is still in flight (or just finished): stop it
        // before starting a new one so we never have two SearchThreads (or a
        // dangling one) alive at once. Its `done` is dropped, never called.
        searchThread_->stopThread(20000);
        searchThread_.reset();
    }
    searchThread_ = std::make_unique<SearchThread>(accessToken_, query, page, std::move(done));
    searchThread_->startThread();
}

} // namespace nam
