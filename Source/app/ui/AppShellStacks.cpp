#include "app/ui/AppShell.h"
#include "model/Entitlements.h"

// Stacks surface plumbing: JSON persistence (v2 ordered-chain model, via
// StackModel -- v1 fixed-slot files migrate transparently on first load) and
// the Home/Detail screen wiring. Split out of AppShell.cpp per the
// no-god-files rule; AppShell.cpp keeps screen orchestration (show/resized/
// handleBackButton) since those touch state this file doesn't own.

void AppShell::loadStacksState () {
    stackList_.clear ();
    currentStack_ = 0;
    stacksDetailIdx_ = -1;
    if (!svc_.loadStacksJson) return;
    stackList_ = nam::StackModel::parse (svc_.loadStacksJson ().toStdString ());
}

void AppShell::saveStacksState () {
    if (!svc_.saveStacksJson) return;
    // StackModel::serialize always writes v2 -- a v1 file migrated on load
    // is upgraded on the very next save, no explicit migration step needed.
    svc_.saveStacksJson (juce::String (nam::StackModel::serialize (stackList_)));
}

void AppShell::pushStacks () {
    if (stacksHome_ != nullptr) stacksHome_->setStacks (stackList_, currentStack_);
}

void AppShell::openStackDetail (int idx, bool perform) {
    if (idx < 0 || idx >= (int)stackList_.size () || stacksDetail_ == nullptr) return;
    stacksDetailIdx_ = idx;
    stacksShowDetail_ = true;
    stacksDetail_->setStack (stackList_[(size_t)idx], idx);
    stacksDetail_->selectTab (perform);
    show (Screen::Stacks);
}

void AppShell::openOrbPanel () {
    // Same entry point as tapping the status orb (AppShellChrome.cpp
    // mouseDown): opens the I/O mute/engine flyout. The Stacks gear icon is
    // wired to this rather than duplicating Audio Settings as a second UI.
    ioPanelOpen_ = true;
    ioScrim_.setBounds (contentBounds ());
    ioScrim_.setVisible (true);
    ioScrim_.toFront (false);
    repaint ();
}

void AppShell::wireStacksScreens () {
    stacksHome_->onCreate = [this] {
        // Public-launch config only (kSoftPaywall): the STACKS nav gate lets
        // free users reach Stacks, so creation of a SECOND rig is gated
        // here instead. Policy lives in Entitlements, not this UI layer;
        // isPro_ null stays ungated (desktop convention). Verbatim from the
        // pre-redesign StacksScreen-era onCreate gate.
        if (kGatesEnabled && kSoftPaywall && isPro_) {
            nam::Entitlements ent;
            ent.setPro (isPro_ ());
            if (!ent.canSaveRig ((int)stackList_.size ())) {
                openPaywall (juce::String::fromUTF8 (
                    "Your first rig stays free forever \xE2\x80\x94 Pro adds unlimited rigs"));
                return;
            }
        }
        nam::Stack st;
        st.name = ("Stack " + juce::String ((int)stackList_.size () + 1)).toStdString ();
        stackList_.push_back (std::move (st));
        currentStack_ = (int)stackList_.size () - 1;
        saveStacksState ();
        pushStacks ();
    };
    stacksHome_->onSetCurrent = [this] (int i) {
        if (i < 0 || i >= (int)stackList_.size ()) return;
        currentStack_ = i;
        pushStacks ();
    };
    stacksHome_->onOpen = [this] (int i) { openStackDetail (i, false); };
    stacksHome_->onPerform = [this] (int i) { openStackDetail (i, true); };
    stacksHome_->onSettings = [this] { openOrbPanel (); };

    stacksDetail_->onBack = [this] {
        stacksShowDetail_ = false;
        show (Screen::Stacks);
    };
    stacksDetail_->onSettings = [this] { openOrbPanel (); };
}
