#include "app/ui/RadioScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
const juce::String kBack = juce::String::fromUTF8 ("\xE2\x80\xB9");     // ‹
const juce::String kLock = juce::String::fromUTF8 ("\xF0\x9F\x94\x92"); // 🔒
const juce::String kDot  = juce::String::fromUTF8 ("\xC2\xB7");         // ·
const juce::String kHeart = juce::String::fromUTF8 ("\xE2\x99\xA5");    // ♥
const juce::String kPlay = juce::String::fromUTF8 ("\xE2\x96\xB6");     // ▶
}

RadioScreen::RadioScreen() { setOpaque (true); }

void RadioScreen::setResults (std::vector<nam::ToneInfo> results) {
    results_ = std::move (results);
    relayout();
    repaint();
}

void RadioScreen::setStatus (juce::String s) { status_ = std::move (s); repaint (statusRect_); }

void RadioScreen::resized() { relayout(); }

void RadioScreen::relayout() {
    auto r = getLocalBounds();
    backRect_ = { 12, 22, 44, 40 };
    r.removeFromTop (72);
    statusRect_ = r.removeFromBottom (44);
    stationRow_ = r.removeFromTop (44).reduced (20, 6);
    list_ = r.reduced (20, 4);

    stationRects_.clear();
    int x = stationRow_.getX();
    for (auto& s : stations_) {
        const int w = juce::jmax (64, s.length() * 9 + 28);
        stationRects_.push_back ({ x, stationRow_.getY(), w, stationRow_.getHeight() });
        x += w + 8;
    }

    rowRects_.clear(); keepRects_.clear();
    int y = list_.getY();
    const int rowH = 58;
    for (size_t i = 0; i < results_.size() && y + rowH <= list_.getBottom(); ++i) {
        juce::Rectangle<int> row { list_.getX(), y, list_.getWidth(), rowH - 6 };
        rowRects_.push_back (row);
        keepRects_.push_back ({ row.getRight() - 44, row.getY(), 44, row.getHeight() });
        y += rowH;
    }
}

void RadioScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // Header
    text (kBack, uiFont (20.0f, false), col::inkA (0.6f), backRect_, juce::Justification::centred);
    text ("Radio", displayFont (30.0f), col::ink,
          { 44, 20, 160, 44 }, juce::Justification::centredLeft);
    juce::Rectangle<int> cab { getWidth() - 20 - 96, 28, 96, 32 };
    drawPill (g, cab.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text (kLock + " CAB", uiFontTracked (11.0f, true), col::accentAlt, cab, juce::Justification::centred);

    // Station pills
    for (int i = 0; i < (int) stationRects_.size(); ++i) {
        const bool on = (i == station_);
        drawPill (g, stationRects_[(size_t) i].toFloat(),
                  on ? col::accentA (0.12f) : col::accentA (0.0f),
                  on ? col::accentA (0.6f) : col::inkA (0.18f));
        text (stations_[i] + " st.", uiFont (12.0f, false),
              on ? col::accentAlt : col::inkA (0.7f),
              stationRects_[(size_t) i], juce::Justification::centred);
    }

    // Track list
    for (size_t i = 0; i < rowRects_.size(); ++i) {
        const auto& t = results_[i];
        auto row = rowRects_[i];
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (row.toFloat(), 12.0f);
        g.setColour (col::inkA (0.1f));
        g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 12.0f, 1.0f);

        auto inner = row.reduced (14, 8);
        text (kPlay, uiFont (13.0f, false), col::inkA (0.4f),
              inner.removeFromLeft (20), juce::Justification::centredLeft);
        auto keep = keepRects_[i];
        text (kHeart, uiFont (17.0f, false), col::inkA (0.3f), keep, juce::Justification::centred);
        inner.removeFromRight (48);
        text (juce::String (t.title), uiFont (14.0f, true), col::ink,
              inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft);
        juce::String meta = juce::String (t.gear.empty() ? juce::String ("TONE3000") : juce::String (t.gear));
        if (t.downloads > 0) meta += " " + kDot + " " + kHeart + " " + juce::String (t.downloads);
        text (meta, uiFont (11.0f, false), col::inkA (0.45f), inner, juce::Justification::centredLeft);
    }

    // Status
    g.setColour (col::inkA (0.06f));
    g.fillRect (statusRect_.getX(), statusRect_.getY(), statusRect_.getWidth(), 1);
    text (status_, uiFont (12.0f, false), col::inkA (0.5f),
          statusRect_.reduced (20, 0), juce::Justification::centred);
}

void RadioScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (backRect_.expanded (10).contains (p)) { if (onBack) onBack(); return; }
    for (int i = 0; i < (int) stationRects_.size(); ++i)
        if (stationRects_[(size_t) i].contains (p)) {
            station_ = i; repaint();
            if (onSearch) onSearch (stations_[i]);
            return;
        }
    for (int i = 0; i < (int) keepRects_.size(); ++i)
        if (keepRects_[(size_t) i].contains (p)) { if (onKeep) onKeep (i); return; }
}
