#include "app/ui/AudioSettingsScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9"); // ‹
const juce::String kCheck     = juce::String::fromUTF8 ("\xE2\x9C\x93"); // ✓
const juce::String kDot       = juce::String::fromUTF8 ("\xC2\xB7");     // ·
constexpr int kDragThreshold = 8;

juce::String dbLabel (float peak) {
    if (peak < 0.0005f) return juce::String::fromUTF8 ("\xE2\x88\x92\xE2\x88\x9E dB"); // −∞
    const float db = 20.0f * std::log10 (peak);
    return (db > -0.05f ? "0" : juce::String (db, 1)) + " dB";
}

juce::String deviceMeta (const juce::String& name, bool input) {
    if (name.containsIgnoreCase ("usb") || name.containsIgnoreCase ("irig"))
        return "USB audio";
    if (name.containsIgnoreCase ("default"))
        return input ? "system routing" : "system routing";
    if (name.containsIgnoreCase ("built-in") || name.containsIgnoreCase ("speaker")
        || name.containsIgnoreCase ("microphone"))
        return input ? "built-in" : "built-in";
    if (name.containsIgnoreCase ("bluetooth"))
        return "Bluetooth";
    return input ? "input" : "output";
}
}

AudioSettingsScreen::AudioSettingsScreen() { setOpaque (true); startTimerHz (30); }
AudioSettingsScreen::~AudioSettingsScreen() { stopTimer(); }

void AudioSettingsScreen::setState (AudioSettingsState s) {
    st_ = std::move (s);
    relayout();
    repaint();
}

void AudioSettingsScreen::setLevels (float inPeak, float outPeak) {
    inPeak_  = juce::jlimit (0.0f, 1.0f, inPeak);
    outPeak_ = juce::jlimit (0.0f, 1.0f, outPeak);
    if (gain_ == Gain::Listening) gainMax_ = juce::jmax (gainMax_, inPeak);
}

void AudioSettingsScreen::timerCallback() {
    if (gain_ == Gain::Listening && ++gainTicks_ > 120) {   // ~4 s window
        gain_ = Gain::Applied;
        gainTicks_ = 0;
        appliedDb_ = 0.0f;
        if (gainMax_ > 0.02f)
            appliedDb_ = juce::jlimit (-24.0f, 24.0f,
                                       20.0f * std::log10 (0.25f / gainMax_));
        if (onSetInputDb) onSetInputDb (appliedDb_);
    } else if (gain_ == Gain::Applied && ++gainTicks_ > 75) {
        gain_ = Gain::Idle;
        gainTicks_ = 0;
    }
    if (! isVisible()) return;
    // Repaint only the live areas (meters + gain button), scroll-adjusted.
    const int dy = contentTop_ - (int) scrollY_;
    for (auto r : { meterInRow_, meterOutRow_, gainBtn_ }) {
        const auto vis = r.translated (0, dy).expanded (4)
                             .getIntersection (getLocalBounds().withTrimmedTop (contentTop_));
        if (! vis.isEmpty()) repaint (vis);
    }
}

void AudioSettingsScreen::resized() { relayout(); }

void AudioSettingsScreen::relayout() {
    auto r = getLocalBounds();
    topBar_ = r.removeFromTop (juce::jmax (60, r.getHeight() / 13));
    backRect_   = { topBar_.getX() + 12, topBar_.getCentreY() - 20, 40, 40 };
    statusRect_ = { topBar_.getRight() - 20 - 160, topBar_.getCentreY() - 10, 160, 20 };
    contentTop_ = r.getY();

    const int x = 20, w = getWidth() - 40;
    const int pad = 16, rowH = 56, gap = 8, headH = 26, chipH = 34;
    int y = 0;
    cards_.clear();

    auto beginCard = [&] { return y; };
    auto endCard = [&] (int top) {
        cards_.push_back ({ x, top, w, y - top + pad });
        y += pad + 14;
    };

    // --- DEVICE: INPUT ---
    int top = beginCard();  y += pad;
    inHeader_   = { x + pad, y, w - pad * 2 - 84, headH };
    rescanRect_ = { x + w - pad - 80, y - 4, 80, 30 };
    y += headH;
    inRects_.clear();
    for (int i = 0; i < st_.inputs.size(); ++i) {
        inRects_.push_back ({ x + pad, y, w - pad * 2, rowH - gap });
        y += rowH;
    }
    endCard (top);

    // --- DEVICE: OUTPUT ---
    top = beginCard();  y += pad;
    outHeader_ = { x + pad, y, w - pad * 2, headH };  y += headH;
    outRects_.clear();
    for (int i = 0; i < st_.outputs.size(); ++i) {
        outRects_.push_back ({ x + pad, y, w - pad * 2, rowH - gap });
        y += rowH;
    }
    endCard (top);

    // --- ENGINE ---
    top = beginCard();  y += pad;
    engineHeader_ = { x + pad, y, w - pad * 2, headH };  y += headH;
    rateRow_ = { x + pad, y, w - pad * 2, chipH };
    rateRects_.clear();
    {
        auto chips = rateRow_.withTrimmedLeft (84);
        const int n = juce::jmax (1, st_.rates.size());
        const int cw = (chips.getWidth() - (n - 1) * 6) / n;
        for (int i = 0; i < st_.rates.size(); ++i)
            rateRects_.push_back ({ chips.getX() + i * (cw + 6), chips.getY(), cw, chipH });
    }
    y += chipH + 12;
    bufRow_ = { x + pad, y, w - pad * 2, chipH };
    bufRects_.clear();
    {
        auto chips = bufRow_.withTrimmedLeft (84);
        const int n = juce::jmax (1, st_.buffers.size());
        const int cw = (chips.getWidth() - (n - 1) * 6) / n;
        for (int i = 0; i < st_.buffers.size(); ++i)
            bufRects_.push_back ({ chips.getX() + i * (cw + 6), chips.getY(), cw, chipH });
    }
    y += chipH + 12;
    latencyRow_ = { x + pad, y, w - pad * 2, 26 };  y += 26;
    endCard (top);

    // --- LEVELS ---
    top = beginCard();  y += pad;
    levelsHeader_ = { x + pad, y, w - pad * 2, headH };  y += headH;
    meterInRow_  = { x + pad, y, w - pad * 2, 24 };  y += 28;
    meterOutRow_ = { x + pad, y, w - pad * 2, 24 };  y += 32;
    gainBtn_     = { x + pad, y, w - pad * 2, 42 };  y += 42;
    endCard (top);

    contentH_ = y + 16;
    clampScroll();
}

void AudioSettingsScreen::clampScroll() {
    const float maxScroll = (float) juce::jmax (0, contentH_ - (getHeight() - contentTop_));
    scrollY_ = juce::jlimit (0.0f, maxScroll, scrollY_);
}

juce::Point<int> AudioSettingsScreen::toContent (juce::Point<int> p) const {
    return { p.x, p.y - contentTop_ + (int) scrollY_ };
}

void AudioSettingsScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // --- Scrolling content ---------------------------------------------
    g.saveState();
    g.reduceClipRegion (0, contentTop_, getWidth(), getHeight() - contentTop_);
    const int dy = contentTop_ - (int) scrollY_;
    auto S = [dy] (juce::Rectangle<int> rr) { return rr.translated (0, dy); };

    for (const auto& c : cards_) {
        auto cc = S (c).toFloat();
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (cc, 14.0f);
        g.setColour (col::inkA (0.12f));
        g.drawRoundedRectangle (cc.reduced (0.5f), 14.0f, 1.0f);
    }

    auto sectionLabel = [&] (juce::Rectangle<int> rr, const juce::String& s) {
        text (s, uiFontTracked (10.0f, true), col::inkA (0.45f), S (rr),
              juce::Justification::centredLeft);
    };
    auto deviceRow = [&] (juce::Rectangle<int> rr, const juce::String& name,
                          bool selected, bool input) {
        auto sr = S (rr).toFloat();
        g.setColour (selected ? col::accentA (0.06f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (sr, 10.0f);
        g.setColour (selected ? col::accentA (0.5f) : col::inkA (0.10f));
        g.drawRoundedRectangle (sr.reduced (0.5f), 10.0f, 1.0f);
        auto inner = S (rr).reduced (14, 6);
        g.setColour (col::meterLime);
        g.fillEllipse ((float) inner.getX(), (float) inner.getCentreY() - 4.0f, 8.0f, 8.0f);
        auto tx = inner.withTrimmedLeft (18);
        if (selected)
            text (kCheck, uiFont (13.0f, false), col::accentAlt,
                  tx.removeFromRight (20), juce::Justification::centred);
        text (name, uiFont (14.0f, true), selected ? col::ink : col::inkA (0.8f),
              tx.removeFromTop (tx.getHeight() / 2 + 4), juce::Justification::bottomLeft);
        const bool routed = ! input && name.containsIgnoreCase ("default")
                            && st_.outputRouteHint.isNotEmpty();
        text (routed ? "system routing " + juce::String::fromUTF8 ("\xE2\x86\x92") + " "
                           + st_.outputRouteHint
                     : deviceMeta (name, input),
              uiFont (11.0f, false), routed ? col::meterLime.withAlpha (0.7f) : col::inkA (0.45f),
              tx, juce::Justification::topLeft);
    };
    auto chip = [&] (juce::Rectangle<int> rr, const juce::String& label, bool on) {
        drawPill (g, S (rr).toFloat(), on ? col::accentA (0.12f) : juce::Colours::transparentBlack,
                  on ? col::accentA (0.6f) : col::inkA (0.16f));
        text (label, uiFont (12.0f, true), on ? col::accentAlt : col::inkA (0.6f),
              S (rr), juce::Justification::centred);
    };
    auto meter = [&] (juce::Rectangle<int> rr, const juce::String& label, float peak,
                      bool tickTarget, bool orangeTail) {
        auto row = S (rr);
        text (label, uiFontTracked (10.0f, true), col::inkA (0.4f),
              row.removeFromLeft (32), juce::Justification::centredLeft);
        auto val = row.removeFromRight (52);
        text (dbLabel (peak), uiFont (11.0f, true), col::inkA (0.55f), val,
              juce::Justification::centredRight);
        auto bar = row.reduced (6, 0).withSizeKeepingCentre (row.getWidth() - 12, 8).toFloat();
        g.setColour (col::inkA (0.10f));
        g.fillRoundedRectangle (bar, 4.0f);
        const float w = juce::jmax (4.0f, bar.getWidth() * peak);
        juce::ColourGradient mg (col::meterGreen, bar.getX(), 0,
                                 orangeTail ? col::accent : col::meterLime, bar.getX() + w, 0, false);
        if (orangeTail) mg.addColour (0.7, col::meterLime);
        g.setGradientFill (mg);
        juce::Path p; p.addRoundedRectangle (bar.withWidth (w), 4.0f);
        g.fillPath (p);
        if (tickTarget) {
            g.setColour (col::inkA (0.5f));
            g.fillRect (bar.getX() + bar.getWidth() * 0.82f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        }
    };

    // DEVICE cards
    sectionLabel (inHeader_, "INPUT " + kDot + " GUITAR");
    drawPill (g, S (rescanRect_).toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text ("RESCAN", uiFontTracked (10.0f, true), col::accentAlt, S (rescanRect_),
          juce::Justification::centred);
    for (int i = 0; i < st_.inputs.size() && i < (int) inRects_.size(); ++i)
        deviceRow (inRects_[(size_t) i], st_.inputs[i], st_.inputs[i] == st_.currentInput, true);
    if (st_.inputs.isEmpty())
        text ("No inputs found", uiFont (13.0f, false), col::inkA (0.4f),
              S (inHeader_.translated (0, 34)), juce::Justification::centredLeft);

    sectionLabel (outHeader_, "OUTPUT " + kDot + " AMP / PHONES");
    for (int i = 0; i < st_.outputs.size() && i < (int) outRects_.size(); ++i)
        deviceRow (outRects_[(size_t) i], st_.outputs[i], st_.outputs[i] == st_.currentOutput, false);

    // ENGINE card
    sectionLabel (engineHeader_, "ENGINE");
    text ("sample rate", uiFont (13.0f, false), col::inkA (0.6f),
          S (rateRow_).removeFromLeft (80), juce::Justification::centredLeft);
    for (int i = 0; i < st_.rates.size() && i < (int) rateRects_.size(); ++i)
        chip (rateRects_[(size_t) i], st_.rates[i], st_.rates[i] == st_.currentRate);
    text ("buffer", uiFont (13.0f, false), col::inkA (0.6f),
          S (bufRow_).removeFromLeft (80), juce::Justification::centredLeft);
    for (int i = 0; i < st_.buffers.size() && i < (int) bufRects_.size(); ++i)
        chip (bufRects_[(size_t) i], st_.buffers[i], st_.buffers[i] == st_.currentBuffer);
    {
        auto lr = S (latencyRow_);
        g.setColour (col::inkA (0.08f));
        g.fillRect (lr.getX(), lr.getY() - 2, lr.getWidth(), 1);
        text ("round-trip latency", uiFont (12.0f, false), col::inkA (0.5f), lr,
              juce::Justification::centredLeft);
        const bool good = st_.latencyMs > 0 && st_.latencyMs <= 15.0;
        text (st_.latencyMs > 0 ? juce::String (st_.latencyMs, 1) + " ms" : "--",
              uiFont (12.0f, true), good ? col::meterLime : col::accentAlt, lr,
              juce::Justification::centredRight);
    }

    // LEVELS card
    sectionLabel (levelsHeader_, "LEVELS");
    meter (meterInRow_,  "IN",  inPeak_,  true,  false);
    meter (meterOutRow_, "OUT", outPeak_, false, true);
    {
        const bool listening = (gain_ == Gain::Listening);
        drawPill (g, S (gainBtn_).toFloat(),
                  listening ? col::accentA (0.10f) : juce::Colours::transparentBlack,
                  listening ? col::accentA (0.6f) : col::inkA (0.22f));
        juce::String label = "RE-RUN AUTO GAIN-STAGE";
        if (gain_ == Gain::Listening) label = "listening" + juce::String::fromUTF8 ("\xE2\x80\xA6") + " play a few chords";
        if (gain_ == Gain::Applied)   label = "input set " + kDot + " " + juce::String (appliedDb_, 1) + " dB";
        text (label, uiFontTracked (12.0f, true),
              listening || gain_ == Gain::Applied ? col::accentAlt : col::inkA (0.7f),
              S (gainBtn_), juce::Justification::centred);
    }

    g.restoreState();

    // --- Fixed header (over content) ------------------------------------
    g.setColour (col::bg);
    g.fillRect (topBar_);
    text (kBackGlyph, uiFont (22.0f, false), col::inkA (0.6f), backRect_,
          juce::Justification::centred);
    text ("Audio", displayFont (30.0f), col::ink,
          { backRect_.getRight() + 4, topBar_.getY(), 200, topBar_.getHeight() },
          juce::Justification::centredLeft);
    text (st_.running ? "engine running" : "engine stopped", uiFont (12.0f, true),
          st_.running ? col::meterLime : col::accentAlt, statusRect_,
          juce::Justification::centredRight);
}

void AudioSettingsScreen::mouseDown (const juce::MouseEvent& e) {
    pressY_ = e.getPosition().y;
    pressScroll_ = scrollY_;
    dragged_ = false;
}

void AudioSettingsScreen::mouseDrag (const juce::MouseEvent& e) {
    if (e.getMouseDownPosition().y <= topBar_.getBottom()) return;
    const int delta = e.getPosition().y - pressY_;
    if (std::abs (delta) > kDragThreshold) dragged_ = true;
    if (dragged_) {
        scrollY_ = pressScroll_ - (float) delta;
        clampScroll();
        repaint();
    }
}

void AudioSettingsScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragged_) return;
    const auto p = e.getPosition();
    if (backRect_.expanded (10).contains (p)) { if (onBack) onBack(); return; }
    if (topBar_.contains (p)) return;

    const auto cp = toContent (p);
    if (rescanRect_.contains (cp)) { if (onRescan) onRescan(); return; }
    if (gainBtn_.contains (cp)) {
        if (gain_ == Gain::Idle) { gain_ = Gain::Listening; gainTicks_ = 0; gainMax_ = 0.0f; }
        return;
    }
    for (int i = 0; i < (int) inRects_.size() && i < st_.inputs.size(); ++i)
        if (inRects_[(size_t) i].contains (cp)) { if (onSelectInput) onSelectInput (st_.inputs[i]); return; }
    for (int i = 0; i < (int) outRects_.size() && i < st_.outputs.size(); ++i)
        if (outRects_[(size_t) i].contains (cp)) { if (onSelectOutput) onSelectOutput (st_.outputs[i]); return; }
    for (int i = 0; i < (int) rateRects_.size() && i < st_.rates.size(); ++i)
        if (rateRects_[(size_t) i].contains (cp)) { if (onSelectRate) onSelectRate (st_.rates[i]); return; }
    for (int i = 0; i < (int) bufRects_.size() && i < st_.buffers.size(); ++i)
        if (bufRects_[(size_t) i].contains (cp)) { if (onSelectBuffer) onSelectBuffer (st_.buffers[i]); return; }
}

void AudioSettingsScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    scrollY_ -= w.deltaY * 120.0f;
    clampScroll();
    repaint();
}
