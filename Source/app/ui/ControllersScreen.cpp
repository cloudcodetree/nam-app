#include "app/ui/ControllersScreen.h"

#include "app/ui/NamLookAndFeel.h"

using namespace nam;
using namespace nam::ui;

namespace {

const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
const juce::String kDash = juce::String::fromUTF8 ("\xE2\x80\x94");        // —

constexpr int kRowH = 56;
constexpr int kCardPad = 14;
constexpr std::size_t kMonitorRows = 5;

// Trailing control widths. layout() and paintBindingRow() MUST agree on
// these -- the label reserve was previously a hardcoded 178 that drifted out
// of sync with the buttons and elided every long action name.
constexpr int kClearW = 26, kPolicyW = 52, kLearnW = 62, kCtlGap = 6;
constexpr int kControlsW = kClearW + kCtlGap + kPolicyW + kCtlGap + kLearnW;

// "CC 20" / "CC 20 ch3" / "NOTE 60" / "PC 5" / "KEY 92" (a BLE HID pedal
// arriving as a keyboard). Channel is omitted when 0,
// which is what learned bindings use ("any channel") -- printing "ch0" would
// read as a real channel number.
juce::String describeSignature (const ControlSignature& s) {
    juce::String kind = s.kind == ControlKind::Note            ? "NOTE"
                        : s.kind == ControlKind::ProgramChange ? "PC"
                        : s.kind == ControlKind::Key           ? "KEY"
                                                               : "CC";
    juce::String out = kind + " " + juce::String (s.number);
    if (s.channel != 0) out += " ch" + juce::String (s.channel);
    return out;
}

juce::String describePolicy (FirePolicy p) {
    switch (p) {
        case FirePolicy::Momentary: return "MOM";
        case FirePolicy::Toggle: return "TOG";
        case FirePolicy::Auto: break;
    }
    return "AUTO";
}

void paintCardBg (juce::Graphics& g, juce::Rectangle<int> r) {
    g.setColour (col::inkA (0.05f));
    g.fillRoundedRectangle (r.toFloat (), 12.0f);
    g.setColour (col::inkA (0.12f));
    g.drawRoundedRectangle (r.toFloat ().reduced (0.5f), 12.0f, 1.0f);
}

void paintButton (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
                  bool accent) {
    drawPill (g, r.toFloat (), accent ? col::accentA (0.18f) : col::inkA (0.06f),
              accent ? col::accent : col::inkA (0.18f), 1.0f);
    g.setFont (uiFontTracked (11.0f, true));
    g.setColour (accent ? col::accent : col::inkA (0.75f));
    g.drawText (text, r, juce::Justification::centred, false);
}

}   // namespace

ControllersScreen::ControllersScreen () { setOpaque (true); }

void ControllersScreen::setDevices (std::vector<juce::String> names) {
    devices_ = std::move (names);
    repaint ();
}

void ControllersScreen::setBindings (std::vector<ControlBinding> bindings) {
    bindings_ = std::move (bindings);
    layout ();
    repaint ();
}

void ControllersScreen::setLearning (ControlAction a) {
    learning_ = a;
    repaint ();
}

void ControllersScreen::setPairingAvailable (bool available) {
    pairingAvailable_ = available;
    layout ();
    repaint ();
}

void ControllersScreen::pushEvent (const ControlEvent& e) {
    monitor_.push_front (describeSignature (e.sig) + "   " + juce::String (e.value));
    while (monitor_.size () > kMonitorRows) monitor_.pop_back ();
    repaint ();
}

const ControlBinding* ControllersScreen::bindingFor (ControlAction a) const {
    for (const auto& b : bindings_)
        if (b.action == a) return &b;
    return nullptr;
}

void ControllersScreen::layout () {
    // Single functional header row; content starts immediately below it.
    auto b = getLocalBounds ().withTrimmedTop (12);
    auto headerRow = b.removeFromTop (52).reduced (20, 8);
    backRect_ = headerRow.removeFromLeft (28);
    titleRect_ = headerRow;

    contentArea_ = b.reduced (20, 4);

    // Content-local layout.
    int y = 0;
    const int w = contentArea_.getWidth ();

    // Device card: status lines + PAIR / RESCAN buttons.
    const int deviceH =
        kCardPad * 2 + 22 + (devices_.empty () ? 20 : 20 * (int)devices_.size ()) + 42;
    juce::Rectangle<int> deviceCard (0, y, w, deviceH);
    auto btnRow = deviceCard.reduced (kCardPad).removeFromBottom (34);
    if (pairingAvailable_) {
        pairRect_ = btnRow.removeFromLeft (juce::jmin (150, btnRow.getWidth () / 2 - 4));
        btnRow.removeFromLeft (8);
    } else {
        pairRect_ = {};
    }
    rescanRect_ = btnRow.removeFromLeft (juce::jmin (120, btnRow.getWidth ()));
    y += deviceH + 12;

    // Monitor card.
    y += kCardPad * 2 + 18 + 18 * (int)kMonitorRows + 12;

    // Binding rows.
    rowRects_.clear ();
    for (auto action : allControlActions ()) {
        RowRect r;
        r.action = action;
        r.body = { 0, y, w, kRowH };
        auto inner = r.body.reduced (kCardPad, 10);
        r.clearBtn = inner.removeFromRight (kClearW);
        inner.removeFromRight (kCtlGap);
        r.policyBtn = inner.removeFromRight (kPolicyW);
        inner.removeFromRight (kCtlGap);
        r.learnBtn = inner.removeFromRight (kLearnW);
        rowRects_.push_back (r);
        y += kRowH + 8;
    }

    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()), scrollY_);
}

void ControllersScreen::resized () { layout (); }

void ControllersScreen::paintDeviceCard (juce::Graphics& g, juce::Rectangle<int> r) const {
    paintCardBg (g, r);
    auto inner = r.reduced (kCardPad);

    g.setFont (uiFontTracked (11.0f, true));
    g.setColour (col::inkA (0.5f));
    g.drawText ("CONTROLLER", inner.removeFromTop (16), juce::Justification::centredLeft, false);
    inner.removeFromTop (6);

    g.setFont (uiFont (14.0f, false));
    if (devices_.empty ()) {
        g.setColour (col::inkA (0.55f));
        g.drawText ("Nothing connected", inner.removeFromTop (20), juce::Justification::centredLeft,
                    true);
    } else {
        g.setColour (col::ink);
        for (const auto& d : devices_)
            g.drawText (d, inner.removeFromTop (20), juce::Justification::centredLeft, true);
    }

    if (!pairRect_.isEmpty ()) paintButton (g, pairRect_, "PAIR BLUETOOTH", true);
    paintButton (g, rescanRect_, "RESCAN", false);
}

void ControllersScreen::paintMonitorCard (juce::Graphics& g, juce::Rectangle<int> r) const {
    paintCardBg (g, r);
    auto inner = r.reduced (kCardPad);

    g.setFont (uiFontTracked (11.0f, true));
    g.setColour (col::inkA (0.5f));
    g.drawText ("INCOMING", inner.removeFromTop (16), juce::Justification::centredLeft, false);
    inner.removeFromTop (4);

    if (monitor_.empty ()) {
        g.setFont (uiFont (13.0f, false));
        g.setColour (col::inkA (0.4f));
        g.drawText ("Stomp a switch on your pedal", inner.removeFromTop (18),
                    juce::Justification::centredLeft, true);
        return;
    }

    g.setFont (uiFont (13.0f, false));
    float alpha = 0.9f;
    for (const auto& line : monitor_) {
        g.setColour (col::inkA (alpha));
        g.drawText (line, inner.removeFromTop (18), juce::Justification::centredLeft, false);
        alpha = juce::jmax (0.25f, alpha - 0.15f);
    }
}

void ControllersScreen::paintBindingRow (juce::Graphics& g, const juce::Rectangle<int>& body,
                                         ControlAction action) const {
    paintCardBg (g, body);

    const auto* b = bindingFor (action);
    const bool isLearning = learning_ == action;

    auto text = body.reduced (kCardPad, 8);
    text.removeFromRight (kControlsW + kCtlGap);   // clear the trailing controls

    g.setFont (uiFont (15.0f, false));
    g.setColour (col::ink);
    g.drawText (controlActionLabel (action), text.removeFromTop (20),
                juce::Justification::centredLeft, true);

    g.setFont (uiFont (12.0f, false));
    if (isLearning) {
        g.setColour (col::accent);
        g.drawText ("press a switch...", text, juce::Justification::centredLeft, false);
    } else if (b != nullptr) {
        g.setColour (col::inkA (0.6f));
        g.drawText (describeSignature (b->sig), text, juce::Justification::centredLeft, false);
    } else {
        g.setColour (col::inkA (0.35f));
        g.drawText (kDash, text, juce::Justification::centredLeft, false);
    }
}

void ControllersScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());

    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.7f));
    g.drawText (kBackGlyph, backRect_, juce::Justification::centredLeft, false);

    g.setFont (displayFont (20.0f));
    g.setColour (col::ink);
    g.drawText ("Controllers", titleRect_, juce::Justification::centredLeft, true);

    g.saveState ();
    g.reduceClipRegion (contentArea_);
    g.setOrigin (contentArea_.getX (), contentArea_.getY () - (int)scrollY_);

    const int w = contentArea_.getWidth ();
    int y = 0;
    const int deviceH =
        kCardPad * 2 + 22 + (devices_.empty () ? 20 : 20 * (int)devices_.size ()) + 42;
    paintDeviceCard (g, { 0, y, w, deviceH });
    y += deviceH + 12;

    const int monitorH = kCardPad * 2 + 18 + 18 * (int)kMonitorRows + 12;
    paintMonitorCard (g, { 0, y, w, monitorH });

    for (const auto& r : rowRects_) {
        paintBindingRow (g, r.body, r.action);
        const auto* b = bindingFor (r.action);
        paintButton (g, r.learnBtn, learning_ == r.action ? "CANCEL" : "LEARN",
                     learning_ == r.action);
        paintButton (g, r.policyBtn, describePolicy (b != nullptr ? b->policy : FirePolicy::Auto),
                     false);
        if (b != nullptr) {
            g.setFont (uiFont (16.0f, false));
            g.setColour (col::inkA (0.45f));
            g.drawText (juce::String::fromUTF8 ("\xC3\x97"), r.clearBtn,
                        juce::Justification::centred, false);
        }
    }

    g.restoreState ();
}

void ControllersScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    dragging_ = false;
    pressScrollY_ = scrollY_;

    if (backRect_.expanded (8).contains (p)) {
        if (onBack) onBack ();
        return;
    }
}

void ControllersScreen::mouseDrag (const juce::MouseEvent& e) {
    if (!contentArea_.contains (e.getMouseDownPosition ())) return;
    if (e.getDistanceFromDragStartY () != 0) dragging_ = true;
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentArea_.getHeight ()),
                             pressScrollY_ - (float)e.getDistanceFromDragStartY ());
    repaint ();
}

void ControllersScreen::mouseUp (const juce::MouseEvent& e) {
    // A drag scrolls; only a genuine tap activates a control (press/drag/tap
    // state machine, per the house rules).
    if (dragging_) {
        dragging_ = false;
        return;
    }
    const auto p = e.getPosition ();
    if (!contentArea_.contains (p)) return;
    handleContentTap ({ p.x - contentArea_.getX (), p.y - contentArea_.getY () + (int)scrollY_ });
}

void ControllersScreen::handleContentTap (juce::Point<int> p) {
    if (!pairRect_.isEmpty () && pairRect_.expanded (4).contains (p)) {
        if (onPair) onPair ();
        return;
    }
    if (rescanRect_.expanded (4).contains (p)) {
        if (onRescan) onRescan ();
        return;
    }

    for (const auto& r : rowRects_) {
        if (r.learnBtn.expanded (4).contains (p)) {
            if (onLearn) onLearn (r.action);
            return;
        }
        if (r.policyBtn.expanded (4).contains (p)) {
            if (onCyclePolicy) onCyclePolicy (r.action);
            return;
        }
        // Guard the empty rect: an unbound row has no × drawn, and
        // empty.expanded(k) would make a live region near the origin.
        if (!r.clearBtn.isEmpty () && bindingFor (r.action) != nullptr &&
            r.clearBtn.expanded (4).contains (p)) {
            if (onClear) onClear (r.action);
            return;
        }
    }
}
