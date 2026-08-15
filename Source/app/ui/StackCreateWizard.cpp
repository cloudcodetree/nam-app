#include "app/ui/StackCreateWizard.h"
#include <cmath>
#include "app/ui/NamLookAndFeel.h"

// State/layout/hit-testing for the Stack creation wizard. Painting lives in
// StackCreateWizardPaint.cpp (no-god-files split, same shape as
// StackEditView/StackEditViewPaint).
//
// Local-library caveat: nam::LibraryEntry (Source/model/LibraryEntry.h) has
// no gear-type tag -- it only distinguishes Model vs Ir, not amp vs pedal
// within Model. Steps 1 ("amp channels") and 2 ("pedals") both therefore
// list the SAME full onFetchModels() result; there is no way to filter one
// down to "pedals only" with today's data. This mirrors the spec's own
// step-1 "FROM YOUR LIBRARY" caption, which the mock scopes to local state
// only (not a live TONE3000 fetch) -- see the SDD notes doc's "Stack
// creation flow" / "TONE3000 touchpoints" and the task report for the
// tracked follow-up (a real gear tag on LibraryEntry).

StackCreateWizard::StackCreateWizard () {
    setInterceptsMouseClicks (true, true);
    setVisible (false);
}

const nam::ChainItem* StackCreateWizard::ampItem () const {
    for (const auto& it : draft_.chain)
        if (it.type == nam::GearType::Amp) return &it;
    return nullptr;
}
const nam::ChainItem* StackCreateWizard::cabItem () const {
    for (const auto& it : draft_.chain)
        if (it.type == nam::GearType::Cab) return &it;
    return nullptr;
}
bool StackCreateWizard::pedalIncluded (const std::string& toneId) const {
    for (const auto& it : draft_.chain)
        if (it.type == nam::GearType::Pedal && it.toneId == toneId) return true;
    return false;
}

const char* StackCreateWizard::stepTitle (Step s) {
    switch (s) {
        case Step::Gallery: return "Start from a rig";
        case Step::Amp: return "Pick your amp captures";
        case Step::Pedals: return "Pedals in front";
        case Step::Cab: return "Cabinet";
        case Step::Foot: return "Map to your Chocolate";
    }
    return "";
}
const char* StackCreateWizard::stepSubtitle (Step s) {
    switch (s) {
        case Step::Gallery:
            return "every template comes pre-mapped for your Chocolate \xE2\x80\x94 swap "
                   "anything after";
        case Step::Amp: return "every channel you add here becomes footswitchable in step 4";
        case Step::Pedals: return "tap to include \xC2\xB7 order is editable later";
        case Step::Cab: return "one IR \xE2\x80\x94 shared by all channels";
        case Step::Foot:
            return "we pre-mapped it from your gear \xE2\x80\x94 tap a switch to change what it "
                   "does";
    }
    return "";
}
const char* StackCreateWizard::nextStepLabel (Step s) {
    switch (s) {
        case Step::Amp: return "PEDALS";
        case Step::Pedals: return "CAB";
        case Step::Cab: return "FOOT";
        default: return "";
    }
}

void StackCreateWizard::open (int existingStackCount) {
    draft_ = nam::Stack ();
    draft_.name = ("Stack " + juce::String (existingStackCount + 1)).toStdString ();
    galleryTemplates_ = nam::templates::builtins ();
    models_ = onFetchModels ? onFetchModels () : std::vector<nam::LibraryEntry> ();
    irs_ = onFetchIrs ? onFetchIrs () : std::vector<nam::LibraryEntry> ();
    stepVisited_ = {};
    autoMapped_ = false;
    switches_ = {};
    armedSwitch_ = -1;
    step_ = Step::Gallery;
    scrollY_ = 0.0f;

    if (auto* parent = getParentComponent ()) setBounds (parent->getLocalBounds ());
    setVisible (true);
    toFront (false);
    layout ();
    repaint ();
}

void StackCreateWizard::close () { setVisible (false); }

void StackCreateWizard::resized () { layout (); }

void StackCreateWizard::goToStep (Step s) {
    if (step_ != Step::Gallery) stepVisited_[(size_t)((int)step_ - 1)] = true;
    step_ = s;
    armedSwitch_ = -1;
    scrollY_ = 0.0f;
    if (step_ == Step::Foot) {
        pruneStaleAssignments ();
        autoMapIfNeeded ();
    }
    layout ();
    repaint ();
}

void StackCreateWizard::layout () {
    auto full = getLocalBounds ();
    if (full.isEmpty ()) return;
    auto b = full;

    headerRect_ = b.removeFromTop (56);
    backRect_ = { headerRect_.getX () + 16, headerRect_.getY (), 32, headerRect_.getHeight () };
    titleRect_ = headerRect_.withTrimmedLeft (56).withTrimmedRight (16);

    // 2 lines' worth of 12pt body text -- was 32px (~1.2 lines), which cut
    // the second wrapped line off at the right edge for longer subtitles.
    subtitleRect_ = b.removeFromTop (40).reduced (20, 0);
    b.removeFromTop (4);

    if (step_ != Step::Gallery) {
        pillsRowRect_ = b.removeFromTop (36).reduced (20, 0);
        b.removeFromTop (10);
        constexpr int gap = 8;
        const int pillW = (pillsRowRect_.getWidth () - (kStepCount - 1) * gap) / kStepCount;
        for (int i = 0; i < kStepCount; ++i)
            pillRects_[(size_t)i] = { pillsRowRect_.getX () + i * (pillW + gap),
                                      pillsRowRect_.getY (), pillW, pillsRowRect_.getHeight () };
    } else {
        pillsRowRect_ = {};
    }

    if (step_ != Step::Gallery) {
        footerRect_ = b.removeFromBottom (76);
        footerBtnRect_ = footerRect_.reduced (20, 16);
    } else {
        footerRect_ = {};
        footerBtnRect_ = {};
    }

    contentArea_ = b.reduced (20, 8);
    layoutContent ();
}

void StackCreateWizard::layoutContent () {
    const int w = contentArea_.getWidth ();
    switch (step_) {
        case Step::Gallery: {
            templateCardRects_.clear ();
            int y = 0;
            constexpr int cardH = 132, gap = 14;
            for (size_t i = 0; i < galleryTemplates_.size (); ++i) {
                templateCardRects_.push_back ({ 0, y, w, cardH });
                y += cardH + gap;
            }
            emptyBtnRect_ = { 0, y, w, 48 };
            y += 48;
            contentH_ = y;
            break;
        }
        case Step::Amp: {
            channelRowRects_.clear ();
            libraryRowRects_.clear ();
            int y = 0;
            channelsLabelRect_ = { 0, y, w, 18 };
            y += 24;
            const auto* amp = ampItem ();
            if (amp != nullptr)
                for (int c = 0; c < (int)amp->channels.size (); ++c) {
                    ChannelRowRect rr;
                    rr.body = { 0, y, w - 44, 52 };
                    rr.remove = { w - 40, y + 10, 32, 32 };
                    rr.channelIdx = c;
                    channelRowRects_.push_back (rr);
                    y += 52 + 8;
                }
            y += 12;
            libraryLabelRect_ = { 0, y, w, 18 };
            y += 24;
            for (size_t i = 0; i < models_.size (); ++i) {
                libraryRowRects_.push_back ({ 0, y, w, 52 });
                y += 52 + 8;
            }
            contentH_ = y;
            break;
        }
        case Step::Pedals: {
            pedalCardRects_.clear ();
            constexpr int gap = 12, cardH = 96;
            const int cardW = (w - gap) / 2;
            for (size_t i = 0; i < models_.size (); ++i) {
                const int col = (int)(i % 2), row = (int)(i / 2);
                pedalCardRects_.push_back (
                    { col * (cardW + gap), row * (cardH + gap), cardW, cardH });
            }
            const int rows = (int)((models_.size () + 1) / 2);
            contentH_ = rows > 0 ? rows * cardH + (rows - 1) * gap : 0;
            break;
        }
        case Step::Cab: {
            cabRowRects_.clear ();
            int y = 0;
            constexpr int rowH = 56, gap = 8;
            for (size_t i = 0; i < irs_.size (); ++i) {
                cabRowRects_.push_back ({ 0, y, w, rowH });
                y += rowH + gap;
            }
            contentH_ = y > 0 ? y - gap : 0;
            break;
        }
        case Step::Foot: {
            chocolatePanelRect_ = { 0, 0, w, 176 };
            // Sized to fit `w` rather than a fixed 64px: narrow phones (this
            // panel's own inset budget is < 4*64 + 3*22) were clipping
            // switch D off the right edge of the content area.
            constexpr int minGap = 14;
            const int d = juce::jlimit (40, 64, (w - 32 - 3 * minGap) / 4);
            const int gap = (w - 32 - 4 * d) / 3;
            const int sx = chocolatePanelRect_.getX () + 16;
            const int sy = chocolatePanelRect_.getY () + 56;
            for (int i = 0; i < 4; ++i) switchRects_[(size_t)i] = { sx + i * (d + gap), sy, d, d };

            int y = chocolatePanelRect_.getBottom () + 20;
            clearRowRect_ = { 0, y, w, 40 };
            y += 40 + 8;
            actionRowRects_.clear ();
            const auto actions = buildActions ();
            for (size_t i = 0; i < actions.size (); ++i) {
                actionRowRects_.push_back ({ 0, y, w, 52 });
                y += 52 + 8;
            }
            const auto warn = warningText ();
            if (warn.isNotEmpty ()) {
                warningRect_ = { 0, y, w, 48 };
                y += 48 + 8;
            } else {
                warningRect_ = {};
            }
            contentH_ = y;
            break;
        }
    }
    contentH_ = juce::jmax (contentH_, 0);
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()), scrollY_);
}

void StackCreateWizard::handleFooterTap (juce::Point<int>) {
    switch (step_) {
        case Step::Amp: goToStep (Step::Pedals); break;
        case Step::Pedals: goToStep (Step::Cab); break;
        case Step::Cab: goToStep (Step::Foot); break;
        case Step::Foot: doSave (); break;
        case Step::Gallery: break;
    }
}

void StackCreateWizard::handleContentTap (juce::Point<int> cp) {
    switch (step_) {
        case Step::Gallery: {
            for (size_t i = 0; i < templateCardRects_.size (); ++i)
                if (templateCardRects_[i].contains (cp)) {
                    pickTemplate ((int)i);
                    return;
                }
            if (emptyBtnRect_.contains (cp)) goToStep (Step::Amp);
            return;
        }
        case Step::Amp: {
            for (const auto& rr : channelRowRects_)
                if (rr.remove.contains (cp)) {
                    removeAmpChannel (rr.channelIdx);
                    return;
                }
            for (size_t i = 0; i < libraryRowRects_.size () && i < models_.size (); ++i)
                if (libraryRowRects_[i].contains (cp)) {
                    addAmpChannel (models_[i]);
                    return;
                }
            return;
        }
        case Step::Pedals: {
            for (size_t i = 0; i < pedalCardRects_.size () && i < models_.size (); ++i)
                if (pedalCardRects_[i].contains (cp)) {
                    togglePedal (models_[i]);
                    return;
                }
            return;
        }
        case Step::Cab: {
            for (size_t i = 0; i < cabRowRects_.size () && i < irs_.size (); ++i)
                if (cabRowRects_[i].contains (cp)) {
                    pickCab (irs_[i]);
                    return;
                }
            return;
        }
        case Step::Foot: {
            for (int i = 0; i < 4; ++i)
                if (switchRects_[(size_t)i].contains (cp)) {
                    armSwitch (i);
                    return;
                }
            if (armedSwitch_ < 0) return;
            if (clearRowRect_.contains (cp)) {
                clearArmed ();
                return;
            }
            const auto actions = buildActions ();
            for (size_t i = 0; i < actionRowRects_.size () && i < actions.size (); ++i)
                if (actionRowRects_[i].contains (cp)) {
                    assignArmedTo (actions[i]);
                    return;
                }
            return;
        }
    }
}
