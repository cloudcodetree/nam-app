#include "app/ui/AppShell.h"

namespace {
const juce::String kEllipsis = juce::String::fromUTF8 ("\xE2\x80\xA6"); // …
const juce::String kHeart    = juce::String::fromUTF8 ("\xE2\x99\xA5"); // ♥
const juce::String kDotSep   = juce::String::fromUTF8 ("\xC2\xB7");     // ·
}

AppShell::AppShell (dsp::ToneEngine& engine) : engine_ (engine) {
    play_    = std::make_unique<PlayScreen>();
    edit_    = std::make_unique<EditScreen> (engine_);
    radio_   = std::make_unique<RadioScreen>();
    library_ = std::make_unique<LibraryScreen>();
    live_    = std::make_unique<LiveScreen>();
    devices_ = std::make_unique<AudioSettingsScreen>();

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) radio_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get() })
        addChildComponent (*c);

    play_->onNav = [this] (int tab) {
        switch (tab) { case 1: show (Screen::Edit);  break;
                       case 2: show (Screen::Radio); break;
                       case 3: show (Screen::Live);  break;
                       default: show (Screen::Play); break; }
    };
    play_->onLibrary  = [this] { show (Screen::Library); };
    play_->onSettings = [this] { show (Screen::Devices); };
    devices_->onBack  = [this] { show (Screen::Play); };
    edit_->onDone    = [this] { show (Screen::Play); };
    radio_->onBack   = [this] { show (Screen::Play); };
    library_->onBack = [this] { show (Screen::Play); };
    live_->onExit    = [this] { show (Screen::Play); };

    devices_->onSetInputDb = [this] (float db) { engine_.setInputDb (db); };

    show (Screen::Play);
}

void AppShell::setTone3000 (SearchFn search, DownloadFn download) {
    searchFn_ = std::move (search);
    downloadFn_ = std::move (download);

    radio_->onSearch = [this] (juce::String q) { runRadioSearch (std::move (q)); };

    radio_->onKeep = [this] (int idx) {
        if (! downloadFn_ || idx < 0 || idx >= (int) radioResults_.size()) return;
        const auto tone = radioResults_[(size_t) idx];
        radio_->setStatus ("Downloading \"" + juce::String (tone.title) + "\"" + kEllipsis);
        downloadFn_ (tone, [this] (bool ok, juce::String msg) {
            radio_->setStatus (ok ? (kHeart + " Kept: " + msg) : ("Download failed: " + msg));
        });
    };
}

void AppShell::setLibraryService (GetModelsFn getModels, LoadModelFn loadModel) {
    getModels_ = std::move (getModels);
    loadModel_ = std::move (loadModel);
    library_->onLoad = [this] (nam::LibraryEntry e) { if (loadModel_) loadModel_ (e); show (Screen::Play); };
    live_->onSelect  = [this] (nam::LibraryEntry e) { if (loadModel_) loadModel_ (e); };
}

void AppShell::runRadioSearch (juce::String q) {
    if (! searchFn_) return;
    if (stopDemoFn_) { stopDemoFn_(); auditioning_ = -1; radio_->setPlaying (-1); }
    radio_->setStatus (q.isEmpty() ? ("Tuning in to TONE3000" + kEllipsis)
                                   : ("Connecting / searching \"" + q + "\"" + kEllipsis));
    searchFn_ (q, [this] (bool ok, std::vector<nam::ToneInfo> tones, juce::String err) {
        if (! ok) { radio_->setStatus ("TONE3000: " + err); return; }
        radioResults_ = tones;
        radio_->setResults (tones);
        radio_->setStatus (juce::String ((int) tones.size())
                           + (tones.size() == 1 ? " tone" : " tones")
                           + " " + kDotSep + " tap to audition " + kDotSep
                           + " " + kHeart + " keeps");
    });
}

void AppShell::setAuditionService (AuditionFn audition, std::function<void()> stopDemo) {
    auditionFn_  = std::move (audition);
    stopDemoFn_  = std::move (stopDemo);

    radio_->onAudition = [this] (int idx) {
        if (idx < 0 || idx >= (int) radioResults_.size()) return;
        if (idx == auditioning_) {                       // tap again = stop
            if (stopDemoFn_) stopDemoFn_();
            auditioning_ = -1;
            radio_->setPlaying (-1);
            radio_->setStatus ("Stopped");
            return;
        }
        if (! auditionFn_) return;
        const auto tone = radioResults_[(size_t) idx];
        radio_->setStatus ("Loading \"" + juce::String (tone.title) + "\"" + kEllipsis);
        auditionFn_ (tone, [this, idx, tone] (bool ok, juce::String msg) {
            if (! ok) { radio_->setStatus ("Audition failed: " + msg); return; }
            auditioning_ = idx;
            radio_->setPlaying (idx);
            radio_->setStatus ("Auditioning \"" + juce::String (tone.title) + "\"");
        });
    };
}

void AppShell::setAudioDeviceService (GetDevicesFn get, SelectDeviceFn selectInput,
                                      SelectDeviceFn selectOutput, RescanFn rescan,
                                      SelectDeviceFn selectRate, SelectDeviceFn selectBuffer) {
    getDevices_     = std::move (get);
    selectInput_    = std::move (selectInput);
    selectOutput_   = std::move (selectOutput);
    rescanDevices_  = std::move (rescan);
    selectRate_     = std::move (selectRate);
    selectBuffer_   = std::move (selectBuffer);

    devices_->onRescan = [this] { if (rescanDevices_) rescanDevices_(); refreshDevices(); };
    devices_->onSelectInput = [this] (juce::String name) {
        if (selectInput_) selectInput_ (name);
        refreshDevices();
    };
    devices_->onSelectOutput = [this] (juce::String name) {
        if (selectOutput_) selectOutput_ (name);
        refreshDevices();
    };
    devices_->onSelectRate = [this] (juce::String label) {
        if (selectRate_) selectRate_ (label);
        refreshDevices();
    };
    devices_->onSelectBuffer = [this] (juce::String label) {
        if (selectBuffer_) selectBuffer_ (label);
        refreshDevices();
    };
}

void AppShell::refreshDevices() {
    if (! getDevices_) return;
    devices_->setState (getDevices_());
}

bool AppShell::handleBackButton() {
    if (current_ != nullptr && current_ != play_.get()) { show (Screen::Play); return true; }
    return false;
}

void AppShell::show (Screen s) {
    // Refresh data-backed screens as they come into view.
    if (s == Screen::Library && getModels_) library_->setEntries (getModels_());
    if (s == Screen::Live && getModels_)     live_->setSlots (getModels_());
    if (s == Screen::Devices)                refreshDevices();

    // Radio opens onto the live TONE3000 catalog (browse-by-default).
    if (s == Screen::Radio && ! radioLoadedOnce_ && searchFn_) {
        radioLoadedOnce_ = true;
        runRadioSearch ({});
    }
    // Leaving Radio stops any audition demo.
    if (s != Screen::Radio && auditioning_ >= 0) {
        if (stopDemoFn_) stopDemoFn_();
        auditioning_ = -1;
        radio_->setPlaying (-1);
    }

    juce::Component* target = play_.get();
    switch (s) {
        case Screen::Edit:    target = edit_.get();    break;
        case Screen::Radio:   target = radio_.get();   break;
        case Screen::Library: target = library_.get(); break;
        case Screen::Live:    target = live_.get();    break;
        case Screen::Devices: target = devices_.get(); break;
        case Screen::Play:    default: target = play_.get(); break;
    }
    if (current_ == target) return;
    if (current_ != nullptr) current_->setVisible (false);
    current_ = target;
    current_->setBounds (getLocalBounds());
    current_->setVisible (true);
    current_->toFront (false);
}

void AppShell::setLevels (float in, float out) {
    if (play_ != nullptr)    play_->setLevels (in, out);
    if (devices_ != nullptr) devices_->setLevels (in, out);
}

void AppShell::resized() {
    auto b = getLocalBounds();
    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) radio_.get(), (juce::Component*) library_.get(),
                                (juce::Component*) live_.get(), (juce::Component*) devices_.get() })
        if (c != nullptr) c->setBounds (b);
}
