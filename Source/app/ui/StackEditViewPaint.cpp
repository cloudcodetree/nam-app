#include "app/ui/StackEditView.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

// Painting only -- see StackEditView.cpp for state/layout/hit-testing (the
// no-god-files split rationale is documented there).
using namespace nam::ui;

namespace {
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");       // ·
const juce::String kEmDash = juce::String::fromUTF8 ("\xE2\x80\x94");   // —
const juce::String kArrow = juce::String::fromUTF8 ("\xE2\x86\x92");    // →
const juce::String kCheck = juce::String::fromUTF8 ("\xE2\x9C\x93");    // ✓
const juce::String kHint = "visual for now " + kEmDash + " audio support coming";

void drawAddPill (juce::Graphics& g, juce::Rectangle<int> r) {
    drawPill (g, r.toFloat (), juce::Colours::transparentBlack, col::inkA (0.24f));
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.7f));
    g.drawText ("+ ADD", r, juce::Justification::centred, false);
}

void drawEmptyDashed (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& msg) {
    juce::Path outline, dashed;
    outline.addRoundedRectangle (r.toFloat (), 10.0f);
    float lens[]{ 5.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, lens, 2);
    g.setColour (col::inkA (0.16f));
    g.fillPath (dashed);
    g.setFont (uiFont (11.0f, false));
    g.setColour (col::inkA (0.4f));
    g.drawText (msg, r, juce::Justification::centred, false);
}

void drawTriangle (juce::Graphics& g, juce::Rectangle<int> r, bool up) {
    auto b = r.toFloat ().withSizeKeepingCentre (10.0f, 8.0f);
    juce::Path p;
    if (up)
        p.addTriangle (b.getX (), b.getBottom (), b.getRight (), b.getBottom (), b.getCentreX (),
                       b.getY ());
    else
        p.addTriangle (b.getX (), b.getY (), b.getRight (), b.getY (), b.getCentreX (),
                       b.getBottom ());
    g.setColour (col::inkA (0.55f));
    g.fillPath (p);
}
}   // namespace

void StackEditView::paint (juce::Graphics& g) {
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("ROUTING", routingLabelRect_, juce::Justification::centredLeft, false);

    const int selIdx = stack_.routing == nam::Stack::Routing::AB       ? 1
                       : stack_.routing == nam::Stack::Routing::Stereo ? 2
                                                                       : 0;
    const char* labels[3] = { "SINGLE", "A/B", "STEREO" };
    for (int i = 0; i < 3; ++i) {
        const bool sel = i == selIdx;
        drawPill (g, routingPillRects_[(size_t)i].toFloat (),
                  sel ? col::accent : juce::Colours::transparentBlack,
                  sel ? col::accent : col::inkA (0.2f));
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (sel ? col::inkOnAccent : col::inkA (0.55f));
        g.drawText (labels[i], routingPillRects_[(size_t)i], juce::Justification::centred, false);
    }
    drawPill (g, freeformToggleRect_.toFloat (),
              freeform_ ? col::meterLime.withAlpha (0.16f) : juce::Colours::transparentBlack,
              freeform_ ? col::meterLime : col::inkA (0.2f));
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (freeform_ ? col::meterLime : col::inkA (0.6f));
    g.drawText (freeform_ ? "FREEFORM " + kCheck : juce::String ("FREEFORM"), freeformToggleRect_,
                juce::Justification::centred, false);

    g.saveState ();
    g.reduceClipRegion (contentArea_);
    const int dy = contentArea_.getY () - (int)scrollY_;
    if (freeform_) paintFreeform (g, dy);
    else paintGuided (g, dy);
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

    if (confirmOpen_) paintConfirm (g);
}

void StackEditView::paintGuided (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    const int cw = contentArea_.getWidth ();

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("PEDALS", tr (pedalsHeaderRect_), juce::Justification::centredLeft, false);
    g.setFont (uiFont (9.0f, false));
    g.setColour (col::inkA (0.3f));
    g.drawText (kHint, tr (pedalsHeaderRect_.translated (0, 16).withWidth (cw - 80)),
                juce::Justification::centredLeft, false);
    if (!pedalsAddRect_.isEmpty ()) drawAddPill (g, tr (pedalsAddRect_));
    if (pedalCardRects_.empty ())
        drawEmptyDashed (g, tr ({ 0, pedalsHeaderRect_.getBottom () + 18, cw, 40 }),
                         "no pedals yet");
    else
        for (const auto& pc : pedalCardRects_) {
            const auto* it = findItem (pc.uid);
            if (it == nullptr) continue;
            auto r = tr (pc.body);
            drawStompCardChrome (g, r, !it->bypassed);
            g.setFont (uiFont (9.0f, false));
            g.setColour (it->bypassed ? col::inkA (0.3f) : col::inkA (0.78f));
            g.drawText (juce::String (it->title),
                        r.reduced (8, 0).withY (r.getY () + 30).withHeight (24),
                        juce::Justification::centredTop, true);
            // Top-right, mirroring the LED at top-left -- keeps clear of the
            // knob rings drawn along the card's lower third.
            drawFsBadge (g, { r.getRight () - 30, r.getY () + 6, 22, 22 }, it->fs);
        }

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("AMP", tr (ampHeaderRect_), juce::Justification::centredLeft, false);
    if (!ampAddRect_.isEmpty ()) drawAddPill (g, tr (ampAddRect_));
    const auto* amp = ampUid_.isEmpty () ? nullptr : findItem (ampUid_);
    if (amp == nullptr) drawEmptyDashed (g, tr (ampCardRect_), "+ ADD AMP");
    else {
        auto r = tr (ampCardRect_);
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (r.toFloat (), 12.0f);
        g.setColour (col::inkA (0.12f));
        g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 12.0f, 1.0f);
        auto in = r.reduced (14, 10);
        auto top = in.removeFromTop (26);
        drawFsBadge (g, top.removeFromRight (26), amp->fs);
        g.setFont (displayFont (15.0f));
        g.setColour (col::ink);
        // The card headline follows the ACTIVE channel (item.title only
        // mirrors channels[0], per StackModel's own activeModelToneId
        // preference) so switching CH below updates the name up here too.
        juce::String ampName = juce::String (amp->title);
        if (!amp->channels.empty () && amp->activeChannel >= 0 &&
            amp->activeChannel < (int)amp->channels.size ())
            ampName = juce::String (amp->channels[(size_t)amp->activeChannel].title);
        g.drawText (elide (ampName, displayFont (15.0f), top.getWidth ()), top,
                    juce::Justification::centredLeft, false);
        in.removeFromTop (6);
        drawGrilleStrip (g, in.removeFromTop (24), col::inkA (0.5f));
        in.removeFromTop (8);
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (col::inkA (0.35f));
        g.drawText ("CH", in.removeFromTop (12), juce::Justification::centredLeft, false);
        for (size_t i = 0; i < ampChannelPillRects_.size () && i < amp->channels.size (); ++i) {
            auto pr = tr (ampChannelPillRects_[i]);
            const bool sel = (int)i == amp->activeChannel;
            drawPill (g, pr.toFloat (),
                      sel ? col::accentA (0.14f) : juce::Colours::transparentBlack,
                      sel ? col::accent : col::inkA (0.2f));
            auto pin = pr.reduced (8, 0);
            const auto dot = pin.removeFromLeft (12).withSizeKeepingCentre (6, 6).toFloat ();
            g.setColour (sel ? col::meterLime : col::inkA (0.3f));
            g.fillEllipse (dot);
            const auto pillFont = uiFont (10.0f, sel);
            g.setFont (pillFont);
            g.setColour (sel ? col::accent : col::inkA (0.65f));
            g.drawText (elide (juce::String (amp->channels[i].title), pillFont, pin.getWidth ()),
                        pin, juce::Justification::centredLeft, false);
        }
    }

    const auto* cab = cabUid_.isEmpty () ? nullptr : findItem (cabUid_);
    if (cab == nullptr) drawEmptyDashed (g, tr (cabAddRect_), "+ ADD CAB");
    else {
        auto r = tr (cabRowRect_);
        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (r.toFloat (), 10.0f);
        auto in = r.reduced (12, 8);
        drawConePair (g, in.removeFromLeft (56), col::inkA (0.6f));
        in.removeFromLeft (10);
        g.setFont (uiFont (12.0f, true));
        g.setColour (col::ink);
        g.drawText (juce::String (cab->title), in.removeFromTop (in.getHeight () / 2),
                    juce::Justification::bottomLeft, true);
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (col::inkA (0.4f));
        g.drawText ("CAB", in, juce::Justification::topLeft, false);
    }

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("POST " + kDotSep + " SPACES & OUTBOARD", tr (postHeaderRect_),
                juce::Justification::centredLeft, false);
    g.setFont (uiFont (9.0f, false));
    g.setColour (col::inkA (0.3f));
    g.drawText (kHint, tr (postHeaderRect_.translated (0, 16).withWidth (cw - 80)),
                juce::Justification::centredLeft, false);
    if (!postAddRect_.isEmpty ()) drawAddPill (g, tr (postAddRect_));
    if (postRowRects_.empty ())
        drawEmptyDashed (g, tr ({ 0, postHeaderRect_.getBottom () + 18, cw, 40 }),
                         "nothing here yet");
    else
        for (const auto& pr : postRowRects_) {
            const auto* it = findItem (pr.uid);
            if (it == nullptr) continue;
            auto r = tr (pr.body);
            g.setColour (col::inkA (0.02f));
            g.fillRoundedRectangle (r.toFloat (), 10.0f);
            auto in = r.reduced (14, 8);
            const auto led = in.removeFromLeft (18).withSizeKeepingCentre (8, 8).toFloat ();
            g.setColour (it->bypassed ? col::inkA (0.2f) : col::meterLime);
            g.fillEllipse (led);
            in.removeFromLeft (6);
            auto fsArea = in.removeFromRight (34);
            g.setFont (uiFont (12.0f, false));
            g.setColour (it->bypassed ? col::inkA (0.35f) : col::inkA (0.85f));
            g.drawText (juce::String (it->title), in, juce::Justification::centredLeft, true);
            drawFsBadge (g, fsArea.withSizeKeepingCentre (26, 26), it->fs);
        }

    auto rr = tr (removeStackRect_);
    drawPill (g, rr.toFloat (), juce::Colours::transparentBlack, col::accentA (0.3f));
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::accentAlt.withAlpha (0.85f));
    g.drawText ("REMOVE STACK", rr, juce::Justification::centred, false);
}

void StackEditView::paintFreeform (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };

    g.setFont (uiFont (11.0f, false));
    g.setColour (col::inkA (0.4f));
    g.drawText ("signal flows top " + kArrow + " bottom " + kDotSep + " reorder anything, anywhere",
                tr ({ 0, 0, contentArea_.getWidth (), 28 }), juce::Justification::centredLeft,
                true);

    const char* typeTag[4] = { "PEDAL", "AMP", "CAB", "POST" };
    for (const auto& fr : freeformRowRects_) {
        const auto* it = findItem (fr.uid);
        if (it == nullptr) continue;
        auto r = tr (fr.body);
        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (r.toFloat (), 10.0f);
        auto in = r.reduced (14, 8);
        in.removeFromRight (fr.upBtn.getWidth () + fr.downBtn.getWidth () + 8);
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (col::inkA (0.4f));
        g.drawText (typeTag[(int)it->type], in.removeFromTop (in.getHeight () / 2),
                    juce::Justification::bottomLeft, false);
        juce::String name = juce::String (it->title);
        if (it->type == nam::GearType::Amp && it->activeChannel >= 0 &&
            it->activeChannel < (int)it->channels.size ())
            name +=
                " " + kDotSep + " " + juce::String (it->channels[(size_t)it->activeChannel].title);
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::ink);
        g.drawText (name, in, juce::Justification::topLeft, true);

        drawTriangle (g, tr (fr.upBtn), true);
        drawTriangle (g, tr (fr.downBtn), false);
    }

    drawEmptyDashed (g, tr (freeformAddRect_), "+ ADD GEAR");

    auto rr = tr (removeStackRect_);
    drawPill (g, rr.toFloat (), juce::Colours::transparentBlack, col::accentA (0.3f));
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::accentAlt.withAlpha (0.85f));
    g.drawText ("REMOVE STACK", rr, juce::Justification::centred, false);
}

void StackEditView::paintConfirm (juce::Graphics& g) const {
    g.fillAll (col::scrim);
    g.setColour (col::sheetBg);
    g.fillRoundedRectangle (confirmRect_.toFloat (), 14.0f);
    g.setColour (col::inkA (0.18f));
    g.drawRoundedRectangle (confirmRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
    auto inner = confirmRect_.reduced (18, 16);
    auto msgArea = inner.removeFromTop (inner.getHeight () - 40);
    g.setFont (uiFont (14.0f, true));
    g.setColour (col::ink);
    g.drawText ("Remove '" + juce::String (stack_.name) + "'?", msgArea,
                juce::Justification::centred, true);
    drawPill (g, confirmCancelBtn_.toFloat (), juce::Colours::transparentBlack, col::inkA (0.24f));
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::inkA (0.8f));
    g.drawText ("CANCEL", confirmCancelBtn_, juce::Justification::centred, false);
    drawPill (g, confirmRemoveBtn_.toFloat (), col::accentA (0.18f), col::accentAlt);
    g.setColour (col::accentAlt);
    g.drawText ("REMOVE", confirmRemoveBtn_, juce::Justification::centred, false);
}
