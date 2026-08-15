#include "app/ui/StackCreateWizard.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

// Painting only -- see StackCreateWizard.cpp for state/layout/hit-testing
// (the no-god-files split rationale is documented there).
using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");     // ‹
const juce::String kEmDash = juce::String::fromUTF8 ("\xE2\x80\x94");        // —
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");            // ·
const juce::String kCheck = juce::String::fromUTF8 ("\xE2\x9C\x93");         // ✓
const juce::String kArrow = juce::String::fromUTF8 ("\xE2\x86\x92");         // →
const juce::String kBtDot = juce::String::fromUTF8 ("\xE2\x97\x8F");         // ●
const juce::String kRemoveGlyph = juce::String::fromUTF8 ("\xE2\x9C\x95");   // ✕
const std::array<juce::String, 4> kSwitchLetters{ "A", "B", "C", "D" };
const std::array<juce::String, 4> kPillLabels{ "1 AMP", "2 PEDALS", "3 CAB", "4 FOOT" };
}   // namespace

void StackCreateWizard::paint (juce::Graphics& g) {
    if (!isVisible ()) return;
    paintHeroBackground (g, getLocalBounds ());
    paintHeader (g);

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::inkA (0.45f));
    g.drawFittedText (juce::String::fromUTF8 (stepSubtitle (step_)), subtitleRect_,
                      juce::Justification::topLeft, 2);

    if (step_ != Step::Gallery) paintStepPills (g);

    g.saveState ();
    g.reduceClipRegion (contentArea_);
    paintContent (g, contentArea_.getY () - (int)scrollY_);
    g.restoreState ();

    if (contentH_ > contentArea_.getHeight ()) {
        const float frac = (float)contentArea_.getHeight () / (float)contentH_;
        const float thumbH = juce::jmax (24.0f, contentArea_.getHeight () * frac);
        const float travel = (float)contentArea_.getHeight () - thumbH - 8.0f;
        const float pos = scrollY_ / (float)juce::jmax (1, contentH_ - contentArea_.getHeight ());
        g.setColour (col::inkA (0.2f));
        g.fillRoundedRectangle ((float)contentArea_.getRight () - 4.0f,
                                (float)contentArea_.getY () + 4.0f + travel * pos, 3.0f, thumbH,
                                1.5f);
    }

    if (step_ != Step::Gallery) paintFooter (g);
}

void StackCreateWizard::paintHeader (juce::Graphics& g) const {
    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.7f));
    g.drawText (kBackGlyph, backRect_, juce::Justification::centredLeft, false);

    g.setFont (displayFont (20.0f));
    g.setColour (col::ink);
    g.drawText (juce::String (stepTitle (step_)), titleRect_, juce::Justification::centredLeft,
                true);
}

void StackCreateWizard::paintStepPills (juce::Graphics& g) const {
    for (int i = 0; i < kStepCount; ++i) {
        const auto r = pillRects_[(size_t)i];
        const bool current = (int)step_ - 1 == i;
        const bool visited = stepVisited_[(size_t)i];
        drawPill (g, r.toFloat (), current ? col::accent : juce::Colours::transparentBlack,
                  current ? col::accent : (visited ? col::meterLime : col::inkA (0.2f)));
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (current ? col::inkOnAccent : (visited ? col::meterLime : col::inkA (0.5f)));
        const auto label =
            (visited && !current ? kCheck + " " : juce::String ()) + kPillLabels[(size_t)i];
        g.drawText (label, r, juce::Justification::centred, false);
    }
}

void StackCreateWizard::paintContent (juce::Graphics& g, int dy) const {
    switch (step_) {
        case Step::Gallery: paintGallery (g, dy); break;
        case Step::Amp: paintAmpStep (g, dy); break;
        case Step::Pedals: paintPedalsStep (g, dy); break;
        case Step::Cab: paintCabStep (g, dy); break;
        case Step::Foot: paintFootStep (g, dy); break;
    }
}

void StackCreateWizard::paintGallery (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    for (size_t i = 0; i < galleryTemplates_.size (); ++i) {
        const auto& t = galleryTemplates_[i];
        auto r = tr (templateCardRects_[i]);
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (r.toFloat (), 14.0f);
        g.setColour (col::inkA (0.14f));
        g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 14.0f, 1.0f);

        auto in = r.reduced (16, 12);
        auto top = in.removeFromTop (22);
        g.setFont (displayFont (16.0f));
        g.setColour (col::ink);
        g.drawText (juce::String (t.name), top, juce::Justification::centredLeft, true);

        auto tagRow = in.removeFromTop (16);
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (col::accentAlt);
        g.drawText (juce::String (t.genre).toUpperCase (), tagRow, juce::Justification::centredLeft,
                    false);

        int amps = 0, pedals = 0, cabs = 0;
        for (const auto& it : t.stack.chain) {
            if (it.type == nam::GearType::Amp) ++amps;
            else if (it.type == nam::GearType::Pedal) ++pedals;
            else if (it.type == nam::GearType::Cab) ++cabs;
        }
        juce::StringArray parts;
        if (amps > 0) parts.add (juce::String (amps) + (amps > 1 ? " AMPS" : " AMP"));
        if (pedals > 0) parts.add (juce::String (pedals) + " PEDALS");
        if (cabs > 0) parts.add (juce::String (cabs) + " CAB");
        auto partsRow = in.removeFromTop (16);
        g.setFont (uiFontTracked (8.0f, false));
        g.setColour (col::inkA (0.5f));
        g.drawText (parts.joinIntoString (" " + kDotSep + " "), partsRow,
                    juce::Justification::centredLeft, false);

        in.removeFromTop (6);
        auto fsRow = in;
        const int cellW = fsRow.getWidth () / 4;
        for (int s = 0; s < 4; ++s) {
            auto cell = fsRow.removeFromLeft (cellW);
            const nam::ChainItem* bound = nullptr;
            for (const auto& it : t.stack.chain)
                if (it.fs == s + 1) {
                    bound = &it;
                    break;
                }
            const auto label =
                bound != nullptr ? juce::String (bound->title) : juce::String ("Tap tempo");
            g.setFont (uiFontTracked (8.0f, true));
            g.setColour (col::inkA (0.6f));
            g.drawText (kSwitchLetters[(size_t)s], cell.removeFromTop (14),
                        juce::Justification::centredLeft, false);
            g.setFont (uiFont (8.5f, false));
            g.setColour (col::inkA (0.4f));
            g.drawText (label, cell, juce::Justification::centredLeft, true);
        }
    }

    auto er = tr (emptyBtnRect_);
    drawPill (g, er.toFloat (), juce::Colours::transparentBlack, col::inkA (0.28f));
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::inkA (0.8f));
    g.drawText ("START EMPTY " + kEmDash + " BUILD STEP BY STEP", er, juce::Justification::centred,
                false);
}

void StackCreateWizard::paintAmpStep (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("CHANNELS ON THIS STACK", tr (channelsLabelRect_), juce::Justification::centredLeft,
                false);

    const auto* amp = ampItem ();
    for (const auto& rr : channelRowRects_) {
        auto body = tr (rr.body);
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (body.toFloat (), 10.0f);
        auto in = body.reduced (12, 0);
        auto dot = in.removeFromLeft (16).withSizeKeepingCentre (8, 8).toFloat ();
        g.setColour (col::meterLime);
        g.fillEllipse (dot);
        in.removeFromLeft (6);
        auto tagCol = in.removeFromRight (44);
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::ink);
        const auto title = (amp != nullptr && rr.channelIdx < (int)amp->channels.size ())
                               ? juce::String (amp->channels[(size_t)rr.channelIdx].title)
                               : juce::String ();
        g.drawText (title, in, juce::Justification::centredLeft, true);
        drawPill (g, tagCol.withSizeKeepingCentre (36, 16).toFloat (),
                  juce::Colours::transparentBlack, col::inkA (0.2f));
        g.setFont (uiFontTracked (7.0f, true));
        g.setColour (col::inkA (0.5f));
        g.drawText ("NAM", tagCol.withSizeKeepingCentre (36, 16), juce::Justification::centred,
                    false);

        auto rm = tr (rr.remove);
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::inkA (0.5f));
        g.drawText (kRemoveGlyph, rm, juce::Justification::centred, false);
    }

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("FROM YOUR LIBRARY", tr (libraryLabelRect_), juce::Justification::centredLeft,
                false);
    for (size_t i = 0; i < libraryRowRects_.size () && i < models_.size (); ++i) {
        auto r = tr (libraryRowRects_[i]);
        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (r.toFloat (), 10.0f);
        g.setColour (col::inkA (0.1f));
        g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 10.0f, 1.0f);
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::ink);
        g.drawText (juce::String (models_[i].displayName), r.reduced (12, 0),
                    juce::Justification::centredLeft, true);
    }
}

void StackCreateWizard::paintPedalsStep (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    for (size_t i = 0; i < pedalCardRects_.size () && i < models_.size (); ++i) {
        const auto& e = models_[i];
        const bool on = pedalIncluded (e.id);
        auto r = tr (pedalCardRects_[i]);
        drawStompCardChrome (g, r, seededHue (juce::String (e.id)), on);
        auto in = r.reduced (10);
        auto nameR = in.removeFromBottom (18);
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (on ? col::ink : col::inkA (0.5f));
        g.drawText (juce::String (e.displayName), nameR, juce::Justification::centredLeft, true);
    }
}

void StackCreateWizard::paintCabStep (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    const auto* cab = cabItem ();
    for (size_t i = 0; i < cabRowRects_.size () && i < irs_.size (); ++i) {
        const auto& e = irs_[i];
        const bool sel = cab != nullptr && cab->toneId == e.id;
        auto r = tr (cabRowRects_[i]);
        drawPill (g, r.toFloat (), sel ? col::accentA (0.14f) : col::inkA (0.02f),
                  sel ? col::accent : col::inkA (0.14f));
        g.setFont (uiFont (12.0f, sel));
        g.setColour (sel ? col::accent : col::ink);
        g.drawText (juce::String (e.displayName), r.reduced (14, 0),
                    juce::Justification::centredLeft, true);
    }
}

void StackCreateWizard::paintFootStep (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };

    auto panel = tr (chocolatePanelRect_);
    juce::ColourGradient grad (col::bgGradTop, (float)panel.getX (), (float)panel.getY (), col::bg,
                               (float)panel.getX (), (float)panel.getBottom (), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (panel.toFloat (), 18.0f);
    g.setColour (col::inkA (0.16f));
    g.drawRoundedRectangle (panel.toFloat ().reduced (0.5f), 18.0f, 1.0f);

    g.setFont (uiFontTracked (8.0f, true));
    g.setColour (col::meterLime);
    const juce::Rectangle<int> btRect{ panel.getX (), panel.getY () + 12, panel.getWidth (), 14 };
    g.drawText (kBtDot + " BT MIDI", btRect, juce::Justification::centred, false);

    for (int i = 0; i < 4; ++i) {
        auto r = switchRects_[(size_t)i].translated (contentArea_.getX (), dy);
        const bool armed = armedSwitch_ == i;
        const auto& sw = switches_[(size_t)i];
        g.setColour (armed ? col::accentA (0.22f) : col::inkA (0.06f));
        g.fillEllipse (r.toFloat ());
        g.setColour (armed ? col::accent : col::inkA (0.3f));
        g.drawEllipse (r.toFloat ().reduced (1.0f), armed ? 2.2f : 1.2f);
        g.setFont (displayFont (16.0f));
        g.setColour (col::ink);
        g.drawText (kSwitchLetters[(size_t)i], r.withTrimmedBottom (r.getHeight () / 2),
                    juce::Justification::centred, false);
        const auto sub = sw.tapTempo            ? juce::String ("TAP")
                         : sw.uid.isNotEmpty () ? juce::String ("SET")
                                                : kEmDash;
        g.setFont (uiFontTracked (7.0f, true));
        g.setColour (col::inkA (0.5f));
        g.drawText (sub, r.withTrimmedTop (r.getHeight () / 2), juce::Justification::centred,
                    false);
    }

    auto clearR = tr (clearRowRect_);
    const bool clearEnabled = armedSwitch_ >= 0;
    drawPill (g, clearR.toFloat (), juce::Colours::transparentBlack,
              col::inkA (clearEnabled ? 0.24f : 0.1f));
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (clearEnabled ? 0.7f : 0.3f));
    g.drawText (kEmDash + " nothing " + kEmDash, clearR, juce::Justification::centred, false);

    const auto actions = buildActions ();
    for (size_t i = 0; i < actionRowRects_.size () && i < actions.size (); ++i) {
        const auto& a = actions[i];
        auto r = tr (actionRowRects_[i]);
        int boundSwitch = -1;
        for (int s = 0; s < 4; ++s) {
            const auto& sw = switches_[(size_t)s];
            if (a.uid.isEmpty () ? sw.tapTempo : sw.uid == a.uid) {
                boundSwitch = s;
                break;
            }
        }
        const bool tappable = armedSwitch_ >= 0;
        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (r.toFloat (), 10.0f);
        if (tappable) {
            g.setColour (col::accentA (0.3f));
            g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 10.0f, 1.0f);
        }
        auto in = r.reduced (12, 0);
        auto badge = in.removeFromLeft (28);
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (boundSwitch >= 0 ? col::accent : col::inkA (0.3f));
        g.drawText (boundSwitch >= 0 ? kSwitchLetters[(size_t)boundSwitch] : kEmDash, badge,
                    juce::Justification::centred, false);
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::ink);
        g.drawText (a.label, in, juce::Justification::centredLeft, true);
    }

    if (!warningRect_.isEmpty ()) {
        auto r = tr (warningRect_);
        g.setColour (col::accentA (0.1f));
        g.fillRoundedRectangle (r.toFloat (), 10.0f);
        g.setColour (col::accentA (0.4f));
        g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 10.0f, 1.0f);
        g.setFont (uiFont (11.0f, false));
        g.setColour (col::accentAlt);
        g.drawFittedText (warningText (), r.reduced (12, 6), juce::Justification::centredLeft, 2);
    }
}

void StackCreateWizard::paintFooter (juce::Graphics& g) const {
    const bool save = step_ == Step::Foot;
    const auto label = save ? (kCheck + " SAVE STACK")
                            : ("NEXT: " + juce::String (nextStepLabel (step_)) + " " + kArrow);
    const auto fill = save ? col::meterLime : col::accent;
    drawPill (g, footerBtnRect_.toFloat (), fill, fill);
    g.setFont (uiFontTracked (11.0f, true));
    g.setColour (col::inkOnAccent);
    g.drawText (label, footerBtnRect_, juce::Justification::centred, false);
}
