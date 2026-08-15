#include "app/ui/StackEditView.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

// Painting only -- see StackEditView.cpp for state/layout/hit-testing (the
// no-god-files split rationale is documented there).
using namespace nam::ui;

namespace {
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");      // ·
const juce::String kArrow = juce::String::fromUTF8 ("\xE2\x86\x92");   // →

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
    g.saveState ();
    g.reduceClipRegion (contentArea_);
    const int dy = contentArea_.getY () - (int)scrollY_;
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
        auto thumbR = in.removeFromLeft (in.getHeight ());
        in.removeFromLeft (10);
        drawGearThumb (g, thumbR, thumbFor (*it), it->type, 8.0f);
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
