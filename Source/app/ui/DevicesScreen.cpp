#include "app/ui/DevicesScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9"); // ‹
const juce::String kDotGlyph  = juce::String::fromUTF8 ("\xE2\x97\x89"); // ◉
constexpr int kDragThreshold = 8;
}

DevicesScreen::DevicesScreen() { setOpaque (true); }

void DevicesScreen::setDevices (juce::StringArray inputs,  juce::String currentInput,
                                juce::StringArray outputs, juce::String currentOutput) {
    inputs_    = std::move (inputs);
    outputs_   = std::move (outputs);
    currentIn_  = std::move (currentInput);
    currentOut_ = std::move (currentOutput);
    relayout();
    repaint();
}

void DevicesScreen::resized() { relayout(); }

void DevicesScreen::relayout() {
    auto r = getLocalBounds();
    topBar_ = r.removeFromTop (juce::jmax (56, r.getHeight() / 14));
    backRect_   = { topBar_.getX() + 20, topBar_.getCentreY() - 16, 84, 32 };
    rescanRect_ = { topBar_.getRight() - 20 - 96, topBar_.getCentreY() - 16, 96, 32 };
    contentTop_ = r.getY();

    // Content space (y from 0), full width minus side margins.
    const int x = 26, w = getWidth() - 52;
    const int rowH = 54, gap = 10, headH = 30;
    int y = 0;

    titleRect_ = { x, y, w, 64 };  y += 64;

    inHeader_ = { x, y, w, headH };  y += headH;
    inRects_.clear();
    for (int i = 0; i < inputs_.size(); ++i) {
        inRects_.push_back ({ x, y, w, rowH - gap });
        y += rowH;
    }

    y += 14;
    outHeader_ = { x, y, w, headH };  y += headH;
    outRects_.clear();
    for (int i = 0; i < outputs_.size(); ++i) {
        outRects_.push_back ({ x, y, w, rowH - gap });
        y += rowH;
    }

    contentH_ = y + 24;
    clampScroll();
}

void DevicesScreen::clampScroll() {
    const float maxScroll = (float) juce::jmax (0, contentH_ - (getHeight() - contentTop_));
    scrollY_ = juce::jlimit (0.0f, maxScroll, scrollY_);
}

juce::Point<int> DevicesScreen::toContent (juce::Point<int> p) const {
    return { p.x, p.y - contentTop_ + (int) scrollY_ };
}

void DevicesScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // --- Scrolling content ---------------------------------------------
    g.saveState();
    g.reduceClipRegion (0, contentTop_, getWidth(), getHeight() - contentTop_);
    const int dy = contentTop_ - (int) scrollY_;
    auto shifted = [dy] (juce::Rectangle<int> rr) { return rr.translated (0, dy); };

    {
        auto tr = shifted (titleRect_);
        text ("SIGNAL PATH", uiFontTracked (12.0f, true), col::inkA (0.45f),
              tr.removeFromTop (22), juce::Justification::topLeft);
        text ("Devices", displayFont (34.0f), col::ink, tr, juce::Justification::topLeft);
    }

    auto section = [&] (juce::Rectangle<int> head, const juce::String& label) {
        text (label, uiFontTracked (11.0f, true), col::inkA (0.4f),
              shifted (head), juce::Justification::centredLeft);
    };
    auto row = [&] (juce::Rectangle<int> rr, const juce::String& name, bool selected) {
        auto sr = shifted (rr);
        drawPill (g, sr.toFloat(),
                  selected ? col::accentA (0.12f) : col::inkA (0.03f),
                  selected ? col::accent : col::inkA (0.12f));
        auto inner = sr.reduced (16, 0);
        if (selected)
            text (kDotGlyph, uiFont (14.0f, false), col::accent,
                  inner.removeFromLeft (22), juce::Justification::centredLeft);
        text (name, uiFont (14.0f, false), selected ? col::ink : col::inkA (0.7f),
              inner, juce::Justification::centredLeft);
    };

    section (inHeader_, "INPUT " + juce::String::fromUTF8 ("\xC2\xB7") + " GUITAR");
    for (int i = 0; i < inputs_.size() && i < (int) inRects_.size(); ++i)
        row (inRects_[(size_t) i], inputs_[i], inputs_[i] == currentIn_);
    if (inputs_.isEmpty())
        text ("No inputs found", uiFont (13.0f, false), col::inkA (0.4f),
              shifted (inHeader_.translated (0, 34)), juce::Justification::centredLeft);

    section (outHeader_, "OUTPUT " + juce::String::fromUTF8 ("\xC2\xB7") + " AMP / PHONES");
    for (int i = 0; i < outputs_.size() && i < (int) outRects_.size(); ++i)
        row (outRects_[(size_t) i], outputs_[i], outputs_[i] == currentOut_);
    if (outputs_.isEmpty())
        text ("No outputs found", uiFont (13.0f, false), col::inkA (0.4f),
              shifted (outHeader_.translated (0, 34)), juce::Justification::centredLeft);

    g.restoreState();

    // --- Fixed top bar (drawn over the content) ------------------------
    g.setColour (col::bg);
    g.fillRect (topBar_);
    drawPill (g, backRect_.toFloat(), juce::Colours::transparentBlack, col::inkA (0.22f));
    text (kBackGlyph + " BACK", uiFontTracked (12.0f, true), col::ink,
          backRect_, juce::Justification::centred);
    drawPill (g, rescanRect_.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text ("RESCAN", uiFontTracked (11.0f, true), col::accentAlt,
          rescanRect_, juce::Justification::centred);
}

void DevicesScreen::mouseDown (const juce::MouseEvent& e) {
    pressY_ = e.getPosition().y;
    pressScroll_ = scrollY_;
    dragged_ = false;
}

void DevicesScreen::mouseDrag (const juce::MouseEvent& e) {
    const int delta = e.getPosition().y - pressY_;
    if (std::abs (delta) > kDragThreshold) dragged_ = true;
    if (dragged_) {
        scrollY_ = pressScroll_ - (float) delta;
        clampScroll();
        repaint();
    }
}

void DevicesScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragged_) return;   // it was a scroll, not a tap
    const auto p = e.getPosition();

    if (backRect_.contains (p))   { if (onBack)   onBack();   return; }
    if (rescanRect_.contains (p)) { if (onRescan) onRescan(); return; }
    if (topBar_.contains (p)) return;

    const auto cp = toContent (p);
    for (int i = 0; i < (int) inRects_.size() && i < inputs_.size(); ++i)
        if (inRects_[(size_t) i].contains (cp)) {
            if (onSelectInput) onSelectInput (inputs_[i]);
            return;
        }
    for (int i = 0; i < (int) outRects_.size() && i < outputs_.size(); ++i)
        if (outRects_[(size_t) i].contains (cp)) {
            if (onSelectOutput) onSelectOutput (outputs_[i]);
            return;
        }
}

void DevicesScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    scrollY_ -= w.deltaY * 120.0f;
    clampScroll();
    repaint();
}
