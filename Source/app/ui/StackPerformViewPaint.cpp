#include "app/ui/StackPerformView.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"
#include "model/StackModel.h"

// Painting only -- see StackPerformView.cpp for state/layout/hit-testing
// (the no-god-files split rationale is documented there).
using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
const juce::String kFwdGlyph = juce::String::fromUTF8 ("\xE2\x80\xBA");    // ›
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");          // ·
const juce::String kEmDash = juce::String::fromUTF8 ("\xE2\x80\x94");      // —
}   // namespace

void StackPerformView::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    paintHeader (g);
    paintModeToggle (g);

    g.saveState ();
    g.reduceClipRegion (contentArea_);
    paintGrid (g, contentArea_.getY () - (int)scrollY_);
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
}

void StackPerformView::paintHeader (juce::Graphics& g) const {
    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.7f));
    g.drawText (kBackGlyph, exitRect_, juce::Justification::centred, false);

    g.setFont (uiFont (15.0f, false));
    g.setColour (col::inkA (0.45f));
    g.drawText (kBackGlyph, prevRect_, juce::Justification::centred, false);
    g.drawText (kFwdGlyph, nextRect_, juce::Justification::centred, false);

    auto label = headerLabelRect_;
    auto top = label.removeFromTop (label.getHeight () / 2);
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.45f));
    g.drawText ("SETLIST " + kDotSep + " " + juce::String (pos_) + "/" + juce::String (count_), top,
                juce::Justification::centredBottom, false);
    g.setFont (displayFont (17.0f));
    g.setColour (col::ink);
    g.drawText (juce::String (stack_.name), label, juce::Justification::centredTop, true);
}

void StackPerformView::paintModeToggle (juce::Graphics& g) const {
    auto pill = [&] (juce::Rectangle<int> r, const juce::String& label, bool active) {
        drawPill (g, r.toFloat (), active ? col::accent : juce::Colours::transparentBlack,
                  active ? col::accent : col::inkA (0.2f));
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (active ? col::inkOnAccent : col::inkA (0.6f));
        g.drawText (label, r, juce::Justification::centred, false);
    };
    pill (scenesToggleRect_, "SCENES", !stompMode_);
    pill (stompToggleRect_, "STOMP", stompMode_);
}

void StackPerformView::paintGrid (juce::Graphics& g, int dy) const {
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentArea_.getX (), dy); };
    for (const auto& cell : cells_) paintCell (g, cell, tr (cell.body));
}

void StackPerformView::paintCell (juce::Graphics& g, const Cell& cell,
                                  juce::Rectangle<int> r) const {
    bool active = false, ledOn = false, showLed = false;
    juce::String title, sub;

    switch (cell.kind) {
        case CellKind::Scene: {
            active = stack_.activeScene == cell.index;
            const auto& sc = stack_.scenes[(size_t)cell.index];
            title = juce::String (sc.name).isNotEmpty () ? juce::String (sc.name)
                                                         : "SCENE " + juce::String (cell.index + 1);
            sub = "SCENE " + juce::String (cell.index + 1);
            break;
        }
        case CellKind::Amp: {
            title = "AMP";
            const auto* amp = nam::StackModel::activeAmp (stack_);
            if (amp != nullptr && !amp->channels.empty () && amp->activeChannel >= 0 &&
                amp->activeChannel < (int)amp->channels.size ())
                sub = juce::String (amp->channels[(size_t)amp->activeChannel].title);
            break;
        }
        case CellKind::Tap:
            title = "TAP";
            sub = bpm_ > 1.0 ? juce::String (bpm_, 0) + " BPM" : juce::String (kEmDash);
            break;
        case CellKind::Tuner: title = "TUNER"; break;
        case CellKind::Next: title = "NEXT"; break;
        case CellKind::Stomp: {
            title = "FS" + juce::String (cell.index);
            showLed = true;
            if (cell.uid.isNotEmpty ()) {
                const auto* it = findChainItem (cell.uid);
                if (it != nullptr && it->type == nam::GearType::Amp) {
                    // An amp-mapped switch cycles channels, not bypass (see
                    // AppShellPerform.cpp's onStompTap) -- the sub-label
                    // tracks the ACTIVE CHANNEL's title, same as the SCENES
                    // grid's AMP cell, instead of the item's own static
                    // title (which a channel cycle never touches). No
                    // bypass concept applies here, so the LED just stays lit.
                    // Falls back to the item's own title when channels is
                    // empty (only reachable via a hand-edited/future-version
                    // stacks.json -- every in-app producer seeds channels[0])
                    // so the mapped gear's identity is never blank.
                    sub = (!it->channels.empty () && it->activeChannel >= 0 &&
                           it->activeChannel < (int)it->channels.size ())
                              ? juce::String (it->channels[(size_t)it->activeChannel].title)
                              : juce::String (it->title);
                    ledOn = true;
                } else if (it != nullptr) {
                    sub = juce::String (it->title);
                    ledOn = !it->bypassed;
                }
            } else {
                sub = kEmDash;
            }
            break;
        }
    }

    // A fixed corner radius, not drawPill's stadium shape (radius =
    // height/2) -- these cells got tall to give stage use big touch
    // targets (see layoutGrid), and a stadium radius on a tall-narrow cell
    // degenerates into an egg/oval instead of a switch.
    const auto rf = r.toFloat ();
    constexpr float radius = 16.0f;
    g.setColour (active ? col::accentA (0.16f) : col::inkA (0.03f));
    g.fillRoundedRectangle (rf, radius);
    g.setColour (active ? col::accent : col::inkA (0.16f));
    g.drawRoundedRectangle (rf.reduced (0.6f), radius, 1.2f);

    if (showLed) {
        const auto led = juce::Rectangle<float> (r.getX () + 10.0f, r.getY () + 10.0f, 7.0f, 7.0f);
        g.setColour (ledOn ? col::meterLime : col::inkA (0.2f));
        g.fillEllipse (led);
    }

    auto titleRow = r.reduced (10, 8);
    auto subRow = titleRow.removeFromBottom (titleRow.getHeight () / 2);
    if (showLed) titleRow.removeFromLeft (12);   // clears the LED dot painted above
    // NEXT's forward glyph is a vector triangle, not the "▸" unicode
    // character -- Work Sans doesn't cover U+25B8 on Android, where it
    // rendered as a bare dot instead of an arrow (see drawFwdTriangle).
    if (cell.kind == CellKind::Next) {
        const auto triBox = titleRow.removeFromRight (12);
        drawFwdTriangle (g, triBox.withSizeKeepingCentre (6, 8).toFloat (),
                         active ? col::accent : col::inkA (0.6f));
    }
    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (active ? col::accent : col::inkA (0.6f));
    g.drawText (title, titleRow, juce::Justification::centredLeft, true);
    g.setFont (uiFont (10.0f, false));
    g.setColour (col::inkA (0.5f));
    g.drawText (sub, subRow, juce::Justification::centredLeft, true);
}
