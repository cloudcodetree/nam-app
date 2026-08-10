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
    if ((__system_property_get("ro.boot.qemu", qemu) > 0 && qemu[0] == '1')
        || (__system_property_get("ro.kernel.qemu", qemu) > 0 && qemu[0] == '1')) {
        liveMuted_.store(true, std::memory_order_relaxed);
        alwaysMuteLive_ = true;
        preRenderAuditions_ = true;   // QEMU can't run NAM inference in real time
    }

    library_.load();
    t3kAuth_.loadTokens();   // reuse a prior refresh token if present

    shell_ = std::make_unique<AppShell>(engine_);
    addAndMakeVisible(*shell_);
    AppShell::BrowseServices browse;
    browse.search = [this](juce::String q,
            std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
        doSearch(std::move(q), std::move(done));
    };
    browse.keep = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doToggleKeep(std::move(t), std::move(done));
    };
    browse.isKept = [this](std::string toneId) {
        return ! libraryIdForTone(toneId).empty();
    };
    browse.libraryIdForTone = [this](std::string toneId) {
        return libraryIdForTone(toneId);
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
            if (a.contains("slim") || a.startsWith("2") || a.startsWith("a2"))
                t.a2Count = 1;
            else
                t.a1Count = 1;
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
    browse.listModels = [this](std::string toneId,
            std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done) {
        doListModels(toneId, std::move(done));
    };
    browse.setDemoTrack = [this](int t, std::function<void(bool)> done) {
        ensureDemoTrack(t, [this, t, done](bool ok) {
            if (ok) setDemoTrack(t);   // RT switch only after audio is loaded
            done(ok);
        });
    };
    browse.stopDemo     = [this] { setDemoActive(false); };
    browse.setCab       = [this](int c) { setCab(c); };
    browse.isAuditionCached = [this](std::string toneId) {
        if (preRenderAuditions_)
            return cachedAudition(toneId + "#best#" + std::to_string(demoTrack_)
                                  + "#c" + std::to_string(cab_)) != nullptr;
        // Live mode: any local file means instant audition, any riff.
        return modelCacheFile("keep_" + toneId).existsAsFile()
            || modelCacheFile("auto_" + toneId).existsAsFile()
            || modelCacheFile("ir_" + toneId).existsAsFile();
    };
    browse.isDownloaded = [](std::string toneId) {
        return modelCacheFile("keep_" + toneId).existsAsFile()
            || modelCacheFile("ir_" + toneId).existsAsFile();
    };
    shell_->setBrowseServices(std::move(browse));
    shell_->setLibraryService(
        [this] { return library_.all(nam::LibraryType::Model); },
        [this](nam::LibraryEntry e) { loadModelEntry(e); });
    shell_->setMuteService(
        [this](bool m) { inputMutedUser_.store(m, std::memory_order_relaxed); },
        [this](bool m) { outputMutedUser_.store(m, std::memory_order_relaxed); });
    shell_->setArtworkService([](const nam::LibraryEntry& e) {
        const auto id = toneIdFromEntry(e);
        return id.empty() ? juce::Image()
                          : juce::ImageFileFormat::loadFrom(artworkFile(id));
    });
    shell_->setAudioDeviceService(
        [this] { return audioSettingsState(); },
        [this](juce::String name) { selectInputDevice(name); },
        [this](juce::String name) { selectOutputDevice(name); },
        [this] { rescanAudioDevices(); },
        [this](juce::String label) { selectSampleRate(label); },
        [this](juce::String label) { selectBufferSize(label); });

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
    if (library_.all(nam::LibraryType::Model).empty()
        && ! defaultToneFile().existsAsFile())
        juce::MessageManager::callAsync([this] { fetchPopularDefault(); });
}

AndroidAudioApp::~AndroidAudioApp() {
    stopTimer();
    deviceManager.removeChangeListener(this);
    shutdownAudio();
    setLookAndFeel(nullptr);
}

std::string AndroidAudioApp::copyBundledModelToFile() {
    auto dest = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("model.nam");
    if (! dest.existsAsFile()) {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource("model_nam", size))
            dest.replaceWithData(data, (size_t) size);
    }
    return dest.getFullPathName().toStdString();
}

void AndroidAudioApp::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRate_ = sampleRate;
    blockSize_  = samplesPerBlockExpected;
    mono_.assign((size_t) juce::jmax(1, samplesPerBlockExpected), 0.0f);
    engine_.prepare((int) sampleRate, samplesPerBlockExpected);
    buildDemoLoop(sampleRate);

    // Bundled cab IRs (IrLoader wants a path; stage each through a temp file).
    for (int c = 1; c < nam::demo::kNumCabs; ++c) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(nam::demo::kCabs[c].binaryResource, size);
        if (data == nullptr || size <= 0) continue;
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("cab_" + juce::String(c) + ".wav");
        tmp.replaceWithData(data, (size_t) size);
        cabIrs_[(size_t) c] = nam::loadImpulseResponse(
            tmp.getFullPathName().toStdString(), (int) sampleRate, dsp::kMaxIrTaps);
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
            if (auto m = nam::NamModel::load(p, (int) sampleRate, samplesPerBlockExpected)) {
                juce::Logger::writeToLog("prepare: last-used model '" + juce::String(recent->displayName) + "' ok");
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
        if (auto m = nam::NamModel::load(defFile.getFullPathName().toStdString(),
                                         (int) sampleRate, samplesPerBlockExpected)) {
            juce::String name ("Popular Tone");
            if (defaultToneNameFile().existsAsFile())
                name = defaultToneNameFile().loadFileAsString().trim();
            juce::Logger::writeToLog("prepare: popular default '" + name + "' ok");
            engine_.setModel(std::move(m));
            modelLoaded_ = true;
            juce::MessageManager::callAsync([this, name] {
                if (shell_ != nullptr)
                    shell_->setNowPlayingInfo(name, "TONE3000 " + juce::String::fromUTF8("\xC2\xB7")
                                                    + " MOST KEPT");
            });
            return;
        }
    }

    const auto bundledPath = copyBundledModelToFile();
    if (auto m = nam::NamModel::load(bundledPath, (int) sampleRate, samplesPerBlockExpected)) {
        juce::Logger::writeToLog("prepare: bundled model ok (sr=" + juce::String(sampleRate)
                                 + ", block=" + juce::String(samplesPerBlockExpected)
                                 + ", normAdj=" + juce::String(m->recommendedOutputDbAdjustment(), 1) + " dB)");
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
    } else {
        juce::Logger::writeToLog("prepare: bundled model FAILED (path=" + juce::String(bundledPath)
                                 + ", exists=" + juce::String(juce::File(juce::String(bundledPath)).existsAsFile() ? "yes" : "no")
                                 + ", sr=" + juce::String(sampleRate)
                                 + ", block=" + juce::String(samplesPerBlockExpected) + ")");
    }
}

void AndroidAudioApp::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    auto* buf = info.buffer;
    const int n = info.numSamples;
    const int cap = (int) mono_.size();

    // Mirror the raw input into the tuner ring regardless of demo state —
    // the tuner always listens to the guitar, never to demo playback.
    {
        const float* rawIn = buf->getReadPointer(0, info.startSample);
        int w = tunerWrite_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            tunerRing_[(size_t) (w & (kTunerRingSize - 1))] = rawIn[i];
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
        const auto& loop = demoTracks_[(size_t) demoTrackRT_.load(std::memory_order_relaxed)];
        if (! loop.empty()) {
            size_t pos = demoPos_.load(std::memory_order_relaxed);
            for (int i = 0; i < n && i < cap; ++i) {
                if (pos >= loop.size()) pos = 0;
                const float v = loop[pos++];
                mono_[(size_t) i] = v;
                inPk = juce::jmax(inPk, std::fabs(v));
            }
            demoPos_.store(pos, std::memory_order_relaxed);
        } else {
            for (int i = 0; i < n && i < cap; ++i) mono_[(size_t) i] = 0.0f;
        }
    } else if (demoOn && slot >= 0
        && ! demoSlots_[(size_t) slot].empty()) {
        // Audition: play back the offline-rendered (model-processed) riff.
        // No inference on the audio thread.
        const auto& loop = demoSlots_[(size_t) slot];
        size_t pos = demoPos_.load(std::memory_order_relaxed);
        for (int i = 0; i < n && i < cap; ++i) {
            if (pos >= loop.size()) pos = 0;
            const float v = loop[pos++];
            mono_[(size_t) i] = v;
            inPk = juce::jmax(inPk, std::fabs(v));
        }
        demoPos_.store(pos, std::memory_order_relaxed);
        bypassEngine = true;   // slot is already model-processed
    } else if (liveMuted_.load(std::memory_order_relaxed)
               || inputMutedUser_.load(std::memory_order_relaxed)) {
        // No demo playing and live input muted (emulator or the status-orb
        // input toggle): true silence.
        // Also bypass the engine — amp models synthesise their own noise
        // floor (hiss/hum) even on silent input, and inference costs CPU.
        for (int i = 0; i < n && i < cap; ++i) mono_[(size_t) i] = 0.0f;
        bypassEngine = true;
    } else {
        const float* in = buf->getReadPointer(0, info.startSample);
        for (int i = 0; i < n && i < cap; ++i) {
            mono_[(size_t) i] = in[i];
            inPk = juce::jmax(inPk, std::fabs(in[i]));
        }
    }
    inPeak_.store(inPk, std::memory_order_relaxed);

    if (! bypassEngine)
        engine_.render(mono_.data(), mono_.data(), std::min(n, cap));

    // Output mute: the user toggle (status orb) or the feedback guard (no
    // interface -> mic through amp into speaker howls). Muting OUTPUT keeps
    // the input meter/tuner alive so signal is still visible.
    const bool outMuted = outputMutedUser_.load(std::memory_order_relaxed)
                       || feedbackGuard_.load(std::memory_order_relaxed);
    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        for (int i = 0; i < n && i < cap; ++i)
            out[i] = outMuted ? 0.0f : mono_[(size_t) i];
    }
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    if (shell_ != nullptr) {
        shell_->setLevels(inPeak_.load(std::memory_order_relaxed),
                          engine_.outputPeak());
        // Orb reflects the effective mute state (guard counts as out-muted).
        shell_->setIoMuted(inputMutedUser_.load(std::memory_order_relaxed),
                           outputMutedUser_.load(std::memory_order_relaxed)
                               || feedbackGuard_.load(std::memory_order_relaxed));
    }

    // Tuner analysis at ~10 Hz: copy the latest window out of the ring and
    // run pitch detection on the message thread. Piggyback the latency
    // readout on the same cadence.
    if (shell_ != nullptr && ++tunerTick_ >= 3) {
        tunerTick_ = 0;
        if (auto* dev = deviceManager.getCurrentAudioDevice()) {
            const double sr = dev->getCurrentSampleRate();
            if (sr > 0)
                shell_->setLatencyMs ((dev->getInputLatencyInSamples()
                                       + dev->getOutputLatencyInSamples()
                                       + dev->getCurrentBufferSizeSamples()) * 1000.0 / sr);
        }
        constexpr int kWin = 2048;
        static thread_local std::array<float, (size_t) kWin> win;
        const int w = tunerWrite_.load(std::memory_order_acquire);
        for (int i = 0; i < kWin; ++i)
            win[(size_t) i] = tunerRing_[(size_t) ((w - kWin + i) & (kTunerRingSize - 1))];
        const float hz = dsp::detectPitchHz(win.data(), kWin, sampleRate_);
        shell_->setTunerPitch(hz);
    }

    // Audio watchdog: a USB flake can kill the duplex stream — JUCE stops
    // the device and nothing restarts it ("engine stopped", silence until
    // an app restart). If the device sits stopped for ~2 s, rebuild it.
    if (! applyingDeviceChange_) {
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (dev != nullptr && ! dev->isPlaying()) {
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
    juce::Logger::writeToLog("selectInputDevice(" + name + ") -> "
                             + (err.isEmpty() ? "ok" : err)
                             + " now=" + deviceManager.getAudioDeviceSetup().inputDeviceName);
}

void AndroidAudioApp::selectOutputDevice(const juce::String& name) {
    userChoseOutput_ = true;   // manual pick wins over USB auto-claim
    updateFeedbackGuard();
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.outputDeviceName == name) return;
    setup.outputDeviceName = name;
    setup.useDefaultOutputChannels = true;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectOutputDevice(" + name + ") -> "
                             + (err.isEmpty() ? "ok" : err));
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

    if (applyDeviceSetup(setup).isNotEmpty())
        deviceManager.initialiseWithDefaultDevices(1, 2);

    if (! userChoseInput_) preferUsbInput();
    juce::Logger::writeToLog("rescanAudioDevices -> in=" + currentInputDevice()
                             + " out=" + currentOutputDevice());
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
    const bool guard = ! usbPresent && ! userChoseInput_ && ! userChoseOutput_
                       && ! alwaysMuteLive_;   // emulator has its own mute
    if (feedbackGuard_.exchange(guard, std::memory_order_relaxed) != guard)
        juce::Logger::writeToLog(juce::String("feedback guard ")
                                 + (guard ? "ON (no interface: live path muted)"
                                          : "off (interface present or manual pick)"));
}

void AndroidAudioApp::preferUsbInput() {
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr) return;
    type->scanForDevices();

    juce::String usbIn, usbOut;
    for (const auto& n : type->getDeviceNames(true))
        if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) { usbIn = n; break; }
    for (const auto& n : type->getDeviceNames(false))
        if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) { usbOut = n; break; }
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
    if (usbIn.isNotEmpty() && ! userChoseInput_ && setup.inputDeviceName != usbIn) {
        setup.inputDeviceName = usbIn;
        setup.useDefaultInputChannels = true;
        changed = true;
    }
    if (usbOut.isNotEmpty() && ! userChoseOutput_ && setup.outputDeviceName != usbOut) {
        setup.outputDeviceName = usbOut;
        setup.useDefaultOutputChannels = true;
        changed = true;
    }
    if (! changed) return;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("preferUsb(in=" + usbIn + " out=" + usbOut + ") -> "
                             + (err.isEmpty() ? "ok" : err));
}

// --- Engine settings (sample rate / buffer / latency) ---------------------
namespace {
juce::String rateLabel(double sr) {
    if (sr <= 0) return "?";
    const double k = sr / 1000.0;
    return (std::fabs(k - std::round(k)) < 0.01
                ? juce::String((int) std::round(k)) : juce::String(k, 1)) + "k";
}
}

AudioSettingsState AndroidAudioApp::audioSettingsState() {
    AudioSettingsState s;
    s.inputs  = inputDeviceNames();
    s.outputs = outputDeviceNames();
    s.currentInput  = currentInputDevice();
    s.currentOutput = currentOutputDevice();

    // "System Default" output follows the USB interface when one is plugged
    // in (Android routing policy — verified via audio patches). Surface that
    // so the checkmark on "System Default" doesn't read as "phone speaker".
    // We intentionally never select the USB output explicitly: doing so kicks
    // the stream off the AAudio FAST path and triples the burst size.
    if (s.currentOutput.containsIgnoreCase("default")) {
        for (const auto& name : s.outputs) {
            if (name.containsIgnoreCase("usb")) {
                s.outputRouteHint = name.replace("USB-Audio - ", "")
                                        .replace(" USB headset", "").trim();
                break;
            }
        }
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev != nullptr) {
        for (double r : dev->getAvailableSampleRates())
            if (r == 44100.0 || r == 48000.0 || r == 96000.0)
                s.rates.add(rateLabel(r));
        s.currentRate = rateLabel(dev->getCurrentSampleRate());
        if (! s.rates.contains(s.currentRate)) s.rates.add(s.currentRate);

        auto sizes = dev->getAvailableBufferSizes();
        const int cur = dev->getCurrentBufferSizeSamples();
        // Offer up to 4 sizes spanning small..large.
        for (int want : { 0, 1, 2, 3 }) {
            const int idx = sizes.size() <= 4 ? want
                            : (want * (sizes.size() - 1)) / 3;
            if (idx >= 0 && idx < sizes.size()) {
                const auto label = juce::String(sizes[(int) idx]);
                if (! s.buffers.contains(label)) s.buffers.add(label);
            }
        }
        s.currentBuffer = juce::String(cur);
        if (! s.buffers.contains(s.currentBuffer)) s.buffers.add(s.currentBuffer);

        const double sr = dev->getCurrentSampleRate();
        if (sr > 0)
            s.latencyMs = (dev->getInputLatencyInSamples()
                           + dev->getOutputLatencyInSamples() + cur) * 1000.0 / sr;
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
    juce::Logger::writeToLog("clampBufferForLowLatency(" + juce::String(smallest)
                             + ") -> " + (err.isEmpty() ? "ok" : err));
}

bool AndroidAudioApp::handleBackButton() {
    return shell_ != nullptr && shell_->handleBackButton();
}

void AndroidAudioApp::paint(juce::Graphics& g) { g.fillAll(nam::ui::col::bg); }

void AndroidAudioApp::resized() {
    if (shell_ == nullptr) return;
    // Inset the UI by the system safe area (status bar top, nav bar bottom) so
    // our own controls never sit under the OS bars. The app background paints
    // the full window behind the (translucent) bars.
    const auto insets = juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay()->safeAreaInsets;
    shell_->setBounds(insets.subtractedFrom(getLocalBounds()));
}

// --- TONE3000 -------------------------------------------------------------
juce::File AndroidAudioApp::tokenStoreFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/tone3000_tokens.json");
}

std::string AndroidAudioApp::defaultLibraryDir() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/library").getFullPathName().toStdString();
}

long long AndroidAudioApp::nowSeconds() {
    using namespace std::chrono;
    return (long long) duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void AndroidAudioApp::doSearch(juce::String query,
        std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
    if (! t3kAuth_.isConfigured()) { done(false, {}, "not configured (.env key missing)"); return; }

    // Runs the actual search with a fresh session built from the current token.
    // Backfill artwork for tones kept before art caching existed (or whose
    // download failed): any kept result missing its file gets a quiet fetch.
    auto withBackfill = [this, done](bool ok, std::vector<nam::ToneInfo> tones,
                                     juce::String err) {
        if (ok)
            for (const auto& t : tones)
                if (! libraryIdForTone(t.id).empty()) fetchArtwork(t);
        done(ok, std::move(tones), std::move(err));
    };

    auto run = [this, query, withBackfill] {
        t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());
        t3kSession_->search(query.toStdString(), 1, withBackfill);
    };

    if (t3kAuth_.hasValidToken()) { run(); return; }

    // No valid token: try a silent refresh, else the browser Connect flow
    // (loopback OAuth — the on-device browser redirects back to 127.0.0.1).
    t3kAuth_.tryRefresh([this, run, done](bool refreshed) {
        if (refreshed) { run(); return; }
        t3kAuth_.beginConnectFlow([run, done](nam::Tone3000Auth::Result r) {
            if (! r.ok) { done(false, {}, juce::String(r.error)); return; }
            run();
        });
    });
}

void AndroidAudioApp::loadModelEntry(const nam::LibraryEntry& e) {
    const std::string path = library_.subdir(nam::LibraryType::Model) + "/" + e.fileName;
    auto m = nam::NamModel::load(path, (int) sampleRate_, blockSize_);
    juce::Logger::writeToLog("loadModelEntry '" + juce::String(e.displayName) + "' "
                             + (m != nullptr ? "ok" : "FAILED")
                             + " (file " + (juce::File(juce::String(path)).existsAsFile() ? "exists" : "MISSING")
                             + ", sr=" + juce::String(sampleRate_)
                             + ", block=" + juce::String(blockSize_) + ")");
    if (m != nullptr) {
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
        library_.markUsed(e.id, nowSeconds());
        library_.save();
    }
}

// --- Audition (demo riffs) ------------------------------------------------
// Pre-renders three short looping dry-guitar riffs via Karplus-Strong
// plucked strings: open chords, a pentatonic lead, and palm-muted chugs.
// Runs at prepare time (message thread); the audio thread only reads the
// finished buffers.
namespace {
struct DemoNote { double freq, t; float amp; double ring; float decay; };

void buildKsLoop(std::vector<float>& loop, double sr, double loopSec,
                 const DemoNote* notes, size_t count) {
    loop.assign((size_t) (sr * loopSec), 0.0f);
    juce::Random rng(7);
    for (size_t k = 0; k < count; ++k) {
        const auto& n = notes[k];
        const int start = (int) (n.t * sr);
        const int N = juce::jmax(2, (int) (sr / n.freq));
        std::vector<float> line((size_t) N);
        for (auto& v : line) v = rng.nextFloat() * 2.0f - 1.0f;
        const int len = (int) (sr * n.ring);
        int idx = 0;
        for (int i = 0; i < len && start + i < (int) loop.size(); ++i) {
            const int j = (idx + 1) % N;
            const float out = 0.5f * (line[(size_t) idx] + line[(size_t) j]) * n.decay;
            line[(size_t) idx] = out;
            idx = j;
            loop[(size_t) (start + i)] += out * n.amp * 0.35f;
        }
    }
    for (auto& v : loop) v = std::tanh(v);   // gentle safety clip
}
}

namespace {
// Decodes a WAV byte blob to mono floats at `sr`, trimmed to 12 s max.
bool decodeDiWav(const void* data, size_t size, double sr, std::vector<float>& outMono) {
    unsigned int ch = 0, fileSr = 0;
    drwav_uint64 frames = 0;
    float* raw = drwav_open_memory_and_read_pcm_frames_f32(
        data, size, &ch, &fileSr, &frames, nullptr);
    if (raw == nullptr || ch < 1 || frames == 0) {
        if (raw != nullptr) drwav_free(raw, nullptr);
        return false;
    }
    const drwav_uint64 maxFrames = (drwav_uint64) ((double) fileSr * 12.0);
    frames = juce::jmin(frames, maxFrames);
    std::vector<float> mono((size_t) frames);
    for (drwav_uint64 i = 0; i < frames; ++i)
        mono[(size_t) i] = raw[i * ch];
    drwav_free(raw, nullptr);
    if ((double) fileSr != sr && fileSr > 0) {
        const double ratio = (double) fileSr / sr;
        std::vector<float> res((size_t) ((double) frames / ratio));
        for (size_t i = 0; i < res.size(); ++i) {
            const double pos = (double) i * ratio;
            const size_t i0 = (size_t) pos;
            const float frac = (float) (pos - (double) i0);
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
}

void AndroidAudioApp::buildDemoLoop(double sr) {
    // Bundled tracks decode from BinaryData; fetched tracks reload lazily
    // (ensureDemoTrack) from the disk cache at the current rate. Clear
    // everything first — a device-rate change invalidates old buffers.
    bool allDecoded = true;
    for (int t = 0; t < nam::demo::kNumTracks; ++t) {
        demoTracks_[(size_t) t].clear();
        const char* res = nam::demo::kTracks[t].binaryResource;
        if (res == nullptr) continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource(res, size);
        if (data == nullptr || size <= 0
            || ! decodeDiWav(data, (size_t) size, sr, demoTracks_[(size_t) t]))
            allDecoded = false;
    }
    if (allDecoded) return;

    // 0: open chords in E minor (the original riff).
    static const DemoNote chords[] = {
        { 82.41, 0.0,  0.95f, 1.4, 0.996f }, { 98.00, 0.4,  0.70f, 1.4, 0.996f },
        { 110.0, 0.8,  0.80f, 1.4, 0.996f }, { 82.41, 1.2,  0.95f, 1.4, 0.996f },
        { 123.47, 1.6, 0.70f, 1.4, 0.996f }, { 110.0, 2.0,  0.80f, 1.4, 0.996f },
        { 98.00, 2.4,  0.70f, 1.4, 0.996f }, { 82.41, 2.8,  0.95f, 1.4, 0.996f },
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
        { 82.41, 0.0,  0.95f, 0.12, 0.960f }, { 82.41, 0.2,  0.85f, 0.12, 0.960f },
        { 82.41, 0.4,  0.90f, 0.12, 0.960f }, { 82.41, 0.6,  0.85f, 0.12, 0.960f },
        { 82.41, 0.8,  1.00f, 0.55, 0.992f },   // open accent
        { 82.41, 1.2,  0.90f, 0.12, 0.960f }, { 82.41, 1.4,  0.85f, 0.12, 0.960f },
        { 98.00, 1.6,  0.95f, 0.30, 0.985f },   // G2 stab
        { 82.41, 2.0,  0.90f, 0.12, 0.960f }, { 82.41, 2.2,  0.85f, 0.12, 0.960f },
        { 110.0, 2.4,  0.95f, 0.40, 0.988f },   // A2 stab
        { 82.41, 2.8,  0.95f, 0.12, 0.960f }, { 82.41, 3.0,  0.85f, 0.12, 0.960f },
    };
    buildKsLoop(demoTracks_[0], sr, 3.2, chords, std::size(chords));
    buildKsLoop(demoTracks_[1], sr, 3.2, lead,   std::size(lead));
    buildKsLoop(demoTracks_[2], sr, 3.2, chugs,  std::size(chugs));
    demoTracks_[3] = demoTracks_[0];   // bass fallback: reuse chords synth
}

void AndroidAudioApp::setDemoTrack(int index) {
    demoTrack_ = juce::jlimit(0, nam::demo::kNumTracks - 1, index);
    demoTrackRT_.store(demoTrack_, std::memory_order_relaxed);
}

void AndroidAudioApp::setCab(int index) {
    cab_ = juce::jlimit(0, nam::demo::kNumCabs - 1, index);
    const auto ir = cabIrs_[(size_t) cab_];
    engine_.setImpulse(ir);
    engine_.setIrEnabled(cab_ > 0 && ir != nullptr);
}

void AndroidAudioApp::ensureDemoTrack(int index, std::function<void(bool)> done) {
    if (index < 0 || index >= nam::demo::kNumTracks) { done(false); return; }
    if (! demoTracks_[(size_t) index].empty()) { done(true); return; }

    const double sr = sampleRate_;
    const auto cache = diCacheFile(index);
    const juce::String fileName (nam::demo::kTracks[index].fileName);

    juce::Thread::launch([this, index, sr, cache, fileName, done] {
        juce::MemoryBlock bytes;
        if (cache.existsAsFile()) {
            cache.loadFileAsData(bytes);
        } else {
            // MIT-licensed DI from TONE3000's web-player repo.
            const juce::URL url ("https://raw.githubusercontent.com/tone-3000/"
                                 "neural-amp-modeler-wasm/main/ui/public/inputs/"
                                 + juce::URL::addEscapeChars(fileName, false));
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
        const bool ok = bytes.getSize() > 1024
                        && decodeDiWav(bytes.getData(), bytes.getSize(), sr, *mono);
        juce::MessageManager::callAsync([this, index, mono, ok, done] {
            if (ok) demoTracks_[(size_t) index] = std::move(*mono);
            done(ok && ! demoTracks_[(size_t) index].empty());
        });
    });
}

void AndroidAudioApp::setDemoActive(bool on) {
    demoPos_.store(0, std::memory_order_relaxed);
    demoOn_.store(on, std::memory_order_relaxed);
    if (! on) demoLive_.store(false, std::memory_order_relaxed);
}

void AndroidAudioApp::setLiveInputMuted(bool muted) {
    liveMuted_.store(muted || alwaysMuteLive_, std::memory_order_relaxed);
}

void AndroidAudioApp::installRenderedDemo(std::vector<float> rendered, bool preservePosition) {
    const int next = (demoSlot_.load(std::memory_order_relaxed) + 1) & 1;
    const size_t len = rendered.size();
    demoSlots_[(size_t) next] = std::move(rendered);
    // Model/cab switches keep the demo rolling from the same spot (the DI
    // timeline is identical); anything else starts from the top.
    const bool wasPlaying = demoOn_.load(std::memory_order_relaxed);
    if (! (preservePosition && wasPlaying) || len == 0)
        demoPos_.store(0, std::memory_order_relaxed);
    else
        demoPos_.store(demoPos_.load(std::memory_order_relaxed) % len,
                       std::memory_order_relaxed);
    demoSlot_.store(next, std::memory_order_release);
    demoLive_.store(false, std::memory_order_relaxed);   // slot playback mode
    demoOn_.store(true, std::memory_order_relaxed);
}

void AndroidAudioApp::cacheAudition(const std::string& toneId,
                                    const std::vector<float>& rendered) {
    constexpr size_t kMaxEntries = 12;   // ~7 MB worst case
    for (auto& e : auditionCache_)
        if (e.first == toneId) { e.second = rendered; return; }
    if (auditionCache_.size() >= kMaxEntries)
        auditionCache_.erase(auditionCache_.begin());
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
    if (t3kAuth_.hasValidToken()) { finish(true); return; }
    t3kAuth_.tryRefresh([finish](bool refreshed) { finish(refreshed); });
}

juce::File AndroidAudioApp::modelCacheFile(const std::string& scope) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("audition_models")
        .getChildFile(juce::String(scope) + ".nam");
}

void AndroidAudioApp::pruneModelCache() {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("audition_models");
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
                                        const std::string& cacheKey,
                                        juce::String displayName,
                                        std::function<void(bool, juce::String)> done,
                                        std::shared_ptr<const std::vector<float>> overrideIr) {
    // Deliberately do NOT stop the current demo: it keeps playing until the
    // new tone is ready, then swaps in place — clean A/B comparisons.
    const std::string path = file.getFullPathName().toStdString();
    const double sr = sampleRate_;
    const int liveBlock = blockSize_;
    const bool forcePre = preRenderAuditions_;
    auto dry = std::make_shared<std::vector<float>>(demoTracks_[(size_t) demoTrack_]);
    auto cabIr = overrideIr != nullptr ? overrideIr
                 : (cab_ > 0) ? cabIrs_[(size_t) cab_] : nullptr;

    juce::Thread::launch([this, path, sr, liveBlock, forcePre, dry, deleteAfter,
                          cacheKey, displayName, done, cabIr] {
        constexpr int block = 256;
        auto offline = std::make_unique<dsp::ToneEngine>();
        offline->prepare((int) sr, block);
        auto m = nam::NamModel::load(path, (int) sr, block);
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
        const size_t benchEnd = juce::jmin(dry->size(), (size_t) (sr * 0.25));
        const auto t0 = juce::Time::getHighResolutionTicks();
        size_t i = 0;
        for (; i < benchEnd; i += block) {
            const int n = (int) std::min((size_t) block, dry->size() - i);
            std::copy(dry->begin() + (long) i, dry->begin() + (long) i + n, chunk.begin());
            offline->render(chunk.data(), out->data() + i, n);
        }
        const double benchSec = juce::Time::highResolutionTicksToSeconds(
            juce::Time::getHighResolutionTicks() - t0);
        const double speed = benchSec > 0 ? ((double) benchEnd / sr) / benchSec : 0.0;
        juce::Logger::writeToLog("audition bench: " + juce::String(speed, 2)
                                 + "x realtime -> " + (! forcePre && speed >= 2.0 ? "live" : "pre-render"));

        if (! forcePre && speed >= 2.0) {
            // Fast enough for the audio thread: load a fresh instance at the
            // device block size and go live.
            auto live = nam::NamModel::load(path, (int) sr, liveBlock);
            if (deleteAfter) juce::File(juce::String(path)).deleteFile();
            nam::NamModel* raw = live.release();
            juce::MessageManager::callAsync([this, raw, displayName, done] {
                std::unique_ptr<nam::NamModel> model(raw);
                if (model == nullptr) { done(false, "model load failed"); return; }
                engine_.setModel(std::move(model));
                modelLoaded_ = true;
                // Keep position when already playing (A/B); slot and live
                // playback share the same DI timeline.
                if (! demoOn_.load(std::memory_order_relaxed))
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
            const int n = (int) std::min((size_t) block, dry->size() - i);
            std::copy(dry->begin() + (long) i, dry->begin() + (long) i + n, chunk.begin());
            offline->render(chunk.data(), out->data() + i, n);
            const float frac = (float) i / (float) dry->size();
            if (frac - lastReported >= 0.03f) {
                lastReported = frac;
                juce::MessageManager::callAsync([this, frac] {
                    if (shell_ != nullptr)
                        shell_->setAuditionProgress(0.1f + 0.9f * frac);
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
    auto ir = nam::loadImpulseResponse(irWav.getFullPathName().toStdString(),
                                       (int) sampleRate_, dsp::kMaxIrTaps);
    if (ir == nullptr) { done(false, "IR load failed"); return; }
    engine_.setImpulse(ir);
    engine_.setIrEnabled(true);
    if (preRenderAuditions_) {
        auditionFromFile(juce::File(juce::String(copyBundledModelToFile())),
                         false, cacheKey, displayName, done, ir);
        return;
    }
    if (! demoOn_.load(std::memory_order_relaxed)) {
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
    const juce::String title (tone.title);

    if (irFile.existsAsFile()) { applyIrAudition(irFile, key, title, done); return; }
    withValidToken([this, tone, done, irFile, key, title](bool ok) {
        if (! ok) { done(false, "connect first"); return; }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModel(tone.id, tempDir,
            [this, done, irFile, key, title](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
                irFile.getParentDirectory().createDirectory();
                if (file.moveFileTo(irFile)) applyIrAudition(irFile, key, title, done);
                else applyIrAudition(file, key, title, done);
            });
    });
}

void AndroidAudioApp::doAudition(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    if (tone.format == "ir") { doAuditionIr(std::move(tone), std::move(done)); return; }

    const std::string key = tone.id + "#best#" + std::to_string(demoTrack_)
                            + "#c" + std::to_string(cab_);
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
        if (! ok) {
            // Offline fallback: an older smallest-variant download still plays.
            if (legacySmallest.existsAsFile())
                auditionFromFile(legacySmallest, false, key, juce::String(tone.title), done);
            else
                done(false, "connect first");
            return;
        }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModel(tone.id, tempDir,
            [this, done, key, keepFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
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
    if (localFile.existsAsFile()) { done(true, juce::String(tone.title)); return; }

    withValidToken([this, tone, done, localFile](bool ok) {
        if (! ok) { done(false, "connect first"); return; }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModelForKeep(tone.id, tempDir,
            [done, localFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
                localFile.getParentDirectory().createDirectory();
                if (! file.moveFileTo(localFile)) { done(false, "could not store download"); return; }
                done(true, nameOrErr);
            });
    });
}

void AndroidAudioApp::doAuditionModel(const std::string& toneId, const nam::ModelInfo& model,
        bool isIr, std::function<void(bool, juce::String)> done) {
    const std::string key = toneId + "#" + model.id + "#" + std::to_string(demoTrack_)
                            + (isIr ? "#ir" : "#c" + std::to_string(cab_));
    const juce::String display (model.name.empty() ? model.id : model.name);
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
    if (cachedFile.existsAsFile()) { play(cachedFile, false); return; }

    withValidToken([this, model, done, cachedFile, play](bool ok) {
        if (! ok) { done(false, "connect first"); return; }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadModel(model, tempDir,
            [this, done, cachedFile, play](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
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

void AndroidAudioApp::doListModels(const std::string& toneId,
        std::function<void(bool, std::vector<nam::ModelInfo>, juce::String)> done) {
    withValidToken([this, toneId, done](bool ok) {
        if (! ok) { done(false, {}, "connect first"); return; }
        t3kSession_->listToneModels(toneId, std::move(done));
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
        if (! ok) return;   // offline / never connected: bundled stays
        t3kSession_->search("", 1,
            [this](bool sOk, std::vector<nam::ToneInfo> tones, juce::String) {
                if (! sOk) return;
                const nam::ToneInfo* best = nullptr;
                for (const auto& t : tones)
                    if (t.format == "nam" && t.a2Count > 0
                        && (best == nullptr || t.downloads > best->downloads))
                        best = &t;
                if (best == nullptr) return;
                const auto tone = *best;
                fetchArtwork(tone);
                doDownloadOnly(tone, [this, tone](bool dlOk, juce::String) {
                    if (! dlOk) return;
                    const auto keep = modelCacheFile("keep_" + tone.id);
                    if (! keep.existsAsFile()) return;
                    keep.copyFileTo(defaultToneFile());
                    defaultToneNameFile().replaceWithText(juce::String(tone.title));
                    juce::Logger::writeToLog("popular default fetched: " + juce::String(tone.title));
                    // Deck still empty and we're sitting on the bundled tone?
                    // Swap in the popular default right away.
                    if (library_.all(nam::LibraryType::Model).empty()) {
                        if (auto m = nam::NamModel::load(
                                defaultToneFile().getFullPathName().toStdString(),
                                (int) sampleRate_, blockSize_)) {
                            engine_.setModel(std::move(m));
                            modelLoaded_ = true;
                            if (shell_ != nullptr)
                                shell_->setNowPlayingInfo(juce::String(tone.title),
                                    "TONE3000 " + juce::String::fromUTF8("\xC2\xB7") + " MOST KEPT");
                        }
                    }
                });
            });
    });
}

juce::File AndroidAudioApp::artworkFile(const std::string& toneId) {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("NAM Player/artwork").getChildFile(toneId + ".jpg");
}

std::string AndroidAudioApp::toneIdFromEntry(const nam::LibraryEntry& e) {
    // Kept entries came from keep_<toneId>.nam / ir_<toneId>.nam (possibly
    // " (2)"-suffixed); the tone id is the digit run after the prefix.
    for (const char* prefix : { "keep_", "ir_" }) {
        const auto& f = e.fileName;
        const auto plen = std::string(prefix).size();
        if (f.rfind(prefix, 0) != 0) continue;
        std::string id;
        for (size_t i = plen; i < f.size() && std::isdigit((unsigned char) f[i]); ++i)
            id += f[i];
        if (! id.empty()) return id;
    }
    return {};
}

void AndroidAudioApp::fetchArtwork(nam::ToneInfo tone) {
    // tone.id and imageUrl are API-supplied: the id becomes a filename (must
    // not traverse out of the artwork dir) and the URL is fetched (https
    // only, bounded size). Tone ids are numeric on the real API.
    const bool idIsNumeric = ! tone.id.empty()
        && std::all_of(tone.id.begin(), tone.id.end(),
                       [](unsigned char c) { return std::isdigit(c); });
    if (! idIsNumeric || tone.id.size() > 20) return;
    if (tone.imageUrl.rfind("https://", 0) != 0 || tone.imageUrl.size() > 2048) return;
    if (artworkFile(tone.id).existsAsFile()) return;
    juce::Thread::launch([tone] {
        juce::URL url { juce::String(tone.imageUrl) };
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(15000));
        if (stream == nullptr) return;
        // Bounded read: a hostile/misconfigured server must not OOM the app.
        constexpr size_t kMaxImageBytes = 8 * 1024 * 1024;
        juce::MemoryBlock raw;
        while (! stream->isExhausted()) {
            if (raw.getSize() >= kMaxImageBytes) return;
            juce::HeapBlock<char> chunk(65536);
            const int n = stream->read(chunk.getData(), 65536);
            if (n <= 0) break;
            raw.append(chunk.getData(), (size_t) n);
        }
        auto img = juce::ImageFileFormat::loadFrom(raw.getData(), raw.getSize());
        if (! img.isValid()) return;   // e.g. webp — JUCE can't decode it
        // Downscale so per-swipe decode stays cheap (card is ~<700 px wide).
        constexpr int kMaxDim = 700;
        if (juce::jmax(img.getWidth(), img.getHeight()) > kMaxDim) {
            const float s = (float) kMaxDim / (float) juce::jmax(img.getWidth(), img.getHeight());
            img = img.rescaled(juce::roundToInt(img.getWidth() * s),
                               juce::roundToInt(img.getHeight() * s),
                               juce::Graphics::highResamplingQuality);
        }
        const auto dest = artworkFile(tone.id);
        dest.getParentDirectory().createDirectory();
        juce::FileOutputStream out(dest);
        if (! out.openedOk()) return;
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality(0.85f);
        if (! jpeg.writeImageToStream(img, out)) { dest.deleteFile(); return; }
        out.flush();
        // Cache hygiene (mirrors pruneModelCache): keep the most recent 64.
        auto dir = dest.getParentDirectory();
        juce::Array<juce::File> files = dir.findChildFiles(juce::File::findFiles, false, "*.jpg");
        if (files.size() > 64) {
            files.sort();   // fallback order; refine below by mod time
            std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
                return a.getLastModificationTime() < b.getLastModificationTime();
            });
            for (int i = 0; i < files.size() - 64; ++i)
                files.getReference(i).deleteFile();
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
                if (e.id.rfind(stem, 0) == 0
                    && (e.id.size() == stem.size() || e.id[stem.size()] == '.'
                        || e.id[stem.size()] == ' '))
                    return e.id;
    }
    return {};
}

void AndroidAudioApp::doToggleKeep(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    // IRs are cabs, not tones — they can't join the Play deck, so hearting
    // them is disabled (search is nam-only; this guards cached results).
    if (tone.format == "ir") { done(false, "IR packs can't be added to the deck"); return; }
    const auto id = libraryIdForTone(tone.id);
    if (! id.empty()) {
        library_.remove(id);
        library_.save();
        done(true, juce::String(tone.title));
        return;
    }
    fetchArtwork(tone);   // Play-screen art for the newly kept tone
    doDownload(std::move(tone), std::move(done));
}

// Keep = favorite: imports the already-downloaded file into the Library
// (models as Model, IR tones as Ir). Fetches first only if never downloaded.
void AndroidAudioApp::doDownload(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    const bool isIr = (tone.format == "ir");
    const auto localFile = modelCacheFile((isIr ? "ir_" : "keep_") + tone.id);
    if (localFile.existsAsFile()) {
        auto* entry = nam::importIntoLibrary(library_, localFile.getFullPathName().toStdString(),
                                             isIr ? nam::LibraryType::Ir : nam::LibraryType::Model,
                                             nowSeconds());
        if (entry == nullptr) { done(false, "import failed"); return; }
        library_.save();
        done(true, juce::String(entry->displayName));
        return;
    }
    doDownloadOnly(tone, [this, tone, done](bool ok, juce::String msg) {
        if (! ok) { done(false, msg); return; }
        doDownload(tone, done);   // local file exists now -> import branch
    });
}
