#include "app/ui/DiTrayPanel.h"

#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {

const juce::String kChevDown = juce::String::fromUTF8 ("\xE2\x8C\x84");            // ⌄
const juce::String kPlay = juce::String::fromUTF8 ("\xE2\x96\xB6");                // ▶
const juce::String kPause = juce::String::fromUTF8 ("\xE2\x9D\x9A\xE2\x9D\x9A");   // ❚❚
const juce::String kPrev = juce::String::fromUTF8 ("\xE2\x80\xB9");                // ‹
const juce::String kNext = juce::String::fromUTF8 ("\xE2\x80\xBA");                // ›

constexpr int kTabH = 18;     // idle grab tab
constexpr int kStripH = 46;   // collapsed status strip
constexpr int kRowH = 40;
constexpr int kPad = 16;

// The expanded panel never takes more than this share of the host, per the
// height-capped overlay rule.
constexpr float kMaxPanelFraction = 0.62f;

}   // namespace

DiTrayPanel::DiTrayPanel () { setInterceptsMouseClicks (true, true); }

void DiTrayPanel::setTracks (std::vector<Track> t) {
    tracks_ = std::move (t);
    layout ();
    repaint ();
}

void DiTrayPanel::setCurrent (int index) {
    current_ = index;
    repaint ();
}

void DiTrayPanel::setPlaying (bool p) {
    if (playing_ == p) return;
    const int before = contentInset ();
    playing_ = p;
    layout ();
    // Idle <-> collapsed changes how much room the host screen has.
    if (contentInset () != before && onLayoutChanged) onLayoutChanged ();
    repaint ();
}

void DiTrayPanel::setBusy (bool b) {
    busy_ = b;
    repaint ();
}

int DiTrayPanel::contentInset () const {
    if (expanded_) return 0;   // floats over the screen, does not push it
    return playing_ ? kStripH : kTabH;
}

void DiTrayPanel::collapse () {
    if (!expanded_) return;
    expanded_ = false;
    layout ();
    if (onLayoutChanged) onLayoutChanged ();
    repaint ();
}

bool DiTrayPanel::handleBack () {
    if (!expanded_) return false;
    collapse ();
    return true;
}

std::vector<int> DiTrayPanel::visibleIndices () const {
    std::vector<int> out;
    for (int i = 0; i < (int)tracks_.size (); ++i) {
        if (filter_ == 1 && tracks_[(size_t)i].isBass) continue;
        if (filter_ == 2 && !tracks_[(size_t)i].isBass) continue;
        out.push_back (i);
    }
    return out;
}

void DiTrayPanel::layout () {
    const int w = getWidth ();
    if (w <= 0) return;

    if (!expanded_) {
        panel_ = { 0, 0, w, playing_ ? kStripH : kTabH };
        tabRect_ = panel_;
        playRect_ = playing_ ? juce::Rectangle<int> (kPad, 8, 30, 30) : juce::Rectangle<int> ();
        listArea_ = {};
        filterRects_.clear ();
        return;
    }

    const int maxH = (int)((float)getHeight () * kMaxPanelFraction);
    panel_ = { 0, 0, w, maxH };

    int y = 14 + 16 + 8;   // header row
    prevRect_ = { kPad, y + 6, 26, 34 };
    playRect_ = { kPad + 30, y, 46, 46 };
    nextRect_ = { kPad + 80, y + 6, 26, 34 };
    loopRect_ = { w - kPad - 58, y + 12, 58, 26 };
    y += 46 + 10;

    filterRects_.clear ();
    int fx = kPad;
    for (int i = 0; i < 3; ++i) {
        const int fw = i == 0 ? 52 : (i == 1 ? 70 : 56);
        filterRects_.push_back ({ fx, y, fw, 26 });
        fx += fw + 7;
    }
    y += 26 + 10;

    rowsTop_ = y;
    handleRect_ = { w / 2 - 22, maxH - 16, 44, 4 };
    listArea_ = { 0, rowsTop_, w, maxH - rowsTop_ - 22 };

    contentH_ = (int)visibleIndices ().size () * kRowH;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listArea_.getHeight ()), scrollY_);
}

void DiTrayPanel::resized () { layout (); }

void DiTrayPanel::paintIdle (juce::Graphics& g) {
    g.setColour (col::inkA (0.22f));
    g.fillRoundedRectangle ((float)(getWidth () / 2 - 22), 7.0f, 44.0f, 4.0f, 2.0f);
    g.setFont (uiFontTracked (7.0f, true));
    g.setColour (col::inkA (0.3f));
    g.drawText ("DI", juce::Rectangle<int> (getWidth () / 2 - 22, 12, 44, 8),
                juce::Justification::centred, false);
}

void DiTrayPanel::paintCollapsed (juce::Graphics& g) {
    g.setColour (col::sheetBg);
    g.fillRect (panel_);
    g.setColour (col::accentA (0.28f));
    g.drawLine (0.0f, (float)panel_.getBottom (), (float)getWidth (), (float)panel_.getBottom (),
                1.0f);

    // Pause button (it is only ever shown while playing).
    g.setColour (col::accent);
    g.fillEllipse (playRect_.toFloat ());
    g.setFont (uiFont (10.0f, true));
    g.setColour (col::inkOnAccent);
    g.drawText (kPause, playRect_, juce::Justification::centred, false);

    // Work on a COPY: removeFromRight MUTATES, and panel_ is a member -- doing
    // it in place shrank the strip on every repaint until it disappeared.
    auto strip = panel_;
    const auto chev = strip.removeFromRight (30);
    const auto text = strip.withTrimmedLeft (kPad + 38);

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::ink);
    const auto name =
        current_ < (int)tracks_.size () ? tracks_[(size_t)current_].name : juce::String ();
    g.drawText (busy_ ? "Loading..." : name, text, juce::Justification::centredLeft, true);

    g.setFont (uiFont (12.0f, false));
    g.setColour (col::inkA (0.45f));
    g.drawText (kChevDown, chev, juce::Justification::centred, false);
}

void DiTrayPanel::paintRow (juce::Graphics& g, juce::Rectangle<int> r, const Track& t,
                            bool active) {
    if (active) {
        g.setColour (col::accentA (0.08f));
        g.fillRoundedRectangle (r.reduced (6, 1).toFloat (), 8.0f);
    }
    auto row = r.reduced (kPad, 0);

    g.setFont (uiFont (11.0f, false));
    g.setColour (active ? col::accent : col::inkA (0.3f));
    g.drawText (active && playing_ ? kPause : kPlay, row.removeFromLeft (18),
                juce::Justification::centredLeft, false);

    if (!t.bundled) {
        auto tag = row.removeFromRight (34);
        g.setFont (uiFontTracked (8.0f, true));
        g.setColour (col::inkA (0.32f));
        g.drawText ("GET", tag, juce::Justification::centredRight, false);
    }

    g.setFont (uiFont (13.0f, false));
    g.setColour (active ? col::ink : col::inkA (0.72f));
    g.drawText (t.name, row.withTrimmedLeft (6), juce::Justification::centredLeft, true);
}

void DiTrayPanel::paintExpanded (juce::Graphics& g) {
    juce::Path p;
    p.addRoundedRectangle ((float)panel_.getX (), (float)panel_.getY () - 18.0f,
                           (float)panel_.getWidth (), (float)panel_.getHeight () + 18.0f, 18.0f);
    g.setColour (col::sheetBg);
    g.fillPath (p);
    g.setColour (col::inkA (0.14f));
    g.strokePath (p, juce::PathStrokeType (1.0f));

    auto header = juce::Rectangle<int> (kPad, 14, getWidth () - kPad * 2, 16);
    g.setFont (uiFontTracked (10.0f, true));
    g.setColour (col::inkA (0.5f));
    g.drawText ("DI TEST TRACKS", header, juce::Justification::centredLeft, false);
    const auto vis = visibleIndices ();
    g.setColour (col::inkA (0.35f));
    g.setFont (uiFont (10.0f, false));
    g.drawText (juce::String ((int)vis.size ()) + " of " + juce::String ((int)tracks_.size ()),
                header, juce::Justification::centredRight, false);

    // Transport
    g.setFont (uiFont (17.0f, false));
    g.setColour (col::inkA (0.6f));
    g.drawText (kPrev, prevRect_, juce::Justification::centred, false);
    g.drawText (kNext, nextRect_, juce::Justification::centred, false);

    if (playing_) {
        g.setColour (col::accent);
        g.fillEllipse (playRect_.toFloat ());
        g.setColour (col::inkOnAccent);
    } else {
        g.setColour (col::inkA (0.28f));
        g.drawEllipse (playRect_.toFloat ().reduced (0.5f), 1.0f);
        g.setColour (col::ink);
    }
    g.setFont (uiFont (14.0f, true));
    g.drawText (busy_ ? "..." : (playing_ ? kPause : kPlay), playRect_,
                juce::Justification::centred, false);

    auto nameBox = juce::Rectangle<int> (playRect_.getRight () + 34, playRect_.getY () + 8,
                                         loopRect_.getX () - playRect_.getRight () - 44, 22);
    g.setFont (displayFont (14.0f));
    g.setColour (col::ink);
    g.drawText (current_ < (int)tracks_.size () ? tracks_[(size_t)current_].name : juce::String (),
                nameBox, juce::Justification::centredLeft, true);

    // Filters
    static const char* kLabels[] = { "ALL", "GUITAR", "BASS" };
    for (int i = 0; i < (int)filterRects_.size (); ++i) {
        const bool on = filter_ == i;
        drawPill (g, filterRects_[(size_t)i].toFloat (),
                  on ? col::accentA (0.09f) : juce::Colours::transparentBlack,
                  on ? col::accentA (0.5f) : col::inkA (0.16f), 1.0f);
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (on ? col::accent : col::inkA (0.5f));
        g.drawText (kLabels[i], filterRects_[(size_t)i], juce::Justification::centred, false);
    }

    // Track list, clipped and scrolled.
    g.saveState ();
    g.reduceClipRegion (listArea_);
    int y = listArea_.getY () - (int)scrollY_;
    for (int idx : vis) {
        juce::Rectangle<int> r (0, y, getWidth (), kRowH);
        if (r.getBottom () >= listArea_.getY () && r.getY () <= listArea_.getBottom ())
            paintRow (g, r, tracks_[(size_t)idx], idx == current_);
        y += kRowH;
    }
    g.restoreState ();

    g.setColour (col::inkA (0.3f));
    g.fillRoundedRectangle (handleRect_.toFloat (), 2.0f);
}

void DiTrayPanel::paint (juce::Graphics& g) {
    if (expanded_) paintExpanded (g);
    else if (playing_) paintCollapsed (g);
    else paintIdle (g);
}

void DiTrayPanel::mouseDown (const juce::MouseEvent& e) {
    dragging_ = false;
    pressScrollY_ = scrollY_;

    if (!expanded_) return;
    if (playRect_.expanded (6).contains (e.getPosition ())) return;
}

void DiTrayPanel::mouseDrag (const juce::MouseEvent& e) {
    if (!expanded_ || !listArea_.contains (e.getMouseDownPosition ())) return;
    if (e.getDistanceFromDragStartY () != 0) dragging_ = true;
    scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listArea_.getHeight ()),
                             pressScrollY_ - (float)e.getDistanceFromDragStartY ());
    repaint ();
}

void DiTrayPanel::mouseUp (const juce::MouseEvent& e) {
    if (dragging_) {
        dragging_ = false;
        return;
    }
    const auto p = e.getPosition ();

    if (!expanded_) {
        // Tapping the pause button on the strip must not also open the tray.
        if (playing_ && !playRect_.isEmpty () && playRect_.expanded (6).contains (p)) {
            if (onPlayPause) onPlayPause (false);
            return;
        }
        expanded_ = true;
        scrollY_ = 0.0f;
        layout ();
        if (onLayoutChanged) onLayoutChanged ();
        repaint ();
        return;
    }
    handleExpandedTap (p);
}

void DiTrayPanel::handleExpandedTap (juce::Point<int> p) {
    if (handleRect_.expanded (16).contains (p) || !panel_.contains (p)) {
        collapse ();
        return;
    }
    if (playRect_.expanded (6).contains (p)) {
        if (onPlayPause) onPlayPause (!playing_);
        return;
    }
    if (prevRect_.expanded (6).contains (p)) {
        if (onStep) onStep (-1);
        return;
    }
    if (nextRect_.expanded (6).contains (p)) {
        if (onStep) onStep (1);
        return;
    }
    for (int i = 0; i < (int)filterRects_.size (); ++i)
        if (filterRects_[(size_t)i].expanded (4).contains (p)) {
            filter_ = i;
            scrollY_ = 0.0f;
            layout ();
            repaint ();
            return;
        }

    if (listArea_.contains (p)) {
        const int rel = p.y - listArea_.getY () + (int)scrollY_;
        const int row = rel / kRowH;
        const auto vis = visibleIndices ();
        if (row >= 0 && row < (int)vis.size () && onSelect) onSelect (vis[(size_t)row]);
    }
}
