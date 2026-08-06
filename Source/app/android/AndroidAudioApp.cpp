#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"
#include "model/LibraryImporter.h"
#include "model/LibraryEntry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sys/system_properties.h>

#include "dr_wav.h"

AndroidAudioApp::AndroidAudioApp() {
    setLookAndFeel(&laf_);

    // On the emulator the "microphone" is synthetic noise: amplifying it
    // through an amp model is pure hiss. Mute live input there — the demo
    // riff (audition) is the emulator's sound source. Real devices keep the
    // live guitar path.
    char qemu[PROP_VALUE_MAX] = {};
    if ((__system_property_get("ro.boot.qemu", qemu) > 0 && qemu[0] == '1')
        || (__system_property_get("ro.kernel.qemu", qemu) > 0 && qemu[0] == '1')) {
        liveMuted_.store(true, std::memory_order_relaxed);
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
        doDownload(std::move(t), std::move(done));
    };
    browse.downloadOnly = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doDownloadOnly(std::move(t), std::move(done));
    };
    browse.audition = [this](nam::ToneInfo t, AppShell::DoneFn done) {
        doAudition(std::move(t), std::move(done));
    };
    browse.auditionModel = [this](std::string toneId, nam::ModelInfo m, AppShell::DoneFn done) {
        doAuditionModel(toneId, m, std::move(done));
    };
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
    browse.isAuditionCached = [this](std::string toneId) {
        if (preRenderAuditions_)
            return cachedAudition(toneId + "#auto#" + std::to_string(demoTrack_)) != nullptr;
        // Live mode: a local model file means instant audition, any riff.
        return modelCacheFile("keep_" + toneId).existsAsFile()
            || modelCacheFile("auto_" + toneId).existsAsFile();
    };
    browse.isDownloaded = [](std::string toneId) {
        return modelCacheFile("keep_" + toneId).existsAsFile();
    };
    shell_->setBrowseServices(std::move(browse));
    shell_->setLibraryService(
        [this] { return library_.all(nam::LibraryType::Model); },
        [this](nam::LibraryEntry e) { loadModelEntry(e); });
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

    setSize(900, 500);
    startTimerHz(30);
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

    if (auto m = nam::NamModel::load(copyBundledModelToFile(),
                                     (int) sampleRate, samplesPerBlockExpected)) {
        engine_.setModel(std::move(m));
        modelLoaded_ = true;
    }
}

void AndroidAudioApp::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    auto* buf = info.buffer;
    const int n = info.numSamples;
    const int cap = (int) mono_.size();

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
    } else if (liveMuted_.load(std::memory_order_relaxed)) {
        // No demo playing and live input muted (emulator): true silence.
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

    for (int ch = 0; ch < buf->getNumChannels(); ++ch) {
        float* out = buf->getWritePointer(ch, info.startSample);
        for (int i = 0; i < n && i < cap; ++i) out[i] = mono_[(size_t) i];
    }
}

void AndroidAudioApp::releaseResources() {}

void AndroidAudioApp::timerCallback() {
    if (shell_ != nullptr)
        shell_->setLevels(inPeak_.load(std::memory_order_relaxed),
                          engine_.outputPeak());

    // Slow hot-plug poll (~every 3 s): Android doesn't notify JUCE when a USB
    // interface appears, so rescan until one is adopted or the user picks.
    if (! userChoseInput_ && ++rescanTick_ >= 90) {
        rescanTick_ = 0;
        preferUsbInput();
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

void AndroidAudioApp::preferUsbInput() {
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr) return;
    type->scanForDevices();

    juce::String usb;
    for (const auto& n : type->getDeviceNames(true))
        if (n.containsIgnoreCase("usb") || n.containsIgnoreCase("irig")) { usb = n; break; }
    if (usb.isEmpty()) return;

    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName == usb) return;
    setup.inputDeviceName = usb;
    setup.useDefaultInputChannels = true;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("preferUsbInput(" + usb + ") -> "
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
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.bufferSize == frames) return;
    setup.bufferSize = frames;
    const auto err = applyDeviceSetup(setup);
    juce::Logger::writeToLog("selectBufferSize(" + label + ") -> " + (err.isEmpty() ? "ok" : err));
}

void AndroidAudioApp::changeListenerCallback(juce::ChangeBroadcaster*) {
    // Device setup changed (stream restart etc.) — re-check USB routing unless
    // we caused the change ourselves or the user made an explicit choice.
    if (applyingDeviceChange_ || userChoseInput_) return;
    preferUsbInput();
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
    auto run = [this, query, done] {
        t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());
        t3kSession_->search(query.toStdString(), 1, done);
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
    if (auto m = nam::NamModel::load(path, (int) sampleRate_, blockSize_)) {
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
    liveMuted_.store(muted, std::memory_order_relaxed);
}

void AndroidAudioApp::installRenderedDemo(std::vector<float> rendered) {
    const int next = (demoSlot_.load(std::memory_order_relaxed) + 1) & 1;
    demoSlots_[(size_t) next] = std::move(rendered);
    demoPos_.store(0, std::memory_order_relaxed);
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
                                        std::function<void(bool, juce::String)> done) {
    setDemoActive(false);
    const std::string path = file.getFullPathName().toStdString();
    const double sr = sampleRate_;
    const int liveBlock = blockSize_;
    const bool forcePre = preRenderAuditions_;
    auto dry = std::make_shared<std::vector<float>>(demoTracks_[(size_t) demoTrack_]);

    juce::Thread::launch([this, path, sr, liveBlock, forcePre, dry, deleteAfter,
                          cacheKey, displayName, done] {
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
            installRenderedDemo(std::move(*out));
            done(true, displayName);
        });
    });
}

void AndroidAudioApp::doAudition(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    const std::string key = tone.id + "#best#" + std::to_string(demoTrack_);
    // Rendered-audio cache: instant replay (covers heavy pre-rendered
    // models on device and everything on the emulator).
    if (const auto* hit = cachedAudition(key)) {
        setDemoActive(false);
        installRenderedDemo(std::vector<float>(*hit));
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
    const auto keepFile = modelCacheFile("keep_" + tone.id);
    if (keepFile.existsAsFile()) { done(true, juce::String(tone.title)); return; }

    withValidToken([this, tone, done, keepFile](bool ok) {
        if (! ok) { done(false, "connect first"); return; }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadToneModelForKeep(tone.id, tempDir,
            [done, keepFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
                keepFile.getParentDirectory().createDirectory();
                if (! file.moveFileTo(keepFile)) { done(false, "could not store download"); return; }
                done(true, nameOrErr);
            });
    });
}

void AndroidAudioApp::doAuditionModel(const std::string& toneId, const nam::ModelInfo& model,
        std::function<void(bool, juce::String)> done) {
    const std::string key = toneId + "#" + model.id + "#" + std::to_string(demoTrack_);
    const juce::String display (model.name.empty() ? model.id : model.name);
    if (const auto* hit = cachedAudition(key)) {
        setDemoActive(false);
        installRenderedDemo(std::vector<float>(*hit));
        done(true, display);
        return;
    }

    const auto cachedFile = modelCacheFile("m_" + toneId + "_" + model.id);
    if (cachedFile.existsAsFile()) {
        auditionFromFile(cachedFile, false, key, display, done);
        return;
    }

    withValidToken([this, model, done, key, cachedFile](bool ok) {
        if (! ok) { done(false, "connect first"); return; }
        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        t3kSession_->downloadModel(model, tempDir,
            [this, done, key, cachedFile](bool dlOk, juce::File file, juce::String nameOrErr) {
                if (! dlOk) { done(false, nameOrErr); return; }
                cachedFile.getParentDirectory().createDirectory();
                if (file.moveFileTo(cachedFile)) {
                    pruneModelCache();
                    auditionFromFile(cachedFile, false, key, nameOrErr, done);
                } else {
                    auditionFromFile(file, true, key, nameOrErr, done);
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

// Keep = favorite: imports the already-downloaded model into the Library.
// If it was never downloaded, fetch it first (best quality), then import.
void AndroidAudioApp::doDownload(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    const auto keepFile = modelCacheFile("keep_" + tone.id);
    if (keepFile.existsAsFile()) {
        auto* entry = nam::importIntoLibrary(library_, keepFile.getFullPathName().toStdString(),
                                             nam::LibraryType::Model, nowSeconds());
        if (entry == nullptr) { done(false, "import failed"); return; }
        library_.save();
        done(true, juce::String(entry->displayName));
        return;
    }
    doDownloadOnly(tone, [this, tone, done](bool ok, juce::String msg) {
        if (! ok) { done(false, msg); return; }
        doDownload(tone, done);   // keep file exists now -> import branch
    });
}
