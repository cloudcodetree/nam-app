#include "app/android/AndroidAudioApp.h"

#include "BinaryData.h"
#include "model/LibraryImporter.h"
#include "model/LibraryEntry.h"

#include <algorithm>
#include <chrono>
#include <cmath>

AndroidAudioApp::AndroidAudioApp() {
    setLookAndFeel(&laf_);

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
    shell_->setLibraryService(
        [this] { return library_.all(nam::LibraryType::Model); },
        [this](nam::LibraryEntry e) { loadModelEntry(e); });
    shell_->setAudioDeviceService(
        [this] {
            return AppShell::AudioDeviceState { inputDeviceNames(), outputDeviceNames(),
                                                currentInputDevice(), currentOutputDevice() };
        },
        [this](juce::String name) { selectInputDevice(name); },
        [this](juce::String name) { selectOutputDevice(name); });

    // First launch (empty library, never connected): show the setup/gain-stage.
    if (library_.all(nam::LibraryType::Model).empty() && ! t3kAuth_.hasValidToken())
        shell_->startOnSetup();

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

    const float* in = buf->getReadPointer(0, info.startSample);
    float inPk = 0.0f;
    for (int i = 0; i < n && i < cap; ++i) {
        mono_[(size_t) i] = in[i];
        inPk = juce::jmax(inPk, std::fabs(in[i]));
    }
    inPeak_.store(inPk, std::memory_order_relaxed);

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
