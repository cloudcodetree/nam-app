#include "app/ui/AppShell.h"
#include "app/ui/DemoTrackCatalog.h"

// DI tray wiring: builds the track list from the real catalog and drives the
// host's demo services. Lives here rather than in AppShell.cpp, which is past
// the 800-line ceiling.
//
// The tray plays the DI loop LIVE through the current tone (svc_.playDemoLive),
// so whatever you are looking at -- Play, Stacks, Controllers -- you hear the
// test track through the tone that is loaded right now.

void AppShell::wireDiTray () {
    diTray_ = std::make_unique<DiTrayPanel> ();
    addAndMakeVisible (*diTray_);

    std::vector<DiTrayPanel::Track> tracks;
    tracks.reserve ((size_t)nam::demo::kNumTracks);
    for (int i = 0; i < nam::demo::kNumTracks; ++i) {
        const auto& def = nam::demo::kTracks[i];
        DiTrayPanel::Track t;
        t.name = juce::String (def.display);
        // Bundled tracks are instant; the rest fetch once and cache, which the
        // row's GET tag tells the user before they tap.
        t.bundled = def.binaryResource != nullptr;
        t.isBass = t.name.endsWithIgnoreCase ("Bass");
        tracks.push_back (std::move (t));
    }
    diTray_->setTracks (std::move (tracks));

    diTray_->onLayoutChanged = [this] { resized (); };

    diTray_->onSelect = [this] (int index) { startDiTrack (index); };

    diTray_->onStep = [this] (int delta) {
        const int n = nam::demo::kNumTracks;
        startDiTrack (((diTray_->currentIndex () + delta) % n + n) % n);
    };

    diTray_->onPlayPause = [this] (bool wantPlaying) {
        if (!wantPlaying) {
            if (svc_.playDemoLive) svc_.playDemoLive (false);
            // Give the guitar back the moment the track stops.
            if (svc_.muteLiveInput) svc_.muteLiveInput (false);
            diTray_->setPlaying (false);
            return;
        }
        startDiTrack (diTray_->currentIndex ());
    };
}

void AppShell::startDiTrack (int index) {
    if (diTray_ == nullptr || !svc_.setDemoTrack) return;

    diTray_->setCurrent (index);
    diTray_->setBusy (true);

    // A non-bundled track has to be fetched first; setDemoTrack only switches
    // the real-time source once the audio is actually loaded.
    svc_.setDemoTrack (index, [this, index] (bool ok) {
        if (diTray_ == nullptr) return;
        diTray_->setBusy (false);
        if (!ok) {
            // Fetch failed (offline, or the file is not on the server): leave
            // the tray honest rather than showing a track that is not playing.
            diTray_->setPlaying (false);
            return;
        }
        if (diTray_->currentIndex () != index) return;   // user moved on mid-fetch

        // Mute the guitar while a test track runs, the same way audition does
        // -- otherwise you hear both at once and cannot judge the tone.
        if (svc_.muteLiveInput) svc_.muteLiveInput (true);
        if (svc_.playDemoLive) svc_.playDemoLive (true);
        diTray_->setPlaying (true);
    });
}

void AppShell::layoutDiTray () {
    if (diTray_ == nullptr) return;
    const int h = diTray_->isExpanded () ? contentBounds ().getHeight () : diTray_->contentInset ();
    diTray_->setBounds (0, 0, getWidth (), juce::jmax (1, h));
    // Overlay rule: the tray paints over the screens, never under them, and
    // never over the bottom nav (its bounds stop at contentBounds).
    diTray_->toFront (false);
}
