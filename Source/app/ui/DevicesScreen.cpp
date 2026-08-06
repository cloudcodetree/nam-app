#include "app/ui/DevicesScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9"); // ‹
const juce::String kDotGlyph  = juce::String::fromUTF8 ("\xE2\x97\x89"); // ◉
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
    auto top = r.removeFromTop (juce::jmax (56, r.getHeight() / 14));
    backRect_   = { top.getX() + 20, top.getCentreY() - 16, 84, 32 };
    rescanRect_ = { top.getRight() - 20 - 96, top.getCentreY() - 16, 96, 32 };

    r.reduce (26, 0);
    titleRect_ = r.removeFromTop (64);

    const int rowH = 54, gap = 10, headH = 30;

    inHeader_ = r.removeFromTop (headH);
    inRects_.clear();
    for (int i = 0; i < inputs_.size(); ++i)
        inRects_.push_back (r.removeFromTop (rowH).withTrimmedBottom (gap));

    r.removeFromTop (14);
    outHeader_ = r.removeFromTop (headH);
    outRects_.clear();
    for (int i = 0; i < outputs_.size(); ++i)
        outRects_.push_back (r.removeFromTop (rowH).withTrimmedBottom (gap));
}

void DevicesScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    drawPill (g, backRect_.toFloat(), juce::Colours::transparentBlack, col::inkA (0.22f));
    text (kBackGlyph + " BACK", uiFontTracked (12.0f, true), col::ink,
          backRect_, juce::Justification::centred);

    drawPill (g, rescanRect_.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text ("RESCAN", uiFontTracked (11.0f, true), col::accentAlt,
          rescanRect_, juce::Justification::centred);

    {
        auto tr = titleRect_;
        text ("SIGNAL PATH", uiFontTracked (12.0f, true), col::inkA (0.45f),
              tr.removeFromTop (22), juce::Justification::topLeft);
        text ("Devices", displayFont (34.0f), col::ink, tr, juce::Justification::topLeft);
    }

    auto section = [&] (juce::Rectangle<int> head, const juce::String& label) {
        text (label, uiFontTracked (11.0f, true), col::inkA (0.4f),
              head, juce::Justification::centredLeft);
    };
    auto row = [&] (juce::Rectangle<int> rr, const juce::String& name, bool selected) {
        drawPill (g, rr.toFloat(),
                  selected ? col::accentA (0.12f) : col::inkA (0.03f),
                  selected ? col::accent : col::inkA (0.12f));
        auto inner = rr.reduced (16, 0);
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
              inHeader_.translated (0, 34), juce::Justification::centredLeft);

    section (outHeader_, "OUTPUT " + juce::String::fromUTF8 ("\xC2\xB7") + " AMP / PHONES");
    for (int i = 0; i < outputs_.size() && i < (int) outRects_.size(); ++i)
        row (outRects_[(size_t) i], outputs_[i], outputs_[i] == currentOut_);
    if (outputs_.isEmpty())
        text ("No outputs found", uiFont (13.0f, false), col::inkA (0.4f),
              outHeader_.translated (0, 34), juce::Justification::centredLeft);
}

void DevicesScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (backRect_.contains (p))   { if (onBack)   onBack();   return; }
    if (rescanRect_.contains (p)) { if (onRescan) onRescan(); return; }
    for (int i = 0; i < (int) inRects_.size() && i < inputs_.size(); ++i)
        if (inRects_[(size_t) i].contains (p)) {
            if (onSelectInput) onSelectInput (inputs_[i]);
            return;
        }
    for (int i = 0; i < (int) outRects_.size() && i < outputs_.size(); ++i)
        if (outRects_[(size_t) i].contains (p)) {
            if (onSelectOutput) onSelectOutput (outputs_[i]);
            return;
        }
}
