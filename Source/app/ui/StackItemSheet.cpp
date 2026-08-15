#include "app/ui/StackItemSheet.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kSwapLabel = juce::String::fromUTF8 ("\xE2\x87\x84") + " SWAP GEAR";   // ⇄

const char* typeLabel (nam::GearType t) {
    switch (t) {
        case nam::GearType::Amp: return "AMP";
        case nam::GearType::Cab: return "CAB";
        case nam::GearType::Post: return "POST";
        case nam::GearType::Pedal:
        default: return "PEDAL";
    }
}

juce::String fsPillLabel (int i) {
    return i == 0 ? juce::String ("NONE") : "FS" + juce::String (i);
}
}   // namespace

StackItemSheet::StackItemSheet () {
    setInterceptsMouseClicks (true, true);
    setVisible (false);
}

void StackItemSheet::open (const nam::ChainItem& item) {
    item_ = item;
    if (auto* parent = getParentComponent ()) setBounds (parent->getLocalBounds ());
    setVisible (true);
    toFront (false);
    layout ();
    repaint ();
}

void StackItemSheet::close () { setVisible (false); }

void StackItemSheet::layout () {
    auto full = getLocalBounds ();
    if (full.isEmpty ()) return;

    const int h = juce::jmin (560, full.getHeight () * 72 / 100);   // overlay rule: height-capped
    sheetRect_ = full.removeFromBottom (h);
    contentRect_ = sheetRect_.reduced (20, 16);

    const bool pedalOrPost =
        item_.type == nam::GearType::Pedal || item_.type == nam::GearType::Post;
    const bool isAmp = item_.type == nam::GearType::Amp;
    const int w = contentRect_.getWidth ();
    constexpr int gap = 16, pillGap = 8;

    int y = 0;
    typeLabelRect_ = { 0, y, w, 16 };
    y += 20;
    nameRect_ = { 0, y, w, 28 };
    y += 28 + gap;

    if (pedalOrPost) {
        bypassPillRect_ = { 0, y, 168, 34 };
        y += 34 + gap;
    } else bypassPillRect_ = {};

    channelPillRects_.clear ();
    if (isAmp) {
        channelsLabelRect_ = { 0, y, w, 16 };
        y += 20;
        constexpr int pillH = 32;
        int cx = 0, rowY = y;
        for (const auto& ch : item_.channels) {
            const int pw = juce::jmax (64, (int)juce::GlyphArrangement::getStringWidth (
                                               uiFont (11.0f, true), juce::String (ch.title)) +
                                               28);
            if (cx + pw > w && cx > 0) {
                cx = 0;
                rowY += pillH + pillGap;
            }
            channelPillRects_.push_back ({ cx, rowY, pw, pillH });
            cx += pw + pillGap;
        }
        constexpr int addW = 108;
        if (cx + addW > w && cx > 0) {
            cx = 0;
            rowY += pillH + pillGap;
        }
        addChannelRect_ = { cx, rowY, addW, pillH };
        y = rowY + pillH + gap;
    } else {
        channelsLabelRect_ = {};
        addChannelRect_ = {};
    }

    fsLabelRect_ = { 0, y, w, 16 };
    y += 20;
    {
        constexpr int pillW = 68, pillH = 32;
        int cx = 0, rowY = y;
        for (int i = 0; i < 9; ++i) {
            if (cx + pillW > w && cx > 0) {
                cx = 0;
                rowY += pillH + pillGap;
            }
            fsPillRects_[(size_t)i] = { cx, rowY, pillW, pillH };
            cx += pillW + pillGap;
        }
        y = rowY + pillH + gap;
    }

    if (pedalOrPost) {
        const int bw = (w - 12) / 2;
        swapBtnRect_ = { 0, y, bw, 44 };
        removeBtnRect_ = { bw + 12, y, bw, 44 };
    } else {
        swapBtnRect_ = { 0, y, w, 44 };
        removeBtnRect_ = {};
    }
    y += 44;

    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentRect_.getHeight ()), scrollY_);
}

void StackItemSheet::resized () { layout (); }

void StackItemSheet::paint (juce::Graphics& g) {
    if (!isVisible ()) return;
    g.fillAll (juce::Colour (0xa008070f));

    g.setColour (juce::Colour (0xf214101f));
    g.fillRoundedRectangle (sheetRect_.toFloat (), 16.0f);
    g.setColour (col::inkA (0.16f));
    g.drawRoundedRectangle (sheetRect_.toFloat ().reduced (0.5f), 16.0f, 1.0f);

    g.saveState ();
    g.reduceClipRegion (contentRect_);
    const int dy = contentRect_.getY () - (int)scrollY_;
    auto tr = [&] (juce::Rectangle<int> r) { return r.translated (contentRect_.getX (), dy); };

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText (typeLabel (item_.type), tr (typeLabelRect_), juce::Justification::centredLeft,
                false);
    g.setFont (displayFont (18.0f));
    g.setColour (col::ink);
    g.drawText (juce::String (item_.title), tr (nameRect_), juce::Justification::centredLeft, true);

    if (!bypassPillRect_.isEmpty ()) {
        const auto r = tr (bypassPillRect_);
        const bool on = !item_.bypassed;
        drawPill (g, r.toFloat (),
                  on ? col::meterLime.withAlpha (0.14f) : juce::Colours::transparentBlack,
                  on ? col::meterLime : col::inkA (0.24f));
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (on ? col::meterLime : col::inkA (0.55f));
        g.drawText (on ? "ON" : "BYPASSED", r, juce::Justification::centred, false);
    }

    if (!channelsLabelRect_.isEmpty ()) {
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (col::inkA (0.4f));
        g.drawText ("CHANNELS", tr (channelsLabelRect_), juce::Justification::centredLeft, false);
        for (size_t i = 0; i < channelPillRects_.size (); ++i) {
            const auto r = tr (channelPillRects_[i]);
            const bool sel = (int)i == item_.activeChannel;
            drawPill (g, r.toFloat (), sel ? col::accentA (0.14f) : juce::Colours::transparentBlack,
                      sel ? col::accent : col::inkA (0.2f));
            auto in = r.reduced (10, 0);
            const auto dot = in.removeFromLeft (14).withSizeKeepingCentre (7, 7).toFloat ();
            g.setColour (sel ? col::meterLime : col::inkA (0.3f));
            g.fillEllipse (dot);
            g.setFont (uiFont (11.0f, sel));
            g.setColour (sel ? col::accent : col::inkA (0.7f));
            g.drawText (juce::String (item_.channels[i].title), in,
                        juce::Justification::centredLeft, true);
        }
        const auto addR = tr (addChannelRect_);
        juce::Path dashed, outline;
        outline.addRoundedRectangle (addR.toFloat (), 8.0f);
        float dashLens[]{ 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashLens, 2);
        g.setColour (col::inkA (0.28f));
        g.fillPath (dashed);
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (col::inkA (0.55f));
        g.drawText ("+ CAPTURE", addR, juce::Justification::centred, false);
    }

    g.setFont (uiFontTracked (9.0f, true));
    g.setColour (col::inkA (0.4f));
    g.drawText ("FOOTSWITCH", tr (fsLabelRect_), juce::Justification::centredLeft, false);
    for (int i = 0; i < 9; ++i) {
        const auto r = tr (fsPillRects_[(size_t)i]);
        const bool sel = item_.fs == i;
        drawPill (g, r.toFloat (), sel ? col::accentA (0.14f) : juce::Colours::transparentBlack,
                  sel ? col::accent : col::inkA (0.2f));
        g.setFont (uiFont (10.0f, sel));
        g.setColour (sel ? col::accent : col::inkA (0.6f));
        g.drawText (fsPillLabel (i), r, juce::Justification::centred, false);
    }

    {
        const auto r = tr (swapBtnRect_);
        drawPill (g, r.toFloat (), juce::Colours::transparentBlack, col::inkA (0.24f));
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (col::inkA (0.8f));
        g.drawText (kSwapLabel, r, juce::Justification::centred, false);
    }
    if (!removeBtnRect_.isEmpty ()) {
        const auto r = tr (removeBtnRect_);
        drawPill (g, r.toFloat (), juce::Colours::transparentBlack, col::accentA (0.4f));
        g.setFont (uiFontTracked (10.0f, true));
        g.setColour (col::accentAlt);
        g.drawText ("REMOVE", r, juce::Justification::centred, false);
    }

    g.restoreState ();

    if (contentH_ > contentRect_.getHeight ()) {
        const float frac = (float)contentRect_.getHeight () / (float)contentH_;
        const float thumbH = juce::jmax (24.0f, contentRect_.getHeight () * frac);
        const float travel = (float)contentRect_.getHeight () - thumbH - 8.0f;
        const float pos = scrollY_ / (float)juce::jmax (1, contentH_ - contentRect_.getHeight ());
        g.setColour (col::inkA (0.22f));
        g.fillRoundedRectangle ((float)contentRect_.getRight () - 4.0f,
                                (float)contentRect_.getY () + 4.0f + travel * pos, 3.0f, thumbH,
                                1.5f);
    }
}

void StackItemSheet::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;
    moved_ = false;
    pressed_ = false;

    if (!sheetRect_.contains (p)) {
        close ();
        if (onDismiss) onDismiss ();
        return;
    }
    if (contentRect_.contains (p)) {
        pressed_ = true;
        pressScrollY_ = scrollY_;
    }
}

void StackItemSheet::mouseDrag (const juce::MouseEvent& e) {
    if (!pressed_) return;
    const auto p = e.getPosition ();
    const int dy = p.y - pressPos_.y;
    if (std::abs (dy) > 8) moved_ = true;
    if (moved_) {
        scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - contentRect_.getHeight ()),
                                 pressScrollY_ - (float)dy);
        repaint (contentRect_);
    }
}

void StackItemSheet::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    pressed_ = false;
    if (!tap || !contentRect_.contains (p)) return;

    const juce::Point<int> cp{ p.x - contentRect_.getX (),
                               p.y - contentRect_.getY () + (int)scrollY_ };
    const auto uid = juce::String (item_.uid);

    if (!bypassPillRect_.isEmpty () && bypassPillRect_.contains (cp)) {
        if (onToggleBypass) onToggleBypass (uid);
        return;
    }
    for (size_t i = 0; i < channelPillRects_.size (); ++i)
        if (channelPillRects_[i].contains (cp)) {
            if (onSetChannel) onSetChannel (uid, (int)i);
            return;
        }
    if (!addChannelRect_.isEmpty () && addChannelRect_.contains (cp)) {
        if (onAddChannel) onAddChannel (uid);
        return;
    }
    for (int i = 0; i < 9; ++i)
        if (fsPillRects_[(size_t)i].contains (cp)) {
            if (onSetFs) onSetFs (uid, i);
            return;
        }
    if (swapBtnRect_.contains (cp)) {
        if (onSwap) onSwap (uid);
        return;
    }
    if (!removeBtnRect_.isEmpty () && removeBtnRect_.contains (cp)) {
        if (onRemove) onRemove (uid);
        return;
    }
}
