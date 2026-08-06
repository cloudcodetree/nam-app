#include "app/ui/BrowseScreen.h"
#include "app/ui/NamLookAndFeel.h"

#include <algorithm>

using namespace nam::ui;

namespace {
const juce::String kBack   = juce::String::fromUTF8 ("\xE2\x80\xB9");  // ‹
const juce::String kDot    = juce::String::fromUTF8 ("\xC2\xB7");      // ·
const juce::String kHeart  = juce::String::fromUTF8 ("\xE2\x99\xA5");  // ♥
const juce::String kPlay   = juce::String::fromUTF8 ("\xE2\x96\xB6");  // ▶
const juce::String kStop   = juce::String::fromUTF8 ("\xE2\x97\xBC");  // ◼
const juce::String kTri    = juce::String::fromUTF8 ("\xE2\x96\xB7");  // ▷
const juce::String kSearch = juce::String::fromUTF8 ("\xE2\x8C\x95");  // ⌕
const juce::String kCheck  = juce::String::fromUTF8 ("\xE2\x9C\x93");  // ✓
constexpr int kDragThreshold = 8;

const char* kTagChips[]  = { "metal", "blues", "clean", "vintage", "high gain" };
const char* kMakeChips[] = { "Marshall", "Fender", "Vox", "Mesa", "Orange", "EVH" };
const char* kTrackNames[] = { "CHORDS", "LEAD", "CHUGS" };
}

BrowseScreen::BrowseScreen() {
    setOpaque (true);

    search_.setMultiLine (false);
    search_.setReturnKeyStartsNewLine (false);
    search_.setTextToShowWhenEmpty (juce::String::fromUTF8 ("amps, makes, tags, creators\xE2\x80\xA6"),
                                    col::inkA (0.35f));
    search_.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::textColourId, col::ink);
    search_.setColour (juce::TextEditor::highlightColourId, col::accentA (0.35f));
    search_.setColour (juce::CaretComponent::caretColourId, col::accent);
    search_.setFont (uiFont (14.0f, false));
    search_.onReturnKey = [this] { runSearch(); };
    addAndMakeVisible (search_);

    startTimerHz (15);
}

BrowseScreen::~BrowseScreen() { stopTimer(); }

void BrowseScreen::timerCallback() {
    ++animTicks_;
    if (playingPack_ >= 0) repaint (listArea_);
}

juce::String BrowseScreen::composedQuery() const {
    juce::String q = search_.getText().trim();
    for (const auto& t : selectedTags_) q += (q.isEmpty() ? "" : " ") + t;
    return q;
}

void BrowseScreen::runSearch() {
    if (onQuery) onQuery (composedQuery());
}

void BrowseScreen::setResults (std::vector<nam::ToneInfo> tones) {
    tones_ = std::move (tones);
    if (sort_ == 1)
        std::stable_sort (tones_.begin(), tones_.end(),
                          [] (const auto& a, const auto& b) { return a.downloads > b.downloads; });
    models_.assign (tones_.size(), {});
    kept_.assign (tones_.size(), false);
    expanded_ = -1;
    playingPack_ = playingModel_ = -1;
    scrollY_ = 0.0f;
    relayout();
    repaint();
}

void BrowseScreen::setModels (int packIdx, juce::StringArray names) {
    if (packIdx < 0 || packIdx >= (int) models_.size()) return;
    models_[(size_t) packIdx] = std::move (names);
    relayout();
    repaint();
}

void BrowseScreen::setPlaying (int packIdx, int modelIdx) {
    playingPack_ = packIdx;
    playingModel_ = modelIdx;
    repaint();
}

void BrowseScreen::setKept (int packIdx) {
    if (packIdx >= 0 && packIdx < (int) kept_.size()) kept_[(size_t) packIdx] = true;
    repaint();
}

void BrowseScreen::setStatus (juce::String s) { status_ = std::move (s); repaint (statusRect_); }

void BrowseScreen::resized() { relayout(); }

void BrowseScreen::relayout() {
    auto r = getLocalBounds();

    auto head = r.removeFromTop (64);
    backRect_  = { head.getX() + 12, head.getCentreY() - 12, 32, 32 };
    badgeRect_ = { head.getRight() - 20 - 96, head.getCentreY() - 14, 96, 28 };

    searchBox_ = r.removeFromTop (48).reduced (20, 2);
    search_.setBounds (searchBox_.reduced (40, 6).withTrimmedRight (-30));

    auto frow = r.removeFromTop (44).reduced (20, 6);
    filtersBtn_ = frow.removeFromLeft (108);
    sortBtn_    = frow.removeFromLeft (110);
    countRect_  = frow;

    filterChips_.clear();
    if (filtersOpen_) {
        const int chipH = 28, gap = 6;
        auto panel = r;
        int x = panel.getX() + 32, y = panel.getY() + 10;
        auto place = [&] (const char* label) {
            const int w = juce::jmax (54, (int) juce::String (label).length() * 8 + 22);
            if (x + w > getWidth() - 32) { x = 32; y += chipH + gap; }
            filterChips_.push_back ({ label, { x, y, w, chipH } });
            x += w + gap;
        };
        for (const char* t : kTagChips)  place (t);
        x = 32; y += chipH + gap + 4;
        for (const char* m : kMakeChips) place (m);
        filterPanel_ = { panel.getX() + 20, panel.getY(), panel.getWidth() - 40,
                         (y + chipH + 12) - panel.getY() };
        r.removeFromTop (filterPanel_.getHeight() + 6);
    } else {
        filterPanel_ = {};
    }

    statusRect_ = r.removeFromBottom (36);
    listArea_ = r.reduced (20, 2);

    // Content-space rows.
    rows_.assign (tones_.size(), {});
    const int rowH = 64, gap = 8;
    int y = 0;
    for (size_t i = 0; i < tones_.size(); ++i) {
        auto& row = rows_[i];
        const bool open = ((int) i == expanded_);
        int h = rowH;
        row.header  = { listArea_.getX(), y, listArea_.getWidth(), rowH };
        row.playBtn = { row.header.getX() + 12, y + rowH / 2 - 20, 40, 40 };
        row.badge   = { row.header.getRight() - 58, y + rowH / 2 - 11, 46, 22 };
        if (open) {
            int iy = y + rowH + 4;
            row.trackChips.clear();
            const int tw = (listArea_.getWidth() - 24 - 12) / 3;
            for (int t = 0; t < 3; ++t)
                row.trackChips.push_back ({ listArea_.getX() + 12 + t * (tw + 6), iy, tw, 30 });
            iy += 38;
            row.modelRects.clear();
            const int nModels = models_[i].size();
            if (nModels == 0) iy += 26;    // "loading models…" line
            for (int mI = 0; mI < nModels; ++mI) {
                row.modelRects.push_back ({ listArea_.getX() + 12, iy, listArea_.getWidth() - 24, 34 });
                iy += 36;
            }
            row.keepBtn = { listArea_.getX() + 12, iy + 6, listArea_.getWidth() - 24, 42 };
            iy += 6 + 42 + 12;
            h = iy - y;
        }
        row.frame = { listArea_.getX(), y, listArea_.getWidth(), h };
        y += h + gap;
    }
    contentH_ = y + 8;
    clampScroll();
}

void BrowseScreen::clampScroll() {
    const float maxScroll = (float) juce::jmax (0, contentH_ - listArea_.getHeight());
    scrollY_ = juce::jlimit (0.0f, maxScroll, scrollY_);
}

void BrowseScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // Header
    text (kBack, uiFont (20.0f, false), col::inkA (0.6f), backRect_, juce::Justification::centred);
    text ("Explore", displayFont (26.0f), col::ink,
          { backRect_.getRight() + 6, backRect_.getY() - 8, 200, 44 }, juce::Justification::centredLeft);
    drawPill (g, badgeRect_.toFloat(), col::accentA (0.08f), col::accentA (0.5f));
    text ("TONE3000", uiFontTracked (10.0f, true), col::accentAlt, badgeRect_, juce::Justification::centred);

    // Search box
    g.setColour (col::inkA (0.04f));
    g.fillRoundedRectangle (searchBox_.toFloat(), 14.0f);
    g.setColour (col::inkA (0.14f));
    g.drawRoundedRectangle (searchBox_.toFloat().reduced (0.5f), 14.0f, 1.0f);
    text (kSearch, uiFont (15.0f, false), col::inkA (0.4f),
          searchBox_.withWidth (40), juce::Justification::centred);

    // Filters / sort / count
    const bool filterActive = filtersOpen_ || ! selectedTags_.isEmpty();
    drawPill (g, filtersBtn_.reduced (0, 4).toFloat(),
              filtersOpen_ ? col::accentA (0.12f) : juce::Colours::transparentBlack,
              filterActive ? col::accentA (0.6f) : col::inkA (0.2f));
    juce::String fLabel = "Filters";
    if (! selectedTags_.isEmpty()) fLabel += " " + kDot + " " + juce::String (selectedTags_.size());
    text (fLabel, uiFont (12.0f, true), filterActive ? col::accentAlt : col::inkA (0.7f),
          filtersBtn_, juce::Justification::centred);
    text (juce::String (sort_ == 0 ? "Trending" : "Most kept")
              + " " + juce::String::fromUTF8 ("\xE2\x96\xBE"),
          uiFont (12.0f, true), col::inkA (0.6f), sortBtn_, juce::Justification::centred);
    text (juce::String ((int) tones_.size()) + " packs", uiFont (11.0f, false), col::inkA (0.4f),
          countRect_, juce::Justification::centredRight);

    // Filter panel
    if (filtersOpen_) {
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (filterPanel_.toFloat(), 14.0f);
        g.setColour (col::inkA (0.14f));
        g.drawRoundedRectangle (filterPanel_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        for (const auto& c : filterChips_) {
            const bool on = selectedTags_.contains (c.label);
            drawPill (g, c.rect.toFloat(), on ? col::accent : juce::Colours::transparentBlack,
                      on ? col::accentA (0.7f) : col::inkA (0.2f));
            text (c.label, uiFont (11.0f, true), on ? col::inkOnAccent : col::inkA (0.7f),
                  c.rect, juce::Justification::centred);
        }
    }

    // Pack list (scrolling)
    g.saveState();
    g.reduceClipRegion (listArea_.getX() - 4, listArea_.getY(), listArea_.getWidth() + 8,
                        listArea_.getHeight());
    const int dy = listArea_.getY() - (int) scrollY_;
    auto S = [dy] (juce::Rectangle<int> rr) { return rr.translated (0, dy); };

    for (size_t i = 0; i < rows_.size() && i < tones_.size(); ++i) {
        const auto& row = rows_[i];
        auto frame = S (row.frame);
        if (frame.getBottom() < listArea_.getY() || frame.getY() > listArea_.getBottom()) continue;

        const auto& t = tones_[i];
        const bool open = ((int) i == expanded_);
        const bool packPlaying = ((int) i == playingPack_);

        g.setColour (col::inkA (0.02f));
        g.fillRoundedRectangle (frame.toFloat(), 14.0f);
        g.setColour (packPlaying ? col::accentA (0.5f) : open ? col::accentA (0.35f) : col::inkA (0.1f));
        g.drawRoundedRectangle (frame.toFloat().reduced (0.5f), 14.0f, 1.0f);

        // Header line: ▶ + title/meta + badge
        auto play = S (row.playBtn);
        g.setColour (packPlaying ? col::accent : juce::Colours::transparentBlack);
        g.fillEllipse (play.toFloat());
        g.setColour (packPlaying ? col::accent : col::inkA (0.25f));
        g.drawEllipse (play.toFloat().reduced (0.5f), 1.0f);
        text (packPlaying ? kStop : kPlay, uiFont (13.0f, false),
              packPlaying ? col::inkOnAccent : col::ink, play, juce::Justification::centred);

        auto head = S (row.header).withTrimmedLeft (64).withTrimmedRight (64);
        text (juce::String (t.title), uiFont (14.0f, true), col::ink,
              head.removeFromTop (row.header.getHeight() / 2 + 4), juce::Justification::bottomLeft);
        juce::String meta = juce::String (t.gear.empty() ? juce::String ("TONE3000") : juce::String (t.gear));
        if (t.downloads > 0) meta += " " + kDot + " " + kHeart + " " + juce::String (t.downloads);
        text (meta, uiFont (11.0f, false), col::inkA (0.45f), head, juce::Justification::topLeft);

        auto badge = S (row.badge);
        g.setColour (col::inkA (0.15f));
        g.drawRoundedRectangle (badge.toFloat(), 6.0f, 1.0f);
        text (t.a2Count > 0 ? "A2" : "A1", uiFontTracked (9.0f, true), col::accentAlt,
              badge, juce::Justification::centred);

        if (! open) continue;

        // Demo riff chips
        for (int c = 0; c < 3 && c < (int) row.trackChips.size(); ++c) {
            const bool on = (c == demoTrack_);
            auto cr = S (row.trackChips[(size_t) c]);
            drawPill (g, cr.toFloat(), on ? col::accentA (0.12f) : juce::Colours::transparentBlack,
                      on ? col::accentA (0.6f) : col::inkA (0.16f));
            text (kTrackNames[c], uiFontTracked (10.0f, true),
                  on ? col::accentAlt : col::inkA (0.6f), cr, juce::Justification::centred);
        }

        // Model variants
        if (models_[i].isEmpty()) {
            text ("loading models" + juce::String::fromUTF8 ("\xE2\x80\xA6"), uiFont (12.0f, false),
                  col::inkA (0.4f),
                  { frame.getX() + 24, S (row.trackChips[0]).getBottom() + 8, frame.getWidth() - 40, 22 },
                  juce::Justification::centredLeft);
        }
        for (int mI = 0; mI < models_[i].size() && mI < (int) row.modelRects.size(); ++mI) {
            const bool on = packPlaying && (mI == playingModel_);
            auto mr = S (row.modelRects[(size_t) mI]);
            if (on) {
                g.setColour (col::accentA (0.07f));
                g.fillRoundedRectangle (mr.toFloat(), 9.0f);
            }
            auto inner = mr.reduced (10, 0);
            text (on ? kPlay : kTri, uiFont (11.0f, false),
                  on ? col::accent : col::inkA (0.35f), inner.removeFromLeft (18),
                  juce::Justification::centredLeft);
            text (models_[i][mI], uiFont (12.0f, false), col::inkA (0.8f),
                  inner, juce::Justification::centredLeft);
            if (on) {
                // EQ bars animation
                auto bars = mr.reduced (10, 0).removeFromRight (22);
                for (int b = 0; b < 3; ++b) {
                    const float ph = (float) animTicks_ * (0.35f + 0.1f * (float) b) + (float) b;
                    const float hgt = 5.0f + (0.5f + 0.5f * std::sin (ph)) * 8.0f;
                    g.setColour (col::accent);
                    g.fillRect ((float) bars.getX() + b * 6.0f,
                                (float) mr.getCentreY() + 6.0f - hgt, 2.5f, hgt);
                }
            }
        }

        // Keep button
        auto keep = S (row.keepBtn);
        if (kept_[i]) {
            g.setColour (col::accentA (0.15f));
            g.fillRoundedRectangle (keep.toFloat(), 11.0f);
            text (kCheck + " IN LIBRARY", uiFontTracked (11.0f, true), col::accentAlt,
                  keep, juce::Justification::centred);
        } else {
            g.setColour (col::accent);
            g.fillRoundedRectangle (keep.toFloat(), 11.0f);
            text (kHeart + " KEEP", uiFontTracked (12.0f, true), col::inkOnAccent,
                  keep, juce::Justification::centred);
        }
    }
    g.restoreState();

    // Status
    g.setColour (col::inkA (0.06f));
    g.fillRect (statusRect_.getX(), statusRect_.getY(), statusRect_.getWidth(), 1);
    text (status_, uiFont (11.0f, false), col::inkA (0.5f),
          statusRect_.reduced (20, 0), juce::Justification::centred);
}

void BrowseScreen::mouseDown (const juce::MouseEvent& e) {
    pressY_ = e.getPosition().y;
    pressScroll_ = scrollY_;
    dragged_ = false;
}

void BrowseScreen::mouseDrag (const juce::MouseEvent& e) {
    if (! listArea_.contains (e.getMouseDownPosition())) return;
    const int delta = e.getPosition().y - pressY_;
    if (std::abs (delta) > kDragThreshold) dragged_ = true;
    if (dragged_) {
        scrollY_ = pressScroll_ - (float) delta;
        clampScroll();
        repaint();
    }
}

void BrowseScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragged_) return;
    const auto p = e.getPosition();

    // Tapping outside the search field dismisses the soft keyboard.
    if (! searchBox_.contains (p) && search_.hasKeyboardFocus (true))
        unfocusAllComponents();

    if (backRect_.expanded (10).contains (p)) { if (onBack) onBack(); return; }
    if (filtersBtn_.contains (p)) { filtersOpen_ = ! filtersOpen_; relayout(); repaint(); return; }
    if (sortBtn_.contains (p)) {
        sort_ = (sort_ + 1) % 2;
        setResults (std::move (tones_));   // re-sort in place
        return;
    }
    if (filtersOpen_)
        for (const auto& c : filterChips_)
            if (c.rect.contains (p)) {
                if (selectedTags_.contains (c.label)) selectedTags_.removeString (c.label);
                else selectedTags_.add (c.label);
                repaint();
                runSearch();
                return;
            }

    if (! listArea_.contains (p)) return;
    const juce::Point<int> cp { p.x, p.y - listArea_.getY() + (int) scrollY_ };
    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (! row.frame.contains (cp)) continue;
        const bool open = ((int) i == expanded_);

        if (row.playBtn.contains (cp)) { if (onPlayPack) onPlayPack ((int) i); return; }
        if (open) {
            for (int c = 0; c < (int) row.trackChips.size(); ++c)
                if (row.trackChips[(size_t) c].contains (cp)) {
                    demoTrack_ = c;
                    if (onDemoTrack) onDemoTrack (c);
                    repaint();
                    return;
                }
            for (int mI = 0; mI < (int) row.modelRects.size(); ++mI)
                if (row.modelRects[(size_t) mI].contains (cp)) {
                    if (onPlayModel) onPlayModel ((int) i, mI);
                    return;
                }
            if (row.keepBtn.contains (cp)) { if (onKeep) onKeep ((int) i); return; }
        }
        if (row.header.contains (cp)) {
            expanded_ = open ? -1 : (int) i;
            relayout();
            repaint();
            if (! open && onExpand && models_[i].isEmpty()) onExpand ((int) i);
            return;
        }
        return;
    }
}

void BrowseScreen::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    scrollY_ -= w.deltaY * 120.0f;
    clampScroll();
    repaint();
}
