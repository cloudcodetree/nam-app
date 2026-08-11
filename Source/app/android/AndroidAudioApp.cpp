#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"
#include "dsp/PitchDetector.h"
#include "model/IrLoader.h"
#include "model/LibraryImporter.h"
#include "model/LibraryEntry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sys/system_properties.h>

#include "dr_wav.h"

AndroidAudioApp::AndroidAudioApp() {
    setLookAndFeel(&laf_);

    // A guitar rig must not go dark mid-song: on Android this sets
    // FLAG_KEEP_SCREEN_ON for the window (cleared when the app closes).
    juce::Desktop::getInstance().setScreenSaverEnabled(false);

    // On the emulator the "microphone" is synthetic noise: amplifying it
    // through an amp model is pure hiss. Mute live input there — the demo
    // riff (audition) is the emulator's sound source. Real devices keep the
    // live guitar path.
    char qemu[PROP_VALUE_MAX] = {};
    if ((__system_property_get("ro.boot.qemu", qemu) > 0 && qemu[0] == '1') ||
        (__system_property_get("ro.kernel.qemu", qemu) > 0 && qemu[0] == '1')) {
        liveMuted_.store(true, std::memory_order_relaxed);
        alwaysMuteLive_ = true;
        preRenderAuditions_ = true;   // QEMU can't run NAM inference in real time
    }

    library_.load();
    // Migration (2026-08-10): before the saved/favorite split, every library
    // entry was an implicit "heart". If nothing carries the flag yet, mark
    // them all favorite so existing decks keep their hearts.
    {
        bool anyFav = false, anyEntry = false;
        for (auto type : { nam::LibraryType::Model, nam::LibraryType::Ir })
            for (const auto& e : library_.all(type)) {
                anyEntry = true;
                anyFav = anyFav || e.favorite;
            }
        if (anyEntry && !anyFav) {
            for (auto type : { nam::LibraryType::Model, nam::LibraryType::Ir })
                for (const auto& e : library_.all(type)) library_.setFavorite(e.id, true);
            library_.save();
        }
    }
    t3kAuth_.loadTokens();   // reuse a prior refresh token if present

    shell_ = std::make_unique<AppShell>(engine_);
    addAndMakeVisible(*shell_);
    AppShell::BrowseServices browse;
    browse.search =
        [this](juce::String q,
               std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
            doSearch(std::move(q), std::move(done));
        };
    browse.searchEx =
        [this](nam::SearchParams p,
               std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
            doSearchEx(std::move(p), std::move(done));
        };
    browse.keep = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doToggleKeep(std::move(t), std::move(done));
    };
    browse.isKept = [this](const std::string& toneId) {
        // Heart state = the favorite flag, not mere presence on device.
        const auto id = libraryIdForTone(toneId);
        if (id.empty()) return false;
        const auto* e = library_.find(id);
        return e != nullptr && e->favorite;
    };
    browse.isSaved = [this](const std::string& toneId) {
        return !libraryIdForTone(toneId).empty();
    };
    browse.save = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        fetchArtwork(t);
        doDownload(std::move(t), std::move(done));   // import; favorite stays false
    };
    browse.setFavoriteById = [this](const std::string& libraryId, bool fav) {
        library_.setFavorite(libraryId, fav);
        library_.save();
    };
    browse.libraryIdForTone = [this](const std::string& toneId) {
        return libraryIdForTone(toneId);
    };
    browse.removeKept = [this](const std::string& libraryId) {
        library_.remove(libraryId);
        library_.save();
    };
    browse.artworkForTone = [this](const nam::ToneInfo& t) {
        const auto f = artworkFile(t.id);
        if (f.existsAsFile()) return juce::ImageFileFormat::loadFrom(f);
        fetchArtwork(t);   // async; caller re-asks on the next card show
        return juce::Image();
    };
    browse.listKept = [this] {
        // The deck as browse rows. Entries without a TONE3000 id (sideloaded
        // files) can't drive the TONE3000 row actions, so they stay out.
        auto entries = library_.all(nam::LibraryType::Model);
        // Deck order = heart order (import time), oldest first.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const auto& a, const auto& b) { return a.addedAt < b.addedAt; });
        std::vector<nam::ToneInfo> out;
        for (const auto& e : entries) {
            const auto toneId = toneIdFromEntry(e);
            if (toneId.empty()) continue;
            nam::ToneInfo t;
            t.id = toneId;
            t.title = e.displayName;
            t.format = "nam";
            const auto a = juce::String(e.arch).toLowerCase();
            if (a.contains("slim") || a.startsWith("2") || a.startsWith("a2")) t.a2Count = 1;
            else t.a1Count = 1;
            out.push_back(std::move(t));
        }
        return out;
    };
    browse.downloadOnly = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doDownloadOnly(std::move(t), std::move(done));
    };
    browse.audition = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doAudition(std::move(t), std::move(done));
    };
    browse.auditionModel = [this](std::string toneId, nam::ModelInfo m, bool isIr,
                                  AppShell::DoneFn done) {
        doAuditionModel(toneId, m, isIr, std::move(done));
    };
    browse.muteLiveInput = [this](bool m) { setLiveInputMuted(m); };
    browse.listModels =
        [this](const std::string& toneId,
               std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done) {
            doListModels(toneId, std::move(done));
        };
    browse.setDemoTrack = [this](int t, std::function<void(bool)> done) {
        ensureDemoTrack(t, [this, t, done](bool ok) {
            if (ok) setDemoTrack(t);   // RT switch only after audio is loaded
            done(ok);
        });
    };
    browse.stopDemo = [this] { setDemoActive(false); };
    browse.setCab = [this](int c) { setCab(c); };
    browse.isAuditionCached = [this](const std::string& toneId) {
        if (preRenderAuditions_)
            return cachedAudition(toneId + "#best#" + std::to_string(demoTrack_) + "#c" +
                                  std::to_string(cab_)) != nullptr;
        // Live mode: any local file means instant audition, any riff.
        return modelCacheFile("keep_" + toneId).existsAsFile() ||
               modelCacheFile("auto_" + toneId).existsAsFile() ||
               modelCacheFile("ir_" + toneId).existsAsFile();
    };
    browse.isDownloaded = [](const std::string& toneId) {
        return modelCacheFile("keep_" + toneId).existsAsFile() ||
               modelCacheFile("ir_" + toneId).existsAsFile();
    };
    browse.loadStacksJson = [] { return stacksFile().loadFileAsString(); };
    browse.saveStacksJson = [](const juce::String& s) {
        const auto f = stacksFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText(s);
    };
    browse.loadTone = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doLoadToneLive(std::move(t), std::move(done));
    };
    browse.setTestTone = [this](bool on) { testTone_.store(on, std::memory_order_relaxed); };
    shell_->setBrowseServices(std::move(browse));
    shell_->setLibraryService([this] { return library_.all(nam::LibraryType::Model); },
                              [this](const nam::LibraryEntry& e) { loadModelEntry(e); });
    shell_->setIrService(
        [this] { return library_.all(nam::LibraryType::Ir); },
        [this](const nam::LibraryEntry& e) {
            const std::string p = library_.subdir(nam::LibraryType::Ir) + "/" + e.fileName;
            if (auto ir = nam::loadImpulseResponse(p, (int)sampleRate_, dsp::kMaxIrTaps)) {
                engine_.setImpulse(ir);
                engine_.setIrEnabled(true);
            }
        });
    shell_->setMuteService(
        [this](bool m) { inputMutedUser_.store(m, std::memory_order_relaxed); },
        [this](bool m) { outputMutedUser_.store(m, std::memory_order_relaxed); });
    shell_->setArtworkService([](const nam::LibraryEntry& e) {
        const auto id = toneIdFromEntry(e);
        return id.empty() ? juce::Image() : juce::ImageFileFormat::loadFrom(artworkFile(id));
    });
    shell_->setAudioDeviceService([this] { return audioSettingsState(); },
                                  [this](const juce::String& name) { selectInputDevice(name); },
                                  [this](const juce::String& name) { selectOutputDevice(name); },
                                  [this] { rescanAudioDevices(); },
                                  [this](const juce::String& label) { selectSampleRate(label); },
                                  [this](const juce::String& label) { selectBufferSize(label); });

    // 1 input (guitar) / 2 output. JUCE requests RECORD_AUDIO on input open.
    setAudioChannels(1, 2);

    // Android opens "System Default (Input)" = built-in mic. If a USB guitar
    // interface (iRig) is attached, route input to it instead.
    deviceManager.addChangeListener(this);
    preferUsbInput();
    clampBufferForLowLatency();

    setSize(900, 500);
    startTimerHz(30);

    // First run with an empty deck: fetch the most popular A2 tone as the
    // default in the background (bundled model covers the gap offline).
    if (library_.all(nam::LibraryType::Model).empty() && !defaultToneFile().existsAsFile())
        juce::MessageManager::callAsync([this] { fetchPopularDefault(); });
}

AndroidAudioApp::~AndroidAudioApp() {
    stopTimer();
    deviceManager.removeChangeListener(this);
    shutdownAudio();
    setLookAndFeel(nullptr);
}

std::string AndroidAudioApp::copyBundledModelToFile() {
    auto dest = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("model.nam");
    if (!dest.existsAsFile()) {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource("model_nam", size))
            dest.replaceWithData(data, (size_t)size);
    }
    return dest.getFullPathName().toStdString();
}

void AndroidAudioApp::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlockExpected;
    mono_.assign((size_t)juce::jmax(1, samplesPerBlockExpected), 0.0f);
    engine_.prepare((int)sampleRate, samplesPerBlockExpected);
    buildDemoLoop(sampleRate);

    // Bundled cab IRs (IrLoader wants a path; stage each through a temp file).
    for (int c = 1; c < nam::demo::kNumCabs; ++c) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(nam::demo::kCabs[c].binaryResource, size);
        if (data == nullptr || size <= 0) continue;
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("cab_" + juce::String(c) + ".wav");
        tmp.replaceWithData(data, (size_t)size);
        cabIrs_[(size_t)c] = nam::loadImpulseResponse(tmp.getFullPathName().toStdString(),
                                                      (int)sampleRate, dsp::kMaxIrTaps);
    }
    setCab(cab_);   // re-apply selection at the (possibly new) sample rate

    // Boot into the tone the player last used — the bundled model is a
    // quiet low-gain capture and makes a poor first impression; a real
    // library tone is what they expect to hear.
    {
        const auto entries = library_.all(nam::LibraryType::Model);
        const nam::LibraryEntry* recent = nullptr;
        for (const auto& e : entries)
            if (recent == nullptr || e.lastUsedAt > recent->lastUsedAt) recent = &e;
        if (recent != nullptr) {
            const std::string p = library_.subdir(nam::LibraryType::Model) + "/" + recent->fileName;
            if (auto m = nam::NamModel::load(p, (int)sampleRate, samplesPerBlockExpected)) {
                juce::Logger::writeToLog("prepare: last-used model '" +
                                         juce::String(recent->displayName) + "' ok");
                engine_.setModel(std::move(m));
                modelLoaded_ = true;
                const auto entry = *recent;
                juce::MessageManager::callAsync([this, entry] {
                    if (shell_ != nullptr) shell_->showEntryAsNowPlaying(entry);
                });
                return;
            }
        }
    }

    // Empty deck: use the cached popular default (fetched from TONE3000 on a
    // previous run), else the bundled file as an offline fallback while the
    // popular default downloads.
    const auto defFile = defaultToneFile();
    if (defFile.existsAsFile()) {
        if (auto m = nam::NamModel::load(defFile.getFullPathName().toStdString(), (int)sampleRate,
                                         samplesPerBlockExpected)) {
            juce::String name("Popular Tone");
            if (defaultToneNameFile().existsAsFile())
                name = defaultToneNameFile().loadFileAsString().trim();
            juce::Logger::writeToLog("prepare: popular default '" + name + "' ok");
            engine_.setModel(std::move(m));
            modelLoaded_ = true;
            juce::MessageManager::callAsync([this, name] {
                if (shell_ != nullptr)
                    shell_->setNowPlayingInfo(
                        name, "TONE3000 " + juce::String::fromUTF8("\xC2\xB7") + " MOST KEPT");
            });
            return;
        }
    }

    const auto bundledPath = copyBundledModelToFile();
    if (auto m = nam::NamModel::load(bundledPath, (int)sampleRate, samplesPerBlockExpected)) {
        juce::Logger::writeToLog("prepare: bundled model ok (sr=" + juce::String(sampleRate) +
                                 ", block=" + juce::String(samplesPerBlockExpected) + ", normAdj=" +
                                 juce::String(m->recommendedOutputDbAdjustment(), 1) + " dB)");
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
    } else {
        juce::Logger::writeToLog(
            "prepare: bundled model FAILED (path=" + juce::String(bundledPath) + ", exists=" +
            juce::String(juce::File(juce::String(bundledPath)).existsAsFile() ? "yes" : "no") +
            ", sr=" + juce::String(sampleRate) +
            ", block=" + juce::String(samplesPerBlockExpected) + ")");
    }
}

void AndroidAudioApp::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    inCallback_.store(true, std::memory_order_relaxed);
    auto* buf = info.buffer;
    const int n = info.numSamples;
    const int cap = (int)mono_.size();

    // Mirror the raw input into the tuner ring regardless of demo state —
    // the tuner always listens to the guitar, never to demo playback.
    {
        const float* rawIn = buf->getReadPointer(0, info.startSample);
        int w = tunerWrite_.load(std::memory_order_relaxed);
        // A user input-mute silences the tuner too (zeros flush the ring);
        // the automatic feedback guard does NOT — tuning without an
        // interface via the phone mic stays useful.
        const bool tunerMuted = inputMutedUser_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            tunerRing_[(size_t)(w & (kTunerRingSize - 1))] = tunerMuted ? 0.0f : rawIn[i];
            ++w;
        }
        tunerWrite_.store(w, std::memory_order_release);
    }

    float inPk = 0.0f;
    bool bypassEngine = false;
    const bool demoOn = demoOn_.load(std::memory_order_relaxed);
    const int slot = demoSlot_.load(std::memory_order_relaxed);
    if (demoOn && demoLive_.load(std::memory_order_relaxed)) {
        // Live audition (real device): feed the DRY DI loop into the engine
        // as if it were the guitar input — real-time inference, no waiting.
        // Raw atomic pointer read (lock-free; shared_ptr atomics take a
        // mutex on libc++). The owner side keeps the buffer alive until two
        // device blocks complete after a re-publish (retiredDemos_).
        const auto* loopPtr =
            demoTracksRT_[(size_t)demoTrackRT_.load(std::memory_order_relaxed)].load(
                std::memory_order_acquire);
        if (loopPtr != nullptr && !loopPtr->empty()) {
            const auto& loop = *loopPtr;
            size_t pos = demoPos_.load(std::memory_order_relaxed);
            for (int i = 0; i < n && i < cap; ++i) {
                if (pos >= loop.size()) pos = 0;
                const float v = loop[pos++];
                mono_[(size_t)i] = v;
                inPk = juce::jmax(inPk, std::fabs(v));
            }
            demoPos_.store(pos, std::memory_order_relaxed);
        } else {
            for (int i = 0; i < n && i < cap; ++i) mono_[(size_t)i] = 0.0f;
        }
    } else if (const auto* slotPtr = (demoOn && slot >= 0) ? demoSlotsRT_[(size_t)slot].load(
                                                                 std::memory_order_acquire)
                                                           : nullptr;
               slotPtr != nullptr && !slotPtr->empty()) {
        // Audition: play back the offline-rendered (model-processed) riff.
        // No inference on the audio thread; lock-free raw pointer read.
        const auto& loop = *slotPtr;
        size_t pos = demoPos_.load(std::memory_order_relaxed);
        for (int i = 0; i < n && i < cap; ++i) {
            if (pos >= loop.size()) pos = 0;
            const float v = loop[pos++];
            mono_[(size_t)i] = v;
            inPk = juce::jmax(inPk, std::fabs(v));
        }
        demoPos_.store(pos, std::memory_order_relaxed);
        bypassEngine = true;   // slot is already model-processed
    } else if (liveMuted_.load(std::memory_order_relaxed) ||
               inputMutedUser_.load(std::memory_order_relaxed) ||
               feedbackGuard_.load(std::memory_order_relaxed)) {
        // No demo playing and the live path is muted (emulator, the status-
        // orb input toggle, or the no-interface feedback guard): the engine
        // gets silence. Also bypass it — amp models synthesise their own
        // noise floor even on silent input, and inference costs CPU.
        // Guard-only mute still METERS the raw mic so the input arc and
        // tuner stay live (demo playback through the speaker is unaffected).
        if (feedbackGuard_.load(std::memory_order_relaxed) &&
            !inputMutedUser_.load(std::memory_order_relaxed) &&
            !liveMuted_.load(std::memory_order_relaxed) && buf->getNumChannels() > 0) {
            const float* in = buf->getReadPointer(0, info.startSample);
            for (int i = 0; i < n && i < cap; ++i) inPk = juce::jmax(inPk, std::fabs(in[i]));
        }
        for (int i = 0; i < n && i < cap; ++i) mono_[(size_t)i] = 0.0f;
        bypassEngine = true;
    } else {
        const float* in = buf->getReadPointer(0, info.startSample);
        for (int i = 0; i < n && i < cap; ++i) {
            mono_[(size_t)i] = in[i];
            inPk = juce::jmax(inPk, std::fabs(in[i]));
        }
    }
    inPeak_.store(inPk, std::memory_order_relaxed);

    if (!bypassEngine) engine_.render(mono_.data(), mono_.data(), std::min(n, cap));

    // Output mute is the user's alone — the feedback guard silences the
    // live INPUT path instead, so demo playback keeps working without an
    // interface connected.
    const bool outMuted = outputMutedUser_.load(std::memory_order_relaxed);
    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        // Write the WHOLE block: an oversized device burst (n > cap) must
        // not pass its tail through untouched — on a duplex stream those
        // samples are raw mic input, bypassing every mute.
        for (int i = 0; i < n; ++i) out[i] = (outMuted || i >= cap) ? 0.0f : mono_[(size_t)i];
    }

    // Output-check tone: a plain 440 Hz sine ADDED to the device output
    // (post-chain, respects the output mute). Envelope avoids clicks; both
    // state vars are audio-thread-only, the on/off flag is the atomic.
    const bool testOn = testTone_.load(std::memory_order_relaxed);
    if (!outMuted && (testOn || testEnv_ > 0.0001f)) {
        const double inc =
            2.0 * juce::MathConstants<double>::pi * 440.0 / juce::jmax(1.0, sampleRate_);
        const float target = testOn ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i) {
            testEnv_ += (target - testEnv_) * 0.002f;
            const float s = 0.16f * testEnv_ * (float)std::sin(testPhase_);
            testPhase_ += inc;
            if (testPhase_ > 2.0 * juce::MathConstants<double>::pi)
                testPhase_ -= 2.0 * juce::MathConstants<double>::pi;
            for (int ch = 0; ch < buf->getNumChannels(); ++ch)
                buf->getWritePointer(ch, info.startSample)[i] += s;
        }
    } else {
        testEnv_ = 0.0f;
        testPhase_ = 0.0;
    }

    // Meter the signal actually leaving the app. The engine's own peak
    // telemetry freezes whenever the engine is bypassed (demo playback,
    // mutes), so the output arc must tap the device write instead.
    float outPk = 0.0f;
    if (!outMuted) {
        for (int i = 0; i < n && i < cap; ++i)
            outPk = juce::jmax(outPk, std::fabs(mono_[(size_t)i]));
        outPk = juce::jmax(outPk, 0.16f * testEnv_);
    }
    outPeak_.store(outPk, std::memory_order_relaxed);

    // RELEASE: the message thread's acquire load gates freeing retired demo
    // buffers on this counter — the edge is what makes that free safe.
    appBlocks_.fetch_add(1, std::memory_order_release);
    inCallback_.store(false, std::memory_order_release);
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    if (shell_ != nullptr) {
        shell_->setLevels(inPeak_.load(std::memory_order_relaxed),
                          outPeak_.load(std::memory_order_relaxed));
        // Orb reflects the user's mute toggles (the feedback guard is a
        // monitoring-path protection, not a user state).
        shell_->setIoMuted(inputMutedUser_.load(std::memory_order_relaxed),
                           outputMutedUser_.load(std::memory_order_relaxed));
    }

    // Tuner analysis at ~10 Hz: copy the latest window out of the ring and
    // run pitch detection on the message thread. Piggyback the latency
    // readout on the same cadence.
    if (shell_ != nullptr && ++tunerTick_ >= 3) {
        tunerTick_ = 0;
        if (auto* dev = deviceManager.getCurrentAudioDevice()) {
            const double sr = dev->getCurrentSampleRate();
            if (sr > 0)
                shell_->setLatencyMs((dev->getInputLatencyInSamples() +
                                      dev->getOutputLatencyInSamples() +
                                      dev->getCurrentBufferSizeSamples()) *
                                     1000.0 / sr);
        }
        constexpr int kWin = 2048;
        static thread_local std::array<float, (size_t)kWin> win;
        const int w = tunerWrite_.load(std::memory_order_acquire);
        for (int i = 0; i < kWin; ++i)
            win[(size_t)i] = tunerRing_[(size_t)((w - kWin + i) & (kTunerRingSize - 1))];
        const float hz = dsp::detectPitchHz(win.data(), kWin, sampleRate_);
        shell_->setTunerPitch(hz);
    }

    // Audio watchdog: a USB flake can kill the duplex stream — JUCE stops
    // the device and nothing restarts it ("engine stopped", silence until
    // an app restart). If the device sits stopped for ~2 s, rebuild it.
    if (!applyingDeviceChange_) {
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (dev != nullptr && !dev->isPlaying()) {
            if (++engineDeadTicks_ == 60) {
                engineDeadTicks_ = 0;
                juce::Logger::writeToLog("audio watchdog: device stopped -> rescan");
                rescanAudioDevices();
            }
        } else {
            engineDeadTicks_ = 0;
        }
    }

    // Slow hot-plug poll (~every 3 s): Android doesn't notify JUCE when a USB
    // interface appears, so rescan until one is adopted or the user picks.
    if (++rescanTick_ >= 90) {
        rescanTick_ = 0;
        preferUsbInput();   // per-side manual picks respected internally
    }
}

// --- Audio device selection -----------------------------------------------
juce::StringArray AndroidAudioApp::inputDeviceNames() const {
    if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
        type->scanForDevices();
        return type->getDeviceNames(true);
    }
    return {};
}

juce::StringArray AndroidAudioApp::outputDeviceNames() const {
    if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
        type->scanForDevices();
        return type->getDeviceNames(false);
    }
    return {};
}

juce::String AndroidAudioApp::currentInputDevice() const {
    // An empty setup name means "the default device" — resolve it so the
    // picker can highlight the row that is actually live.
    auto name = deviceManager.getAudioDeviceSetup().inputDeviceName;
    if (name.isEmpty())
        if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
            const auto names = type->getDeviceNames(true);
            const int def = type->getDefaultDeviceIndex(true);
            if (def >= 0 && def < names.size()) name = names[def];
        }
    return name;
}

juce::String AndroidAudioApp::currentOutputDevice() const {
    auto name = deviceManager.getAudioDeviceSetup().outputDeviceName;
    if (name.isEmpty())
        if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
            const auto names = type->getDeviceNames(false);
            const int def = type->getDefaultDeviceIndex(false);
            if (def >= 0 && def < names.size()) name = names[def];
        }
    return name;
}

// Applies a device-setup change; on failure retries once letting the device
// pick its own sample rate / buffer size (an explicit-device stream can reject
// the rate the default stream was opened at).
juce::String AndroidAudioApp::applyDeviceSetup(juce::AudioDeviceManager::AudioDeviceSetup setup) {
    applyingDeviceChange_ = true;
    auto err = deviceManager.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty()) {
        setup.sampleRate = 0;
        setup.bufferSize = 0;
        err = deviceManager.setAudioDeviceSetup(setup, true);
    }
    applyingDeviceChange_ = false;
    return err;
}

void AndroidAudioApp::selectInputDevice(const juce::String& name) {
    userChoseInput_ = true;   // manual pick wins over USB auto-select
    updateFeedbackGuard();
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName == name) return;
    setup.inputDeviceName = name;
    setup.useDefaultInputChannels = true;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectInputDevice(" + name + ") -> " + (err.isEmpty() ? "ok" : err) +
                             " now=" + deviceManager.getAudioDeviceSetup().inputDeviceName);
}

void AndroidAudioApp::selectOutputDevice(const juce::String& name) {
    userChoseOutput_ = true;   // manual pick wins over USB auto-claim
    updateFeedbackGuard();
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.outputDeviceName == name) return;
    setup.outputDeviceName = name;
    setup.useDefaultOutputChannels = true;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectOutputDevice(" + name + ") -> " + (err.isEmpty() ? "ok" : err));
}

void AndroidAudioApp::rescanAudioDevices() {
    // JUCE's OboeAudioIODeviceType enumerates devices ONCE in its constructor
    // and its scanForDevices() is a no-op, so a USB interface plugged in after
    // launch never shows up. Recreating the type forces a fresh enumeration.
    auto setup = deviceManager.getAudioDeviceSetup();
    deviceManager.closeAudioDevice();
    if (auto* old = deviceManager.getCurrentDeviceTypeObject())
        deviceManager.removeAudioDeviceType(old);
    deviceManager.addAudioDeviceType(std::unique_ptr<juce::AudioIODeviceType>(
        juce::AudioIODeviceType::createAudioIODeviceType_Oboe()));
    deviceManager.setCurrentAudioDeviceType("Android Oboe", true);

    if (applyDeviceSetup(setup).isNotEmpty()) deviceManager.initialiseWithDefaultDevices(1, 2);

    if (!userChoseInput_) preferUsbInput();
    juce::Logger::writeToLog("rescanAudioDevices -> in=" + currentInputDevice() +
                             " out=" + currentOutputDevice());
}

void AndroidAudioApp::updateFeedbackGuard() {
    // Danger = live guitar path running phone mic -> speaker. True whenever
    // no USB interface is available and the user hasn't deliberately picked
    // devices (a manual pick means they know their monitoring chain).
    bool usbPresent = false;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        for (const auto& n : type->getDeviceNames(true))
            if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) {
                usbPresent = true;
                break;
            }
    const bool guard = !usbPresent && !userChoseInput_ && !userChoseOutput_ &&
                       !alwaysMuteLive_;   // emulator has its own mute
    if (feedbackGuard_.exchange(guard, std::memory_order_relaxed) != guard)
        juce::Logger::writeToLog(juce::String("feedback guard ") +
                                 (guard ? "ON (no interface: live path muted)"
                                        : "off (interface present or manual pick)"));
}

void AndroidAudioApp::preferUsbInput() {
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr) return;
    type->scanForDevices();

    juce::String usbIn, usbOut;
    for (const auto& n : type->getDeviceNames(true))
        if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) {
            usbIn = n;
            break;
        }
    for (const auto& n : type->getDeviceNames(false))
        if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) {
            usbOut = n;
            break;
        }
    updateFeedbackGuard();   // every path that (re)checks devices refreshes it
    if (usbIn.isEmpty() && usbOut.isEmpty()) return;

    // Claim BOTH sides of the guitar interface explicitly. "System Default"
    // output does reach the USB device on the faster MMAP path (96-frame
    // bursts vs Legacy's 192) — but default routing follows the most recent
    // device, so a Bluetooth connect (headphones, glasses) silently steals
    // the rig mid-song. An explicit device can't be stolen; the ~2-4 ms
    // Legacy cost is the price of immunity. Manual picks still win.
    auto setup = deviceManager.getAudioDeviceSetup();
    bool changed = false;
    if (usbIn.isNotEmpty() && !userChoseInput_ && setup.inputDeviceName != usbIn) {
        setup.inputDeviceName = usbIn;
        setup.useDefaultInputChannels = true;
        changed = true;
    }
    if (usbOut.isNotEmpty() && !userChoseOutput_ && setup.outputDeviceName != usbOut) {
        setup.outputDeviceName = usbOut;
        setup.useDefaultOutputChannels = true;
        changed = true;
    }
    if (!changed) return;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("preferUsb(in=" + usbIn + " out=" + usbOut + ") -> " +
                             (err.isEmpty() ? "ok" : err));
}

// --- Engine settings (sample rate / buffer / latency) ---------------------
namespace {
juce::String rateLabel(double sr) {
    if (sr <= 0) return "?";
    const double k = sr / 1000.0;
    return (std::fabs(k - std::round(k)) < 0.01 ? juce::String((int)std::round(k))
                                                : juce::String(k, 1)) +
           "k";
}
}   // namespace

AudioSettingsState AndroidAudioApp::audioSettingsState() {
    AudioSettingsState s;
    s.inputs = inputDeviceNames();
    s.outputs = outputDeviceNames();
    s.currentInput = currentInputDevice();
    s.currentOutput = currentOutputDevice();

    // "System Default" output follows the USB interface when one is plugged
    // in (Android routing policy — verified via audio patches). Surface that
    // so the checkmark on "System Default" doesn't read as "phone speaker".
    // We intentionally never select the USB output explicitly: doing so kicks
    // the stream off the AAudio FAST path and triples the burst size.
    if (s.currentOutput.containsIgnoreCase("default")) {
        for (const auto& name : s.outputs) {
            if (name.containsIgnoreCase("usb")) {
                s.outputRouteHint =
                    name.replace("USB-Audio - ", "").replace(" USB headset", "").trim();
                break;
            }
        }
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev != nullptr) {
        for (double r : dev->getAvailableSampleRates())
            if (r == 44100.0 || r == 48000.0 || r == 96000.0) s.rates.add(rateLabel(r));
        s.currentRate = rateLabel(dev->getCurrentSampleRate());
        if (!s.rates.contains(s.currentRate)) s.rates.add(s.currentRate);

        auto sizes = dev->getAvailableBufferSizes();
        const int cur = dev->getCurrentBufferSizeSamples();
        // Offer up to 4 sizes spanning small..large.
        for (int want : { 0, 1, 2, 3 }) {
            const int idx = sizes.size() <= 4 ? want : (want * (sizes.size() - 1)) / 3;
            if (idx >= 0 && idx < sizes.size()) {
                const auto label = juce::String(sizes[(int)idx]);
                if (!s.buffers.contains(label)) s.buffers.add(label);
            }
        }
        s.currentBuffer = juce::String(cur);
        if (!s.buffers.contains(s.currentBuffer)) s.buffers.add(s.currentBuffer);

        const double sr = dev->getCurrentSampleRate();
        if (sr > 0)
            s.latencyMs =
                (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples() + cur) *
                1000.0 / sr;
        s.running = dev->isPlaying();
    }
    return s;
}

void AndroidAudioApp::selectSampleRate(const juce::String& label) {
    const double sr = label.upToFirstOccurrenceOf("k", false, true).getDoubleValue() * 1000.0;
    if (sr <= 0) return;
    auto setup = deviceManager.getAudioDeviceSetup();
    if (std::fabs(setup.sampleRate - sr) < 1.0) return;
    setup.sampleRate = sr;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectSampleRate(" + label + ") -> " + (err.isEmpty() ? "ok" : err));
}

void AndroidAudioApp::selectBufferSize(const juce::String& label) {
    const int frames = label.getIntValue();
    if (frames <= 0) return;
    userChoseBuffer_ = true;   // manual pick wins over the low-latency clamp
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.bufferSize == frames) return;
    setup.bufferSize = frames;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectBufferSize(" + label + ") -> " + (err.isEmpty() ? "ok" : err));
}

void AndroidAudioApp::changeListenerCallback(juce::ChangeBroadcaster*) {
    // Device setup changed (stream restart etc.) — re-check USB routing unless
    // we caused the change ourselves or the user made an explicit choice.
    if (applyingDeviceChange_) return;
    preferUsbInput();   // guards per-side manual picks internally
    clampBufferForLowLatency();
}

void AndroidAudioApp::clampBufferForLowLatency() {
    if (userChoseBuffer_) return;
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return;
    const auto sizes = dev->getAvailableBufferSizes();
    if (sizes.isEmpty()) return;
    int smallest = sizes[0];
    for (int s : sizes) smallest = juce::jmin(smallest, s);
    smallest = juce::jmax(96, smallest);
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.bufferSize > 0 && setup.bufferSize <= smallest) return;
    setup.bufferSize = smallest;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("clampBufferForLowLatency(" + juce::String(smallest) + ") -> " +
                             (err.isEmpty() ? "ok" : err));
}

bool AndroidAudioApp::handleBackButton() { return shell_ != nullptr && shell_->handleBackButton(); }

void AndroidAudioApp::paint(juce::Graphics& g) { g.fillAll(nam::ui::col::bg); }

void AndroidAudioApp::resized() {
    if (shell_ == nullptr) return;
    // Inset the UI by the system safe area (status bar top, nav bar bottom) so
    // our own controls never sit under the OS bars. The app background paints
    // the full window behind the (translucent) bars.
    const auto insets =
        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->safeAreaInsets;
    shell_->setBounds(insets.subtractedFrom(getLocalBounds()));
}
