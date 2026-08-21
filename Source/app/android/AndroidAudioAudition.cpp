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

// AndroidAudioApp's audition/demo half (split out per the no-god-files
// rule): DI track decode + Karplus-Strong fallback synthesis, the
// publish-then-retire demo buffers, offline audition rendering, and the
// cab-IR audition path.

// --- Audition (demo riffs) ------------------------------------------------
// Pre-renders three short looping dry-guitar riffs via Karplus-Strong
// plucked strings: open chords, a pentatonic lead, and palm-muted chugs.
// Runs at prepare time (message thread); the audio thread only reads the
// finished buffers.
namespace {
struct DemoNote {
    double freq, t;
    float amp;
    double ring;
    float decay;
};

void buildKsLoop(std::vector<float>& loop, double sr, double loopSec, const DemoNote* notes,
                 size_t count) {
    loop.assign((size_t)(sr * loopSec), 0.0f);
    juce::Random rng(7);
    for (size_t k = 0; k < count; ++k) {
        const auto& n = notes[k];
        const int start = (int)(n.t * sr);
        const int N = juce::jmax(2, (int)(sr / n.freq));
        std::vector<float> line((size_t)N);
        for (auto& v : line) v = rng.nextFloat() * 2.0f - 1.0f;
        const int len = (int)(sr * n.ring);
        int idx = 0;
        for (int i = 0; i < len && start + i < (int)loop.size(); ++i) {
            const int j = (idx + 1) % N;
            const float out = 0.5f * (line[(size_t)idx] + line[(size_t)j]) * n.decay;
            line[(size_t)idx] = out;
            idx = j;
            loop[(size_t)(start + i)] += out * n.amp * 0.35f;
        }
    }
    for (auto& v : loop) v = std::tanh(v);   // gentle safety clip
}
}   // namespace

namespace {
// Decodes a WAV byte blob to mono floats at `sr`, trimmed to 12 s max.
bool decodeDiWav(const void* data, size_t size, double sr, std::vector<float>& outMono) {
    unsigned int ch = 0, fileSr = 0;
    drwav_uint64 frames = 0;
    float* raw =
        drwav_open_memory_and_read_pcm_frames_f32(data, size, &ch, &fileSr, &frames, nullptr);
    if (raw == nullptr || ch < 1 || frames == 0) {
        if (raw != nullptr) drwav_free(raw, nullptr);
        return false;
    }
    const drwav_uint64 maxFrames = (drwav_uint64)((double)fileSr * 12.0);
    frames = juce::jmin(frames, maxFrames);
    std::vector<float> mono((size_t)frames);
    for (drwav_uint64 i = 0; i < frames; ++i) mono[(size_t)i] = raw[i * ch];
    drwav_free(raw, nullptr);
    if ((double)fileSr != sr && fileSr > 0) {
        const double ratio = (double)fileSr / sr;
        std::vector<float> res((size_t)((double)frames / ratio));
        for (size_t i = 0; i < res.size(); ++i) {
            const double pos = (double)i * ratio;
            const size_t i0 = (size_t)pos;
            const float frac = (float)(pos - (double)i0);
            const float a = mono[juce::jmin(i0, mono.size() - 1)];
            const float b = mono[juce::jmin(i0 + 1, mono.size() - 1)];
            res[i] = a + (b - a) * frac;
        }
        mono = std::move(res);
    }
    outMono = std::move(mono);
    return true;
}

juce::File diCacheFile(int index) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("di_cache")
        .getChildFile("di_" + juce::String(index) + ".wav");
}
}   // namespace

void AndroidAudioApp::buildDemoLoop(double sr) {
    // Bundled tracks decode from BinaryData; fetched tracks reload lazily
    // (ensureDemoTrack) from the disk cache at the current rate. Clear
    // everything first — a device-rate change invalidates old buffers.
    // prepare-time: audio is stopped, so plain publishes + a full retire
    // flush are safe here.
    retiredDemos_.clear();
    bool allDecoded = true;
    for (int t = 0; t < nam::demo::kNumTracks; ++t) {
        publishTrack(t, nullptr);
        const char* res = nam::demo::kTracks[t].binaryResource;
        if (res == nullptr) continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource(res, size);
        std::vector<float> decoded;
        if (data != nullptr && size > 0 && decodeDiWav(data, (size_t)size, sr, decoded))
            publishTrack(t, std::make_shared<const std::vector<float>>(std::move(decoded)));
        else allDecoded = false;
    }
    if (allDecoded) {
        retiredDemos_.clear();   // nothing can be in flight while stopped
        return;
    }

    // 0: open chords in E minor (the original riff).
    static const DemoNote chords[] = {
        { 82.41, 0.0, 0.95f, 1.4, 0.996f },  { 98.00, 0.4, 0.70f, 1.4, 0.996f },
        { 110.0, 0.8, 0.80f, 1.4, 0.996f },  { 82.41, 1.2, 0.95f, 1.4, 0.996f },
        { 123.47, 1.6, 0.70f, 1.4, 0.996f }, { 110.0, 2.0, 0.80f, 1.4, 0.996f },
        { 98.00, 2.4, 0.70f, 1.4, 0.996f },  { 82.41, 2.8, 0.95f, 1.4, 0.996f },
    };
    // 1: single-note E-minor pentatonic lead, higher register.
    static const DemoNote lead[] = {
        { 164.81, 0.0, 0.80f, 1.0, 0.996f }, { 196.00, 0.4, 0.75f, 1.0, 0.996f },
        { 220.00, 0.8, 0.80f, 1.0, 0.996f }, { 246.94, 1.2, 0.85f, 1.0, 0.996f },
        { 293.66, 1.5, 0.80f, 1.0, 0.996f }, { 329.63, 1.9, 0.90f, 1.3, 0.997f },
        { 246.94, 2.5, 0.75f, 0.9, 0.996f }, { 220.00, 2.8, 0.70f, 0.9, 0.996f },
    };
    // 2: palm-muted low-E chugs with open accents (short ring = mute).
    static const DemoNote chugs[] = {
        { 82.41, 0.0, 0.95f, 0.12, 0.960f }, { 82.41, 0.2, 0.85f, 0.12, 0.960f },
        { 82.41, 0.4, 0.90f, 0.12, 0.960f }, { 82.41, 0.6, 0.85f, 0.12, 0.960f },
        { 82.41, 0.8, 1.00f, 0.55, 0.992f },   // open accent
        { 82.41, 1.2, 0.90f, 0.12, 0.960f }, { 82.41, 1.4, 0.85f, 0.12, 0.960f },
        { 98.00, 1.6, 0.95f, 0.30, 0.985f },   // G2 stab
        { 82.41, 2.0, 0.90f, 0.12, 0.960f }, { 82.41, 2.2, 0.85f, 0.12, 0.960f },
        { 110.0, 2.4, 0.95f, 0.40, 0.988f },   // A2 stab
        { 82.41, 2.8, 0.95f, 0.12, 0.960f }, { 82.41, 3.0, 0.85f, 0.12, 0.960f },
    };
    std::vector<float> ks0, ks1, ks2;
    buildKsLoop(ks0, sr, 3.2, chords, std::size(chords));
    buildKsLoop(ks1, sr, 3.2, lead, std::size(lead));
    buildKsLoop(ks2, sr, 3.2, chugs, std::size(chugs));
    publishTrack(0, std::make_shared<const std::vector<float>>(std::move(ks0)));
    publishTrack(1, std::make_shared<const std::vector<float>>(std::move(ks1)));
    publishTrack(2, std::make_shared<const std::vector<float>>(std::move(ks2)));
    publishTrack(3, demoTracks_[0]);   // bass fallback: reuse chords synth
    retiredDemos_.clear();             // audio stopped: nothing in flight
}

// --- Demo-buffer publish-then-retire (message thread only) ----------------
void AndroidAudioApp::reclaimRetiredDemos() {
    const auto now = appBlocks_.load(std::memory_order_acquire);
    retiredDemos_.erase(std::remove_if(retiredDemos_.begin(), retiredDemos_.end(),
                                       [now](const auto& r) { return now >= r.second + 2; }),
                        retiredDemos_.end());
    // Stagnation fallback (device stopped: the counter never advances, so
    // the block gate can never fire): only safe when no callback is in
    // flight RIGHT NOW — the acquire on inCallback_ proves the last one
    // fully finished, and a callback starting after this check reads the
    // current published pointers, never a retiree. Mirrors ToneEngine.
    while (retiredDemos_.size() > 8 && now == retiredDemos_.front().second &&
           !inCallback_.load(std::memory_order_acquire))
        retiredDemos_.erase(retiredDemos_.begin());
}

void AndroidAudioApp::publishTrack(int index, std::shared_ptr<const std::vector<float>> buf) {
    reclaimRetiredDemos();
    const auto stamp = appBlocks_.load(std::memory_order_acquire);
    auto& owner = demoTracks_[(size_t)index];
    if (owner != nullptr) retiredDemos_.push_back({ std::move(owner), stamp });
    owner = std::move(buf);
    demoTracksRT_[(size_t)index].store(owner != nullptr ? owner.get() : nullptr,
                                       std::memory_order_release);
}

void AndroidAudioApp::publishSlot(int index, std::shared_ptr<const std::vector<float>> buf) {
    reclaimRetiredDemos();
    const auto stamp = appBlocks_.load(std::memory_order_acquire);
    auto& owner = demoSlots_[(size_t)index];
    if (owner != nullptr) retiredDemos_.push_back({ std::move(owner), stamp });
    owner = std::move(buf);
    demoSlotsRT_[(size_t)index].store(owner != nullptr ? owner.get() : nullptr,
                                      std::memory_order_release);
}

void AndroidAudioApp::setDemoTrack(int index) {
    demoTrack_ = juce::jlimit(0, nam::demo::kNumTracks - 1, index);
    demoTrackRT_.store(demoTrack_, std::memory_order_relaxed);
}

void AndroidAudioApp::setCab(int index) {
    cab_ = juce::jlimit(0, nam::demo::kNumCabs - 1, index);
    const auto ir = cabIrs_[(size_t)cab_];
    engine_.setImpulse(ir);
    engine_.setIrEnabled(cab_ > 0 && ir != nullptr);
}

void AndroidAudioApp::ensureDemoTrack(int index, std::function<void(bool)> done) {
    if (index < 0 || index >= nam::demo::kNumTracks) {
        done(false);
        return;
    }
    if (const auto& cur = demoTracks_[(size_t)index]; cur != nullptr && !cur->empty()) {
        done(true);
        return;
    }
    // A fetch is already in flight: don't start a second download — the
    // publish will land shortly and playback picks it up (silence until).
    if (demoFetching_[(size_t)index]) {
        done(true);
        return;
    }
    demoFetching_[(size_t)index] = true;

    const double sr = sampleRate_;
    const auto cache = diCacheFile(index);
    const juce::String fileName(nam::demo::kTracks[index].fileName);

    juce::Thread::launch([this, index, sr, cache, fileName, done] {
        juce::MemoryBlock bytes;
        if (cache.existsAsFile()) {
            cache.loadFileAsData(bytes);
        } else {
            // MIT-licensed DI from TONE3000's web-player repo.
            const juce::URL url("https://raw.githubusercontent.com/tone-3000/"
                                "neural-amp-modeler-wasm/main/ui/public/inputs/" +
                                juce::URL::addEscapeChars(fileName, false));
            if (auto stream = url.createInputStream(
                    juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(15000))) {
                stream->readIntoMemoryBlock(bytes, 12 * 1024 * 1024);
            }
            if (bytes.getSize() > 1024) {
                cache.getParentDirectory().createDirectory();
                cache.replaceWithData(bytes.getData(), bytes.getSize());
            }
        }

        auto mono = std::make_shared<std::vector<float>>();
        const bool ok =
            bytes.getSize() > 1024 && decodeDiWav(bytes.getData(), bytes.getSize(), sr, *mono);
        juce::MessageManager::callAsync([this, index, mono, ok, done] {
            demoFetching_[(size_t)index] = false;
            // Publish a fresh immutable buffer; any audio block still holding
            // the old shared_ptr keeps a valid reference (no in-place write).
            if (ok) publishTrack(index, mono);
            const auto& cur = demoTracks_[(size_t)index];
            done(ok && cur != nullptr && !cur->empty());
        });
    });
}

void AndroidAudioApp::setDemoActive(bool on) {
    demoPos_.store(0, std::memory_order_relaxed);
    demoOn_.store(on, std::memory_order_relaxed);
    if (!on) demoLive_.store(false, std::memory_order_relaxed);
}

void AndroidAudioApp::setDemoLivePlaying(bool on) {
    if (!on) {
        setDemoActive(false);
        return;
    }
    // Restart from the top only when it was not already rolling, so toggling
    // pause/play does not lose your place in the riff.
    if (!demoOn_.load(std::memory_order_relaxed)) demoPos_.store(0, std::memory_order_relaxed);
    demoLive_.store(true, std::memory_order_relaxed);
    demoOn_.store(true, std::memory_order_relaxed);
}

void AndroidAudioApp::setLiveInputMuted(bool muted) {
    liveMuted_.store(muted || alwaysMuteLive_, std::memory_order_relaxed);
}

void AndroidAudioApp::installRenderedDemo(std::vector<float> rendered, bool preservePosition) {
    const int next = (demoSlot_.load(std::memory_order_relaxed) + 1) & 1;
    const size_t len = rendered.size();
    // Publish, never mutate: a reader holding the old buffer keeps it alive.
    publishSlot(next, std::make_shared<const std::vector<float>>(std::move(rendered)));
    // Model/cab switches keep the demo rolling from the same spot (the DI
    // timeline is identical); anything else starts from the top.
    const bool wasPlaying = demoOn_.load(std::memory_order_relaxed);
    if (!(preservePosition && wasPlaying) || len == 0) demoPos_.store(0, std::memory_order_relaxed);
    else demoPos_.store(demoPos_.load(std::memory_order_relaxed) % len, std::memory_order_relaxed);
    demoSlot_.store(next, std::memory_order_release);
    demoLive_.store(false, std::memory_order_relaxed);   // slot playback mode
    demoOn_.store(true, std::memory_order_relaxed);
}

void AndroidAudioApp::cacheAudition(const std::string& toneId, const std::vector<float>& rendered) {
    constexpr size_t kMaxEntries = 12;   // ~7 MB worst case
    for (auto& e : auditionCache_)
        if (e.first == toneId) {
            e.second = rendered;
            return;
        }
    if (auditionCache_.size() >= kMaxEntries) auditionCache_.erase(auditionCache_.begin());
    auditionCache_.emplace_back(toneId, rendered);
}

const std::vector<float>* AndroidAudioApp::cachedAudition(const std::string& toneId) const {
    for (const auto& e : auditionCache_)
        if (e.first == toneId) return &e.second;
    return nullptr;
}

void AndroidAudioApp::withValidToken(std::function<void(bool)> then) {
    auto finish = [this, then](bool ok) {
        if (ok) {
            // Only rebuild the session when the token actually changed:
            // destroying it joins its download threads, which would cancel
            // an in-flight keep/audition download.
            const auto tok = t3kAuth_.accessToken();
            if (t3kSession_ == nullptr || sessionToken_ != tok) {
                t3kSession_ = std::make_unique<nam::Tone3000Session>(tok);
                sessionToken_ = tok;
            }
        }
        then(ok);
    };
    if (t3kAuth_.hasValidToken()) {
        finish(true);
        return;
    }
    t3kAuth_.tryRefresh([finish](bool refreshed) { finish(refreshed); });
}

juce::File AndroidAudioApp::modelCacheFile(const std::string& scope) {
    // The scope embeds API-supplied ids (tone/model). Whitelist the charset
    // so a hostile id can never traverse out of the cache dir (same
    // invariant fetchArtwork enforces for artwork paths).
    std::string safe;
    safe.reserve(scope.size());
    for (const char c : scope)
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') safe += c;
    if (safe.empty()) safe = "invalid";
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audition_models")
        .getChildFile(juce::String(safe) + ".nam");
}

void AndroidAudioApp::pruneModelCache() {
    auto dir =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("audition_models");
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.nam");
    if (files.size() <= 24) return;
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getLastModificationTime() < b.getLastModificationTime();
    });
    for (int i = 0; i < files.size() - 24; ++i) files.getReference(i).deleteFile();
}

// One unified audition job: load the model off-thread, benchmark a short
// stretch of DI through it, then pick the mode by MEASURED speed — if the
// device renders comfortably faster than real time, stream live (works for
// full A2 standards on fast phones with optimized builds); otherwise finish
// the offline render and play the buffer. The emulator always pre-renders.
void AndroidAudioApp::auditionFromFile(juce::File file, bool deleteAfter,
                                       const std::string& cacheKey, juce::String displayName,
                                       std::function<void(bool, juce::String)> done,
                                       std::shared_ptr<const std::vector<float>> overrideIr) {
    // Deliberately do NOT stop the current demo: it keeps playing until the
    // new tone is ready, then swaps in place — clean A/B comparisons.
    const std::string path = file.getFullPathName().toStdString();
    const double sr = sampleRate_;
    const int liveBlock = blockSize_;
    const bool forcePre = preRenderAuditions_;
    const auto src = demoTracks_[(size_t)demoTrack_];   // owner read, message thread
    auto dry = std::make_shared<std::vector<float>>(src != nullptr ? *src : std::vector<float>());
    auto cabIr = overrideIr != nullptr ? overrideIr : (cab_ > 0) ? cabIrs_[(size_t)cab_] : nullptr;

    juce::Thread::launch([this, path, sr, liveBlock, forcePre, dry, deleteAfter, cacheKey,
                          displayName, done, cabIr] {
        constexpr int block = 256;
        auto offline = std::make_unique<dsp::ToneEngine>();
        offline->prepare((int)sr, block);
        auto m = nam::NamModel::load(path, (int)sr, block);
        if (m == nullptr) {
            if (deleteAfter) juce::File(juce::String(path)).deleteFile();
            juce::MessageManager::callAsync([done] { done(false, "model load failed"); });
            return;
        }
        offline->setModel(std::move(m));
        if (cabIr != nullptr) {
            offline->setImpulse(cabIr);
            offline->setIrEnabled(true);
        }

        auto out = std::make_shared<std::vector<float>>(dry->size(), 0.0f);
        std::vector<float> chunk(block, 0.0f);

        // Benchmark: render the first ~0.25 s and time it.
        const size_t benchEnd = juce::jmin(dry->size(), (size_t)(sr * 0.25));
        const auto t0 = juce::Time::getHighResolutionTicks();
        size_t i = 0;
        for (; i < benchEnd; i += block) {
            const int n = (int)std::min((size_t)block, dry->size() - i);
            std::copy(dry->begin() + (long)i, dry->begin() + (long)i + n, chunk.begin());
            offline->render(chunk.data(), out->data() + i, n);
        }
        const double benchSec =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
        const double speed = benchSec > 0 ? ((double)benchEnd / sr) / benchSec : 0.0;
        juce::Logger::writeToLog("audition bench: " + juce::String(speed, 2) + "x realtime -> " +
                                 (!forcePre && speed >= 2.0 ? "live" : "pre-render"));

        if (!forcePre && speed >= 2.0) {
            // Fast enough for the audio thread: load a fresh instance at the
            // device block size and go live.
            auto live = nam::NamModel::load(path, (int)sr, liveBlock);
            if (deleteAfter) juce::File(juce::String(path)).deleteFile();
            nam::NamModel* raw = live.release();
            juce::MessageManager::callAsync([this, raw, displayName, done] {
                std::unique_ptr<nam::NamModel> model(raw);
                if (model == nullptr) {
                    done(false, "model load failed");
                    return;
                }
                engine_.setModel(std::move(model));
                modelLoaded_ = true;
                // Keep position when already playing (A/B); slot and live
                // playback share the same DI timeline.
                if (!demoOn_.load(std::memory_order_relaxed))
                    demoPos_.store(0, std::memory_order_relaxed);
                demoLive_.store(true, std::memory_order_relaxed);
                demoOn_.store(true, std::memory_order_relaxed);
                done(true, displayName);
            });
            return;
        }

        // Too heavy (or emulator): finish the offline render.
        float lastReported = 0.0f;
        for (; i < dry->size(); i += block) {
            const int n = (int)std::min((size_t)block, dry->size() - i);
            std::copy(dry->begin() + (long)i, dry->begin() + (long)i + n, chunk.begin());
            offline->render(chunk.data(), out->data() + i, n);
            const float frac = (float)i / (float)dry->size();
            if (frac - lastReported >= 0.03f) {
                lastReported = frac;
                juce::MessageManager::callAsync([this, frac] {
                    if (shell_ != nullptr) shell_->setAuditionProgress(0.1f + 0.9f * frac);
                });
            }
        }
        if (deleteAfter) juce::File(juce::String(path)).deleteFile();

        float pk = 0.0f;
        for (float v : *out) pk = std::max(pk, std::fabs(v));
        if (pk > 0.0001f) {
            const float g = 0.6f / pk;
            for (auto& v : *out) v *= g;
        }

        juce::MessageManager::callAsync([this, out, done, displayName, cacheKey] {
            cacheAudition(cacheKey, *out);
            installRenderedDemo(std::move(*out), true);
            done(true, displayName);
        });
    });
}

// Loads a downloaded IR .wav as the engine's cab impulse under the current
// amp model. Live devices swap it in gapless; the emulator re-renders the
// bundled model with this IR.
void AndroidAudioApp::applyIrAudition(juce::File irWav, const std::string& cacheKey,
                                      juce::String displayName,
                                      std::function<void(bool, juce::String)> done) {
    auto ir = nam::loadImpulseResponse(irWav.getFullPathName().toStdString(), (int)sampleRate_,
                                       dsp::kMaxIrTaps);
    if (ir == nullptr) {
        done(false, "IR load failed");
        return;
    }
    engine_.setImpulse(ir);
    engine_.setIrEnabled(true);
    if (preRenderAuditions_) {
        auditionFromFile(juce::File(juce::String(copyBundledModelToFile())), false, cacheKey,
                         displayName, done, ir);
        return;
    }
    if (!demoOn_.load(std::memory_order_relaxed)) {
        demoPos_.store(0, std::memory_order_relaxed);
        demoLive_.store(true, std::memory_order_relaxed);
        demoOn_.store(true, std::memory_order_relaxed);
    }
    done(true, displayName);
}

void AndroidAudioApp::doAuditionIr(nam::ToneInfo tone,
                                   std::function<void(bool, juce::String)> done) {
    const auto irFile = modelCacheFile("ir_" + tone.id);
    const std::string key = tone.id + "#ir#" + std::to_string(demoTrack_);
    const juce::String title(tone.title);

    if (irFile.existsAsFile()) {
        applyIrAudition(irFile, key, title, done);
        return;
    }
    withValidToken([this, tone, done, irFile, key, title](bool ok) {
        if (!ok) {
            done(false, "connect first");
            return;
        }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModel(
            tone.id, tempDir,
            [this, done, irFile, key, title](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (!dlOk) {
                    done(false, nameOrErr);
                    return;
                }
                irFile.getParentDirectory().createDirectory();
                if (file.moveFileTo(irFile)) applyIrAudition(irFile, key, title, done);
                else applyIrAudition(file, key, title, done);
            });
    });
}

void AndroidAudioApp::doAudition(nam::ToneInfo tone, std::function<void(bool, juce::String)> done) {
    if (tone.format == "ir") {
        doAuditionIr(std::move(tone), std::move(done));
        return;
    }

    const std::string key =
        tone.id + "#best#" + std::to_string(demoTrack_) + "#c" + std::to_string(cab_);
    // Rendered-audio cache: instant replay (covers heavy pre-rendered
    // models on device and everything on the emulator).
    if (const auto* hit = cachedAudition(key)) {
        installRenderedDemo(std::vector<float>(*hit), true);
        done(true, juce::String(tone.title));
        return;
    }

    // Pack auditions default to BEST quality: with optimized builds and
    // measured-speed routing, heavy models either run live or pre-render
    // briefly. The file is shared with the ↓ download (keep_ scope) — one
    // fetch serves audition, download state, and KEEP.
    const auto keepFile = modelCacheFile("keep_" + tone.id);
    if (keepFile.existsAsFile()) {
        auditionFromFile(keepFile, false, key, juce::String(tone.title), done);
        return;
    }
    const auto legacySmallest = modelCacheFile("auto_" + tone.id);

    withValidToken([this, tone, done, key, keepFile, legacySmallest](bool ok) {
        if (!ok) {
            // Offline fallback: an older smallest-variant download still plays.
            if (legacySmallest.existsAsFile())
                auditionFromFile(legacySmallest, false, key, juce::String(tone.title), done);
            else done(false, "connect first");
            return;
        }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModel(
            tone.id, tempDir,
            [this, done, key, keepFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (!dlOk) {
                    done(false, nameOrErr);
                    return;
                }
                keepFile.getParentDirectory().createDirectory();
                if (file.moveFileTo(keepFile)) {
                    pruneModelCache();
                    auditionFromFile(keepFile, false, key, nameOrErr, done);
                } else {
                    auditionFromFile(file, true, key, nameOrErr, done);
                }
            },
            false /* best quality */);
    });
}

void AndroidAudioApp::doDownloadOnly(nam::ToneInfo tone,
                                     std::function<void(bool, juce::String)> done) {
    const bool isIr = (tone.format == "ir");
    const auto localFile = modelCacheFile((isIr ? "ir_" : "keep_") + tone.id);
    if (localFile.existsAsFile()) {
        done(true, juce::String(tone.title));
        return;
    }

    withValidToken([this, tone, done, localFile](bool ok) {
        if (!ok) {
            done(false, "connect first");
            return;
        }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModelForKeep(
            tone.id, tempDir,
            [done, localFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (!dlOk) {
                    done(false, nameOrErr);
                    return;
                }
                localFile.getParentDirectory().createDirectory();
                if (!file.moveFileTo(localFile)) {
                    done(false, "could not store download");
                    return;
                }
                done(true, nameOrErr);
            });
    });
}

void AndroidAudioApp::doAuditionModel(const std::string& toneId, const nam::ModelInfo& model,
                                      bool isIr, std::function<void(bool, juce::String)> done) {
    const std::string key = toneId + "#" + model.id + "#" + std::to_string(demoTrack_) +
                            (isIr ? "#ir" : "#c" + std::to_string(cab_));
    const juce::String display(model.name.empty() ? model.id : model.name);
    if (const auto* hit = cachedAudition(key)) {
        installRenderedDemo(std::vector<float>(*hit), true);
        done(true, display);
        return;
    }

    const auto cachedFile = modelCacheFile("m_" + toneId + "_" + model.id);
    auto play = [this, isIr, key, display, done](juce::File f, bool deleteAfter) {
        if (isIr) applyIrAudition(f, key, display, done);   // IR variant = cab swap
        else auditionFromFile(f, deleteAfter, key, display, done);
    };
    if (cachedFile.existsAsFile()) {
        play(cachedFile, false);
        return;
    }

    withValidToken([this, model, done, cachedFile, play](bool ok) {
        if (!ok) {
            done(false, "connect first");
            return;
        }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadModel(
            model, tempDir,
            [this, done, cachedFile, play](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (!dlOk) {
                    done(false, nameOrErr);
                    return;
                }
                cachedFile.getParentDirectory().createDirectory();
                if (file.moveFileTo(cachedFile)) {
                    pruneModelCache();
                    play(cachedFile, false);
                } else {
                    play(file, true);
                }
            });
    });
}

void AndroidAudioApp::doListModels(
    const std::string& toneId,
    std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done) {
    withValidToken([this, toneId, done](bool ok) {
        if (!ok) {
            done(false, {}, "connect first");
            return;
        }
        t3kSession_->listToneModels(toneId, std::move(done));
    });
}
