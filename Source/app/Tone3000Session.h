#pragma once

#include <functional>
#include <memory>
#include <string>

#include <juce_core/juce_core.h>

namespace nam {

// Downloads a TONE3000 tone's best-fit A2 model file on a background
// thread, authenticating requests with a previously-obtained OAuth access
// token.
//
// SECURITY: the access token is only ever placed in an Authorization header
// sent directly to TONE3000's servers; it is never logged, and any error
// string surfaced to callers redacts it. HTTP redirects (e.g. a model_url
// that 302s to a pre-signed S3/CDN URL) are followed manually, one hop at a
// time, so the header is dropped the moment a redirect leaves tone3000.com.
class Tone3000Session {
public:
    explicit Tone3000Session(std::string accessToken);
    ~Tone3000Session();

    Tone3000Session(const Tone3000Session&) = delete;
    Tone3000Session& operator=(const Tone3000Session&) = delete;

    // Looks up the models available for `toneId`, picks the best A2 model
    // (see nam::pickBestModel), and downloads it into destDir under a
    // sanitized filename derived from the model's name
    // (destDir/<sanitized name>.nam).
    //
    // `done` is invoked exactly once, on the JUCE message thread: on
    // success with (true, downloadedFile, displayName); on any failure
    // with (false, juce::File{}, errorMessage).
    //
    // Calling this again while a download is already in progress cancels
    // the prior one first (its `done` is dropped, never invoked).
    void downloadToneModel(const std::string& toneId, juce::File destDir,
                           std::function<void(bool, juce::File, juce::String)> done);

private:
    class DownloadThread;

    std::string accessToken_;
    std::unique_ptr<DownloadThread> downloadThread_;
};

} // namespace nam
