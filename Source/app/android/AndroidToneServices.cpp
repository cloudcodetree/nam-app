#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"
#include "model/IrLoader.h"
#include "model/LibraryImporter.h"
#include "model/LibraryEntry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

#include "dr_wav.h"

// AndroidAudioApp's TONE3000 + library service half (split out per the
// no-god-files rule): auth/session lifecycle, search, download/keep/save,
// on-the-fly stack loads, artwork cache, and the model cache. Nothing here
// touches the audio device — that stays in AndroidAudioApp.cpp.

// --- TONE3000 -------------------------------------------------------------
juce::File AndroidAudioApp::tokenStoreFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/tone3000_tokens.json");
}

std::string AndroidAudioApp::defaultLibraryDir() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/library")
        .getFullPathName()
        .toStdString();
}

long long AndroidAudioApp::nowSeconds() {
    using namespace std::chrono;
    return (long long)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void AndroidAudioApp::doSearch(
    juce::String query, std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
    if (!t3kAuth_.isConfigured()) {
        done(false, {}, "not configured (.env key missing)");
        return;
    }

    // Runs the actual search with a fresh session built from the current token.
    // Backfill artwork for tones kept before art caching existed (or whose
    // download failed): any kept result missing its file gets a quiet fetch.
    auto withBackfill = [this, done](bool ok, std::vector<nam::ToneInfo> tones, juce::String err) {
        if (ok)
            for (const auto& t : tones)
                if (!libraryIdForTone(t.id).empty()) fetchArtwork(t);
        done(ok, std::move(tones), std::move(err));
    };

    auto run = [this, query, withBackfill] {
        // Reuse the session unless the token changed: destroying it joins
        // its download threads ON THIS (message) THREAD — up to 20 s of ANR
        // and a cancelled in-flight keep download (same guard as
        // withValidToken).
        const auto tok = t3kAuth_.accessToken();
        if (t3kSession_ == nullptr || sessionToken_ != tok) {
            t3kSession_ = std::make_unique<nam::Tone3000Session>(tok);
            sessionToken_ = tok;
        }
        t3kSession_->search(query.toStdString(), 1, withBackfill);
    };

    if (t3kAuth_.hasValidToken()) {
        run();
        return;
    }

    // No valid token: try a silent refresh, else the browser Connect flow
    // (loopback OAuth — the on-device browser redirects back to 127.0.0.1).
    t3kAuth_.tryRefresh([this, run, done](bool refreshed) {
        if (refreshed) {
            run();
            return;
        }
        t3kAuth_.beginConnectFlow([run, done](nam::Tone3000Auth::Result r) {
            if (!r.ok) {
                done(false, {}, juce::String(r.error));
                return;
            }
            run();
        });
    });
}

void AndroidAudioApp::doSearchEx(
    nam::SearchParams params,
    std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
    if (!t3kAuth_.isConfigured()) {
        done(false, {}, "not configured (.env key missing)");
        return;
    }

    auto withBackfill = [this, done](bool ok, std::vector<nam::ToneInfo> tones, juce::String err) {
        if (ok)
            for (const auto& t : tones)
                if (!libraryIdForTone(t.id).empty()) fetchArtwork(t);
        done(ok, std::move(tones), std::move(err));
    };

    auto run = [this, params, withBackfill] {
        // Session reuse guard — see doSearch (ANR + dropped-download hazard).
        const auto tok = t3kAuth_.accessToken();
        if (t3kSession_ == nullptr || sessionToken_ != tok) {
            t3kSession_ = std::make_unique<nam::Tone3000Session>(tok);
            sessionToken_ = tok;
        }
        t3kSession_->search(params, withBackfill);
    };

    if (t3kAuth_.hasValidToken()) {
        run();
        return;
    }
    t3kAuth_.tryRefresh([this, run, done](bool refreshed) {
        if (refreshed) {
            run();
            return;
        }
        t3kAuth_.beginConnectFlow([run, done](nam::Tone3000Auth::Result r) {
            if (!r.ok) {
                done(false, {}, juce::String(r.error));
                return;
            }
            run();
        });
    });
}

void AndroidAudioApp::loadModelEntry(const nam::LibraryEntry& e) {
    const std::string path = library_.subdir(nam::LibraryType::Model) + "/" + e.fileName;
    auto m = nam::NamModel::load(path, (int)sampleRate_, blockSize_);
    juce::Logger::writeToLog(
        "loadModelEntry '" + juce::String(e.displayName) + "' " + (m != nullptr ? "ok" : "FAILED") +
        " (file " + (juce::File(juce::String(path)).existsAsFile() ? "exists" : "MISSING") +
        ", sr=" + juce::String(sampleRate_) + ", block=" + juce::String(blockSize_) + ")");
    if (m != nullptr) {
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
        library_.markUsed(e.id, nowSeconds());
        library_.save();
    }
}

juce::File AndroidAudioApp::stacksFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/stacks.json");
}

// See the header comment / AppShell::loadStacksState: called only when
// StackModel::parse degraded the file's content to {} -- the raw bytes are
// worth keeping around rather than letting the next save clobber them.
// Overwrites any earlier .bak (only the most recently unreadable file is
// worth keeping); a missing source file is a silent no-op.
void AndroidAudioApp::backupStacksJson() {
    const auto src = stacksFile();
    if (!src.existsAsFile()) return;
    src.copyFileTo(src.getSiblingFile("stacks.json.bak"));
}

void AndroidAudioApp::doLoadToneLive(nam::ToneInfo tone,
                                     std::function<void(bool, juce::String)> done) {
    const bool isIr = (tone.format == "ir");
    const auto localFile = modelCacheFile((isIr ? "ir_" : "keep_") + tone.id);
    auto apply = [this, isIr, tone, done](juce::File f) {
        if (isIr) {
            if (auto ir = nam::loadImpulseResponse(f.getFullPathName().toStdString(),
                                                   (int)sampleRate_, dsp::kMaxIrTaps)) {
                engine_.setImpulse(ir);
                engine_.setIrEnabled(true);
                done(true, juce::String(tone.title));
            } else {
                done(false, "could not read impulse");
            }
            return;
        }
        if (auto m = nam::NamModel::load(f.getFullPathName().toStdString(), (int)sampleRate_,
                                         blockSize_)) {
            engine_.setModel(std::move(m));
            modelLoaded_ = true;
            done(true, juce::String(tone.title));
        } else {
            done(false, "could not load model");
        }
    };
    if (localFile.existsAsFile()) {
        apply(localFile);
        return;
    }
    doDownloadOnly(tone, [localFile, apply, done](bool ok, juce::String msg) {
        if (!ok) {
            done(false, std::move(msg));
            return;
        }
        apply(localFile);
    });
}

juce::File AndroidAudioApp::defaultToneFile() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("default_tone.nam");
}

juce::File AndroidAudioApp::defaultToneNameFile() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("default_tone.name");
}

void AndroidAudioApp::fetchPopularDefault() {
    if (defaultFetchKicked_) return;
    defaultFetchKicked_ = true;

    withValidToken([this](bool ok) {
        if (!ok) return;   // offline / never connected: bundled stays
        t3kSession_->search(
            "", 1, [this](bool sOk, std::vector<nam::ToneInfo> tones, juce::String) {
                if (!sOk) return;
                const nam::ToneInfo* best = nullptr;
                for (const auto& t : tones)
                    if (t.format == "nam" && t.a2Count > 0 &&
                        (best == nullptr || t.downloads > best->downloads))
                        best = &t;
                if (best == nullptr) return;
                const auto tone = *best;
                fetchArtwork(tone);
                doDownloadOnly(tone, [this, tone](bool dlOk, juce::String) {
                    if (!dlOk) return;
                    const auto keep = modelCacheFile("keep_" + tone.id);
                    if (!keep.existsAsFile()) return;
                    keep.copyFileTo(defaultToneFile());
                    defaultToneNameFile().replaceWithText(juce::String(tone.title));
                    juce::Logger::writeToLog("popular default fetched: " +
                                             juce::String(tone.title));
                    // Deck still empty and we're sitting on the bundled tone?
                    // Swap in the popular default right away.
                    if (library_.all(nam::LibraryType::Model).empty()) {
                        if (auto m = nam::NamModel::load(
                                defaultToneFile().getFullPathName().toStdString(), (int)sampleRate_,
                                blockSize_)) {
                            engine_.setModel(std::move(m));
                            modelLoaded_ = true;
                            if (shell_ != nullptr)
                                shell_->setNowPlayingInfo(juce::String(tone.title),
                                                          "TONE3000 " +
                                                              juce::String::fromUTF8("\xC2\xB7") +
                                                              " MOST KEPT");
                        }
                    }
                });
            });
    });
}

juce::File AndroidAudioApp::artworkFile(const std::string& toneId) {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/artwork")
        .getChildFile(toneId + ".jpg");
}

std::string AndroidAudioApp::toneIdFromEntry(const nam::LibraryEntry& e) {
    // Kept entries came from keep_<toneId>.nam / ir_<toneId>.nam (possibly
    // " (2)"-suffixed); the tone id is the digit run after the prefix.
    for (const char* prefix : { "keep_", "ir_" }) {
        const auto& f = e.fileName;
        const auto plen = std::string(prefix).size();
        if (f.rfind(prefix, 0) != 0) continue;
        std::string id;
        for (size_t i = plen; i < f.size() && std::isdigit((unsigned char)f[i]); ++i) id += f[i];
        if (!id.empty()) return id;
    }
    return {};
}

void AndroidAudioApp::fetchArtwork(nam::ToneInfo tone) {
    // tone.id and imageUrl are API-supplied: the id becomes a filename (must
    // not traverse out of the artwork dir) and the URL is fetched (https
    // only, bounded size). Tone ids are numeric on the real API.
    const bool idIsNumeric =
        !tone.id.empty() && std::all_of(tone.id.begin(), tone.id.end(),
                                        [](unsigned char c) { return std::isdigit(c); });
    if (!idIsNumeric || tone.id.size() > 20) return;
    if (tone.imageUrl.rfind("https://", 0) != 0 || tone.imageUrl.size() > 2048) return;
    if (artworkFile(tone.id).existsAsFile()) return;
    juce::Thread::launch([tone] {
        juce::URL url{ juce::String(tone.imageUrl) };
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(15000));
        if (stream == nullptr) return;
        // Bounded read: a hostile/misconfigured server must not OOM the app.
        constexpr size_t kMaxImageBytes = 8 * 1024 * 1024;
        juce::MemoryBlock raw;
        while (!stream->isExhausted()) {
            if (raw.getSize() >= kMaxImageBytes) return;
            juce::HeapBlock<char> chunk(65536);
            const int n = stream->read(chunk.getData(), 65536);
            if (n <= 0) break;
            raw.append(chunk.getData(), (size_t)n);
        }
        auto img = juce::ImageFileFormat::loadFrom(raw.getData(), raw.getSize());
        if (!img.isValid()) return;   // e.g. webp — JUCE can't decode it
        // Downscale so per-swipe decode stays cheap (card is ~<700 px wide).
        constexpr int kMaxDim = 700;
        if (juce::jmax(img.getWidth(), img.getHeight()) > kMaxDim) {
            const float s = (float)kMaxDim / (float)juce::jmax(img.getWidth(), img.getHeight());
            img = img.rescaled(juce::roundToInt(img.getWidth() * s),
                               juce::roundToInt(img.getHeight() * s),
                               juce::Graphics::highResamplingQuality);
        }
        const auto dest = artworkFile(tone.id);
        dest.getParentDirectory().createDirectory();
        juce::FileOutputStream out(dest);
        if (!out.openedOk()) return;
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality(0.85f);
        if (!jpeg.writeImageToStream(img, out)) {
            dest.deleteFile();
            return;
        }
        out.flush();
        // Cache hygiene (mirrors pruneModelCache): keep the most recent 64.
        auto dir = dest.getParentDirectory();
        juce::Array<juce::File> files = dir.findChildFiles(juce::File::findFiles, false, "*.jpg");
        if (files.size() > 64) {
            files.sort();   // fallback order; refine below by mod time
            std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
                return a.getLastModificationTime() < b.getLastModificationTime();
            });
            for (int i = 0; i < files.size() - 64; ++i) files.getReference(i).deleteFile();
        }
    });
}

std::string AndroidAudioApp::libraryIdForTone(const std::string& toneId) const {
    // Kept entries were imported from keep_<toneId>.nam / ir_<toneId>.nam,
    // so the library id starts with that stem (possibly " (2)" suffixed).
    for (const char* prefix : { "keep_", "ir_" }) {
        const std::string stem = prefix + toneId;
        for (auto type : { nam::LibraryType::Model, nam::LibraryType::Ir })
            for (const auto& e : library_.all(type))
                if (e.id.rfind(stem, 0) == 0 &&
                    (e.id.size() == stem.size() || e.id[stem.size()] == '.' ||
                     e.id[stem.size()] == ' '))
                    return e.id;
    }
    return {};
}

void AndroidAudioApp::doToggleKeep(nam::ToneInfo tone,
                                   std::function<void(bool, juce::String)> done) {
    // Heart = saved + favorite flag. Already saved -> toggle the flag only
    // (un-hearting KEEPS the download; the save button owns removal).
    const auto id = libraryIdForTone(tone.id);
    if (!id.empty()) {
        const auto* e = library_.find(id);
        library_.setFavorite(id, !(e != nullptr && e->favorite));
        // Heal pre-fix entries whose name is still the raw cache stem
        // ("ir_79857"): we know the real title now, so keep it.
        if (e != nullptr && !tone.title.empty() &&
            (e->displayName.rfind("ir_", 0) == 0 || e->displayName.rfind("keep_", 0) == 0))
            library_.setDisplayName(id, tone.title);
        library_.save();
        done(true, juce::String(tone.title));
        return;
    }
    fetchArtwork(tone);   // Play-screen art for the newly kept tone
    doDownload(tone, [this, tone, done](bool ok, juce::String msg) {
        if (ok) {
            const auto id2 = libraryIdForTone(tone.id);
            if (!id2.empty()) {
                library_.setFavorite(id2, true);
                library_.save();
            }
        }
        done(ok, std::move(msg));
    });
}

// Keep = favorite: imports the already-downloaded file into the Library
// (models as Model, IR tones as Ir). Fetches first only if never downloaded.
void AndroidAudioApp::doDownload(nam::ToneInfo tone, std::function<void(bool, juce::String)> done) {
    const bool isIr = (tone.format == "ir");
    const auto localFile = modelCacheFile((isIr ? "ir_" : "keep_") + tone.id);
    if (localFile.existsAsFile()) {
        auto* entry = nam::importIntoLibrary(library_, localFile.getFullPathName().toStdString(),
                                             isIr ? nam::LibraryType::Ir : nam::LibraryType::Model,
                                             nowSeconds(), tone.title);
        if (entry == nullptr) {
            done(false, "import failed");
            return;
        }
        library_.save();
        done(true, juce::String(entry->displayName));
        return;
    }
    doDownloadOnly(tone, [this, tone, done](bool ok, juce::String msg) {
        if (!ok) {
            done(false, msg);
            return;
        }
        doDownload(tone, done);   // local file exists now -> import branch
    });
}
