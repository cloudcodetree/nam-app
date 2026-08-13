#include "app/ui/PaywallPanel.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
constexpr int kPad = 16;
constexpr int kHandleH = 18;
constexpr int kTitleH = 30;
constexpr int kReasonH = 24;
constexpr int kCheckRowH = 24;
constexpr int kNumChecks = 3;
constexpr int kButtonH = 46;
constexpr int kButtonGap = 12;
constexpr int kNotNowH = 30;
constexpr int kStatusH = 18;
constexpr int kGap = 10;
const char* kChecks[kNumChecks] = {
    "Unlimited rigs & stacks",
    "List & grid deck layouts",
    "Every DI audition track",
};
}   // namespace

PaywallPanel::PaywallPanel () {
    setOpaque (false);
    priceText_ = juce::String::fromUTF8 ("UNLOCK \xC2\xB7 $9.99");
}

void PaywallPanel::setReason (juce::String reason) {
    reason_ = std::move (reason);
    layout ();
    repaint ();
}

void PaywallPanel::setPriceText (juce::String price) {
    priceText_ = std::move (price);
    repaint ();
}

void PaywallPanel::setBusy (bool busy) {
    if (busy == busy_) return;
    busy_ = busy;
    repaint ();
}

void PaywallPanel::setStatus (juce::String status) {
    status_ = std::move (status);
    layout ();
    repaint ();
}

int PaywallPanel::computeContentHeight () const {
    int h = kPad + kHandleH + kGap + kTitleH + kReasonH + kGap;
    h += kCheckRowH * kNumChecks + kGap;
    h += kButtonH + kButtonGap + kButtonH + kGap;
    h += kNotNowH;
    if (status_.isNotEmpty ()) h += kStatusH;
    h += kPad;
    return h;
}

void PaywallPanel::layout () {
    contentH_ = computeContentHeight ();
    const int w = juce::jmin (380, getWidth () - 32);
    const int maxH = getHeight () * 55 / 100;   // overlay rule: height-capped
    const int h = juce::jmin (contentH_, juce::jmax (0, maxH));
    panelRect_ = { getWidth () / 2 - w / 2, getHeight () - h - 12, w, h };

    const int innerX = kPad;
    const int innerW = w - kPad * 2;
    int y = kPad + kHandleH + kGap + kTitleH + kReasonH + kGap + kCheckRowH * kNumChecks + kGap;
    buyRect_ = { innerX, y, innerW, kButtonH };
    y += kButtonH + kButtonGap;
    restoreRect_ = { innerX, y, innerW, kButtonH };
    y += kButtonH + kGap;
    notNowRect_ = { innerX, y, innerW, kNotNowH };

    scroll_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - panelRect_.getHeight ()), scroll_);
}

void PaywallPanel::resized () { layout (); }

void PaywallPanel::paint (juce::Graphics& g) {
    g.setColour (juce::Colour (0xf214101f));
    g.fillRoundedRectangle (panelRect_.toFloat (), 14.0f);
    g.setColour (col::inkA (0.18f));
    g.drawRoundedRectangle (panelRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);

    g.saveState ();
    juce::Path clip;
    clip.addRoundedRectangle (panelRect_.toFloat ().reduced (1.0f), 14.0f);
    g.reduceClipRegion (clip);

    // Content rects are stored content-local (x already absolute within the
    // panel's own local frame; y starts at 0). S() maps to screen space by
    // the panel's origin minus the current scroll offset — same convention
    // as StacksScreen's slot rows.
    const int dx = panelRect_.getX ();
    const int dy = panelRect_.getY () - (int)scroll_;
    auto S = [dx, dy] (juce::Rectangle<int> rr) { return rr.translated (dx, dy); };

    g.setColour (col::inkA (0.2f));
    g.fillRoundedRectangle (S ({ panelRect_.getWidth () / 2 - 18, kPad, 36, 4 }).toFloat (), 2.0f);

    int y = kPad + kHandleH + kGap;
    const int innerW = panelRect_.getWidth () - kPad * 2;
    g.setFont (displayFont (21.0f));
    g.setColour (col::ink);
    g.drawText ("NAM Player Pro", S ({ kPad, y, innerW, kTitleH }),
                juce::Justification::centredLeft, false);
    y += kTitleH;

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::inkA (0.55f));
    g.drawText (reason_, S ({ kPad, y, innerW, kReasonH }), juce::Justification::centredLeft,
                false);
    y += kReasonH + kGap;

    for (int i = 0; i < kNumChecks; ++i) {
        auto row = S ({ kPad, y, innerW, kCheckRowH });
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::accentAlt);
        g.drawText (juce::String::fromUTF8 ("\xE2\x9C\x93"), row.removeFromLeft (22),
                    juce::Justification::centredLeft, false);
        g.setColour (col::inkA (0.85f));
        g.drawText (kChecks[i], row, juce::Justification::centredLeft, false);
        y += kCheckRowH;
    }
    y += kGap;

    // BUY pill — price text, dimmed + relabeled while a purchase is in flight.
    auto buy = S (buyRect_);
    drawPill (g, buy.toFloat (), busy_ ? col::accentA (0.35f) : col::accent, col::accent);
    g.setFont (uiFontTracked (12.0f, true));
    g.setColour (busy_ ? col::inkOnAccent.withAlpha (0.6f) : col::inkOnAccent);
    g.drawText (busy_ ? "UNLOCKING..." : priceText_, buy, juce::Justification::centred, false);
    y += kButtonH + kButtonGap;

    // RESTORE ghost pill.
    auto restore = S (restoreRect_);
    drawPill (g, restore.toFloat (), juce::Colours::transparentBlack,
              col::inkA (busy_ ? 0.10f : 0.25f));
    g.setFont (uiFontTracked (11.0f, true));
    g.setColour (busy_ ? col::inkA (0.3f) : col::inkA (0.7f));
    g.drawText (busy_ ? "RESTORING..." : "RESTORE PURCHASE", restore, juce::Justification::centred,
                false);
    y += kButtonH + kGap;

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::inkA (0.45f));
    g.drawText ("not now", S (notNowRect_), juce::Justification::centred, false);
    y += kNotNowH;

    if (status_.isNotEmpty ()) {
        g.setFont (uiFont (11.0f, false));
        g.setColour (col::inkA (0.5f));
        g.drawText (status_, S ({ kPad, y, innerW, kStatusH }), juce::Justification::centred,
                    false);
    }

    g.restoreState ();

    if (contentH_ > panelRect_.getHeight ()) {
        const float frac = (float)panelRect_.getHeight () / (float)contentH_;
        const float thumbH = juce::jmax (24.0f, panelRect_.getHeight () * frac);
        const float travel = (float)panelRect_.getHeight () - thumbH - 8.0f;
        const float pos = scroll_ / (float)juce::jmax (1, contentH_ - panelRect_.getHeight ());
        g.setColour (col::inkA (0.25f));
        g.fillRoundedRectangle ((float)panelRect_.getRight () - 7.0f,
                                (float)panelRect_.getY () + 4.0f + travel * pos, 3.0f, thumbH,
                                1.5f);
    }
}

void PaywallPanel::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (panelRect_.contains (p)) {
        pressed_ = true;
        moved_ = false;
        pressPos_ = p;
        pressScroll_ = scroll_;
    } else {
        pressed_ = false;
        if (onClose) onClose ();
    }
}

void PaywallPanel::mouseDrag (const juce::MouseEvent& e) {
    if (!pressed_) return;
    const int dragDy = e.getPosition ().y - pressPos_.y;
    if (std::abs (dragDy) > 8) moved_ = true;
    if (moved_) {
        scroll_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - panelRect_.getHeight ()),
                                pressScroll_ - (float)dragDy);
        repaint (panelRect_.expanded (2));
    }
}

void PaywallPanel::mouseUp (const juce::MouseEvent& e) {
    if (!pressed_) return;
    const bool tap = !moved_;
    pressed_ = false;
    if (!tap) return;

    const auto p = e.getPosition ();
    const int dy = panelRect_.getY () - (int)scroll_;
    const juce::Point<int> cp{ p.x - panelRect_.getX (), p.y - dy };
    if (!busy_ && buyRect_.contains (cp)) {
        if (onBuy) onBuy ();
        return;
    }
    if (!busy_ && restoreRect_.contains (cp)) {
        if (onRestore) onRestore ();
        return;
    }
    if (notNowRect_.contains (cp)) {
        if (onClose) onClose ();
        return;
    }
}
