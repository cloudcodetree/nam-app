#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"
#include "model/LibraryImporter.h"
#include "model/LibraryEntry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sys/system_properties.h>

AndroidAudioApp::AndroidAudioApp() {
    setLookAndFeel(&laf_);

    // On the emulator the "microphone" is synthetic noise: amplifying it
    // through an amp model is pure hiss. Mute live input there — the demo
    // riff (audition) is the emulator's sound source. Real devices keep the
    // live guitar path.
    char qemu[PROP_VALUE_MAX] = {};
    if ((__system_property_get("ro.boot.qemu", qemu) > 0 && qemu[0] == '1')
        || (__system_property_get("ro.kernel.qemu", qemu) > 0 && qemu[0] == '1'))
        liveMuted_.store(true, std::memory_order_relaxed);

    library_.load();
    t3kAuth_.loadTokens();   // reuse a prior refresh token if present

    shell_ = std::make_unique<AppShell>(engine_);
    addAndMakeVisible(*shell_);
    shell_->setTone3000(
        [this](juce::String q,
               std::function<void(bool, std::vector<nam::ToneInfo>, juce::String)> done) {
            doSearch(std::move(q), std::move(done));
        },
        [this](nam::ToneInfo t, std::function<void(bool, juce::String)> done) {
            doDownload(std::move(t), std::move(done));
        });
    shell_->setAuditionService(
        [this](nam::ToneInfo t, std::function<void(bool, juce::String)> done) {
            doAudition(std::move(t), std::move(done));
        },
        [this] { setDemoActive(false); });
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
    const int slot = demoSlot_.load(std::memory_order_relaxed);
    if (demoOn_.load(std::memory_order_relaxed) && slot >= 0
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

// --- Audition (demo riff) -------------------------------------------------
// Pre-renders a short looping dry-guitar riff via Karplus-Strong plucked
// strings: E-minor noodle over ~3.2 s. Runs at prepare time (message thread),
// so allocation is fine; the audio thread only reads the finished buffer.
void AndroidAudioApp::buildDemoLoop(double sr) {
    const double loopSec = 3.2;
    demoLoop_.assign((size_t) (sr * loopSec), 0.0f);

    struct Note { double freq, t; float amp; };
    const Note notes[] = {
        { 82.41, 0.0,  0.95f },   // E2
        { 98.00, 0.4,  0.70f },   // G2
        { 110.0, 0.8,  0.80f },   // A2
        { 82.41, 1.2,  0.95f },   // E2
        { 123.47, 1.6, 0.70f },   // B2
        { 110.0, 2.0,  0.80f },   // A2
        { 98.00, 2.4,  0.70f },   // G2
        { 82.41, 2.8,  0.95f },   // E2 (rings into the loop start)
    };

    juce::Random rng(7);
    for (const auto& n : notes) {
        const int start = (int) (n.t * sr);
        const int N = juce::jmax(2, (int) (sr / n.freq));
        std::vector<float> line((size_t) N);
        for (auto& v : line) v = rng.nextFloat() * 2.0f - 1.0f;

        const int len = (int) (sr * 1.4);   // ring time per pluck
        int idx = 0;
        for (int i = 0; i < len && start + i < (int) demoLoop_.size(); ++i) {
            const int j = (idx + 1) % N;
            const float out = 0.5f * (line[(size_t) idx] + line[(size_t) j]) * 0.996f;
            line[(size_t) idx] = out;
            idx = j;
            demoLoop_[(size_t) (start + i)] += out * n.amp * 0.35f;
        }
    }
    for (auto& v : demoLoop_) v = std::tanh(v);   // gentle safety clip
}

void AndroidAudioApp::setDemoActive(bool on) {
    demoPos_.store(0, std::memory_order_relaxed);
    demoOn_.store(on, std::memory_order_relaxed);
}

void AndroidAudioApp::setLiveInputMuted(bool muted) {
    liveMuted_.store(muted, std::memory_order_relaxed);
}

void AndroidAudioApp::installRenderedDemo(std::vector<float> rendered) {
    const int next = (demoSlot_.load(std::memory_order_relaxed) + 1) & 1;
    demoSlots_[(size_t) next] = std::move(rendered);
    demoPos_.store(0, std::memory_order_relaxed);
    demoSlot_.store(next, std::memory_order_release);
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

void AndroidAudioApp::doAudition(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    // Re-tap of a tone we already rendered: play instantly, no network.
    if (const auto* hit = cachedAudition(tone.id)) {
        installRenderedDemo(std::vector<float>(*hit));
        done(true, juce::String(tone.title));
        return;
    }

    if (! t3kAuth_.hasValidToken()) { done(false, "connect first (pick a station)"); return; }
    if (t3kSession_ == nullptr)
        t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const std::string toneId = tone.id;
    t3kSession_->downloadToneModel(tone.id, tempDir,
        [this, done, toneId](bool ok, juce::File file, juce::String nameOrErr) {
            if (! ok) { done(false, nameOrErr); return; }

            // Render the dry riff through the tone OFFLINE (background
            // thread, separate engine instance) — playback then costs
            // nothing on the audio thread.
            const std::string path = file.getFullPathName().toStdString();
            const double sr = sampleRate_;
            auto dry = std::make_shared<std::vector<float>>(demoLoop_);
            juce::Thread::launch([this, path, sr, dry, done, nameOrErr, toneId] {
                constexpr int block = 256;
                auto offline = std::make_unique<dsp::ToneEngine>();
                offline->prepare((int) sr, block);
                auto m = nam::NamModel::load(path, (int) sr, block);
                juce::File(juce::String(path)).deleteFile();
                if (m == nullptr) {
                    juce::MessageManager::callAsync([done] { done(false, "model load failed"); });
                    return;
                }
                offline->setModel(std::move(m));

                auto out = std::make_shared<std::vector<float>>(dry->size(), 0.0f);
                std::vector<float> chunk(block, 0.0f);
                for (size_t i = 0; i < dry->size(); i += block) {
                    const int n = (int) std::min((size_t) block, dry->size() - i);
                    std::copy(dry->begin() + (long) i, dry->begin() + (long) i + n, chunk.begin());
                    offline->render(chunk.data(), out->data() + i, n);
                }

                // Normalise the audition to a healthy, consistent level.
                float pk = 0.0f;
                for (float v : *out) pk = std::max(pk, std::fabs(v));
                if (pk > 0.0001f) {
                    const float g = 0.6f / pk;
                    for (auto& v : *out) v *= g;
                }

                juce::MessageManager::callAsync([this, out, done, nameOrErr, toneId] {
                    cacheAudition(toneId, *out);
                    installRenderedDemo(std::move(*out));
                    done(true, nameOrErr);
                });
            });
        },
        true /* preferSmallest: quick, cheap audition variant */);
}

void AndroidAudioApp::doDownload(nam::ToneInfo tone,
        std::function<void(bool, juce::String)> done) {
    if (! t3kAuth_.hasValidToken()) { done(false, "connect first (pick a station)"); return; }
    if (t3kSession_ == nullptr)
        t3kSession_ = std::make_unique<nam::Tone3000Session>(t3kAuth_.accessToken());

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    t3kSession_->downloadToneModel(tone.id, tempDir,
        [this, done](bool ok, juce::File file, juce::String nameOrErr) {
            if (! ok) { done(false, nameOrErr); return; }
            auto* entry = nam::importIntoLibrary(library_, file.getFullPathName().toStdString(),
                                                 nam::LibraryType::Model, nowSeconds());
            file.deleteFile();
            if (entry == nullptr) { done(false, "import failed"); return; }
            library_.save();
            done(true, juce::String(entry->displayName));
        });
}
