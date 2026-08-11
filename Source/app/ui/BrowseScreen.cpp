#include "app/ui/BrowseScreen.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <algorithm>
#include <cmath>

using namespace nam::ui;

namespace {
const juce::String kBack = juce::String::fromUTF8 ("\xE2\x80\xB9");     // ‹
const juce::String kDot = juce::String::fromUTF8 ("\xC2\xB7");          // ·
const juce::String kHeart = juce::String::fromUTF8 ("\xE2\x99\xA5");    // ♥
const juce::String kPlay = juce::String::fromUTF8 ("\xE2\x96\xB6");     // ▶
const juce::String kStop = juce::String::fromUTF8 ("\xE2\x97\xBC");     // ◼
const juce::String kSearch = juce::String::fromUTF8 ("\xE2\x8C\x95");   // ⌕
const juce::String kCheck = juce::String::fromUTF8 ("\xE2\x9C\x93");    // ✓
const juce::String kDown = juce::String::fromUTF8 ("\xE2\x86\x93");     // ↓
constexpr int kDragThreshold = 8;

const char* kTagChips[] = { "metal", "blues", "clean", "vintage", "high gain" };
const char* kMakeChips[] = { "Marshall", "Fender", "Vox", "Mesa", "Orange", "EVH" };

// Vector heart (Android's filled U+2665 glyph falls back to the huge,
// uncolourable colour-emoji font — text is out).
juce::Path heartPath (juce::Rectangle<float> b) {
    juce::Path p;
    p.startNewSubPath (0.50f, 0.32f);
    p.cubicTo (0.50f, 0.20f, 0.38f, 0.12f, 0.27f, 0.12f);
    p.cubicTo (0.11f, 0.12f, 0.04f, 0.26f, 0.04f, 0.38f);
    p.cubicTo (0.04f, 0.58f, 0.26f, 0.74f, 0.50f, 0.92f);
    p.cubicTo (0.74f, 0.74f, 0.96f, 0.58f, 0.96f, 0.38f);
    p.cubicTo (0.96f, 0.26f, 0.89f, 0.12f, 0.73f, 0.12f);
    p.cubicTo (0.62f, 0.12f, 0.50f, 0.20f, 0.50f, 0.32f);
    p.closeSubPath ();
    p.applyTransform (juce::AffineTransform::scale (b.getWidth (), b.getHeight ())
                          .translated (b.getX (), b.getY ()));
    return p;
}
}   // namespace

BrowseScreen::BrowseScreen () {
    setOpaque (true);

    search_.setMultiLine (false);
    search_.setReturnKeyStartsNewLine (false);
    search_.setTextToShowWhenEmpty (
        juce::String::fromUTF8 ("amps, makes, tags, creators\xE2\x80\xA6"), col::inkA (0.35f));
    search_.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_.setColour (juce::TextEditor::textColourId, col::ink);
    search_.setColour (juce::TextEditor::highlightColourId, col::accentA (0.35f));
    search_.setColour (juce::CaretComponent::caretColourId, col::accent);
    search_.setFont (uiFont (14.0f, false));
    search_.onReturnKey = [this] { runSearch (); };
    addAndMakeVisible (search_);

    startTimerHz (15);
}

BrowseScreen::~BrowseScreen () { stopTimer (); }

void BrowseScreen::timerCallback () {
    ++animTicks_;
    if (!isVisible ()) return;
    // Repaint only the animated button areas, not the whole list — full
    // software repaints at 15 Hz can eat an entire core on a big screen.
    const int dy = listArea_.getY () - (int)scrollY_;
    auto rp = [this, dy] (juce::Rectangle<int> r) {
        const auto vis = r.translated (0, dy).expanded (6).getIntersection (listArea_);
        if (!vis.isEmpty ()) repaint (vis);
    };
    if (loadingPack_ >= 0 && loadingPack_ < (int)rows_.size ())
        rp (rows_[(size_t)loadingPack_].playBtn);
}

juce::String BrowseScreen::composedQuery () const {
    juce::String q = search_.getText ().trim ();
    for (const auto& t : selectedTags_) q += (q.isEmpty () ? "" : " ") + t;
    return q;
}

void BrowseScreen::runSearch () {
    favoritesOnly_ = false;   // a live search leaves the deck view
    if (onQuery) onQuery (composedQuery ());
}

void BrowseScreen::setResults (std::vector<nam::ToneInfo> tones) {
    // Display order == the owner's order (sorting lives with the owner so
    // row indices always match its result list).
    tones_ = std::move (tones);
    models_.assign (tones_.size (), {});
    kept_.assign (tones_.size (), false);
    cached_.assign (tones_.size (), false);
    downloaded_.assign (tones_.size (), false);
    selVariant_.assign (tones_.size (), -1);
    defaultVariant_.assign (tones_.size (), -1);
    defaultName_.assign (tones_.size (), {});
    expanded_ = -1;
    playingPack_ = playingModel_ = -1;
    loadingPack_ = -1;
    menu_ = Menu::None;
    scrollY_ = 0.0f;
    relayout ();
    repaint ();
}

void BrowseScreen::setModels (int packIdx, juce::StringArray names) {
    if (packIdx < 0 || packIdx >= (int)models_.size ()) return;
    models_[(size_t)packIdx] = std::move (names);
    relayout ();
    repaint ();
}

void BrowseScreen::setDefaultModel (int packIdx, int modelIdx, juce::String name) {
    if (packIdx < 0 || packIdx >= (int)defaultName_.size ()) return;
    defaultVariant_[(size_t)packIdx] = modelIdx;
    defaultName_[(size_t)packIdx] = std::move (name);
    repaint (listArea_);
}

void BrowseScreen::setPlaying (int packIdx, int modelIdx) {
    playingPack_ = packIdx;
    playingModel_ = modelIdx;
    if (packIdx >= 0 && packIdx < (int)selVariant_.size () && modelIdx >= 0)
        selVariant_[(size_t)packIdx] = modelIdx;
    repaint ();
}

void BrowseScreen::setKeptFlags (std::vector<bool> kept) {
    kept_ = std::move (kept);
    repaint (listArea_);
}

void BrowseScreen::setStatus (juce::String s) {
    status_ = std::move (s);
    repaint (statusRect_);
}

void BrowseScreen::setLoading (int packIdx, float progress) {
    loadingPack_ = packIdx;
    loadProgress_ = juce::jlimit (0.0f, 1.0f, progress);
    repaint (listArea_);
}

void BrowseScreen::setLoadingProgress (float progress) {
    loadProgress_ = juce::jlimit (0.0f, 1.0f, progress);
    if (loadingPack_ >= 0) repaint (listArea_);
}

void BrowseScreen::setCachedFlags (std::vector<bool> cached) {
    cached_ = std::move (cached);
    repaint (listArea_);
}

void BrowseScreen::setDownloadedFlags (std::vector<bool> dl) {
    downloaded_ = std::move (dl);
    repaint (listArea_);
}

void BrowseScreen::resized () { relayout (); }

void BrowseScreen::relayout () {
    auto r = getLocalBounds ();

    auto head = r.removeFromTop (64);
    backRect_ = { head.getX () + 12, head.getCentreY () - 12, 32, 32 };
    badgeRect_ = { head.getRight () - 20 - 96, head.getCentreY () - 14, 96, 28 };

    searchBox_ = r.removeFromTop (48).reduced (20, 2);
    search_.setBounds (searchBox_.reduced (40, 6).withTrimmedRight (-30));

    auto frow = r.removeFromTop (44).reduced (20, 6);
    filtersBtn_ = frow.removeFromLeft (108);
    sortBtn_ = frow.removeFromLeft (110);
    favBtn_ = frow.removeFromLeft (52);
    countRect_ = frow;

    filterChips_.clear ();
    if (filtersOpen_) {
        const int chipH = 28, gap = 6;
        auto panel = r;
        int x = panel.getX () + 32, y = panel.getY () + 10;
        auto place = [&] (const char* label) {
            const int w = juce::jmax (54, (int)juce::String (label).length () * 8 + 22);
            if (x + w > getWidth () - 32) {
                x = 32;
                y += chipH + gap;
            }
            filterChips_.push_back ({ label, { x, y, w, chipH } });
            x += w + gap;
        };
        for (const char* t : kTagChips) place (t);
        x = 32;
        y += chipH + gap + 4;
        for (const char* m : kMakeChips) place (m);
        filterPanel_ = { panel.getX () + 20, panel.getY (), panel.getWidth () - 40,
                         (y + chipH + 12) - panel.getY () };
    } else {
        filterPanel_ = {};
    }

    statusRect_ = r.removeFromBottom (36);
    listArea_ = r.reduced (20, 2);

    rows_.assign (tones_.size (), {});
    const int rowH = 64, gap = 8;
    int y = 0;
    for (size_t i = 0; i < tones_.size (); ++i) {
        auto& row = rows_[i];
        const bool open = ((int)i == expanded_);
        int h = rowH;
        row.header = { listArea_.getX (), y, listArea_.getWidth (), rowH };
        row.playBtn = { row.header.getX () + 10, y + rowH / 2 - 18, 36, 36 };
        row.badge = { row.header.getRight () - 54, y + rowH / 2 - 11, 42, 22 };
        row.starBtn = { row.badge.getX () - 50, y + rowH / 2 - 22, 44, 44 };
        if (open) {
            int iy = y + rowH + 4;
            row.diBtn = { listArea_.getX () + 12, iy, listArea_.getWidth () - 24, 40 };
            iy += 48;
            row.varBtn = { listArea_.getX () + 12, iy, listArea_.getWidth () - 24, 40 };
            iy += 48;
            row.cabBtn = { listArea_.getX () + 12, iy, listArea_.getWidth () - 24, 40 };
            iy += 48;
            iy += 8;
            h = iy - y;
        }
        row.frame = { listArea_.getX (), y, listArea_.getWidth (), h };
        y += h + gap;
    }
    contentH_ = y + 8;
    clampScroll ();
}

void BrowseScreen::clampScroll () {
    const float maxScroll = (float)juce::jmax (0, contentH_ - listArea_.getHeight ());
    scrollY_ = juce::jlimit (0.0f, maxScroll, scrollY_);
}

int BrowseScreen::menuCount () const {
    if (menu_ == Menu::DemoTrack) return nam::demo::kNumTracks;
    if (menu_ == Menu::Cab) return nam::demo::kNumCabs;
    if (menu_ == Menu::Variant && expanded_ >= 0 && expanded_ < (int)models_.size ())
        return models_[(size_t)expanded_].size ();
    return 0;
}

int BrowseScreen::menuRowH () const { return 38; }

juce::Rectangle<int> BrowseScreen::menuPanelRect () const {
    if (menu_ == Menu::None || expanded_ < 0 || expanded_ >= (int)rows_.size ()) return {};
    const auto& row = rows_[(size_t)expanded_];
    const auto btn = (menu_ == Menu::DemoTrack ? row.diBtn
                      : menu_ == Menu::Cab     ? row.cabBtn
                                               : row.varBtn)
                         .translated (0, listArea_.getY () - (int)scrollY_);
    const int totalH = menuCount () * menuRowH () + 8;
    if (totalH <= 8) return {};
    const int belowTop = btn.getBottom () + 4;
    const int belowH = getHeight () - 24 - belowTop;
    if (belowH >= juce::jmin (totalH, menuRowH () * 2 + 8))
        return { btn.getX (), belowTop, btn.getWidth (), juce::jmin (totalH, belowH) };
    const int aboveH = juce::jmin (totalH, btn.getY () - 4 - 24);
    return { btn.getX (), btn.getY () - 4 - aboveH, btn.getWidth (),
             juce::jmax (menuRowH (), aboveH) };
}

std::vector<juce::Rectangle<int>> BrowseScreen::menuRowRects () const {
    std::vector<juce::Rectangle<int>> out;
    const auto panel = menuPanelRect ();
    if (panel.isEmpty ()) return out;
    int y = panel.getY () + 4 - (int)menuScroll_;
    for (int d = 0; d < menuCount (); ++d) {
        out.push_back ({ panel.getX (), y, panel.getWidth (), menuRowH () });
        y += menuRowH ();
    }
    return out;
}

void BrowseScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    // Header
    text (kBack, uiFont (20.0f, false), col::inkA (0.6f), backRect_, juce::Justification::centred);
    text ("Explore", displayFont (26.0f), col::ink,
          { backRect_.getRight () + 6, backRect_.getY () - 8, 200, 44 },
          juce::Justification::centredLeft);
    drawPill (g, badgeRect_.toFloat (), col::accentA (0.08f), col::accentA (0.5f));
    text ("TONE3000", uiFontTracked (10.0f, true), col::accentAlt, badgeRect_,
          juce::Justification::centred);

    // Search box
    g.setColour (col::inkA (0.04f));
    g.fillRoundedRectangle (searchBox_.toFloat (), 14.0f);
    g.setColour (col::inkA (0.14f));
    g.drawRoundedRectangle (searchBox_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
    text (kSearch, uiFont (15.0f, false), col::inkA (0.4f), searchBox_.withWidth (40),
          juce::Justification::centred);

    // Filters / sort / count
    const bool filterActive = filtersOpen_ || !selectedTags_.isEmpty ();
    drawPill (g, filtersBtn_.reduced (0, 4).toFloat (),
              filtersOpen_ ? col::accentA (0.12f) : juce::Colours::transparentBlack,
              filterActive ? col::accentA (0.6f) : col::inkA (0.2f));
    juce::String fLabel = "Filters";
    if (!selectedTags_.isEmpty ())
        fLabel += " " + kDot + " " + juce::String (selectedTags_.size ());
    text (fLabel, uiFont (12.0f, true), filterActive ? col::accentAlt : col::inkA (0.7f),
          filtersBtn_, juce::Justification::centred);
    text (juce::String (sort_ == 0 ? "Trending" : "Most kept") + " " +
              juce::String::fromUTF8 ("\xE2\x96\xBE"),
          uiFont (12.0f, true), col::inkA (0.6f), sortBtn_, juce::Justification::centred);
    // Favorites: heart pill toggling between the catalog and the deck.
    {
        drawPill (g, favBtn_.reduced (0, 4).toFloat (),
                  favoritesOnly_ ? col::accentA (0.12f) : juce::Colours::transparentBlack,
                  favoritesOnly_ ? col::accentA (0.6f) : col::inkA (0.2f));
        const auto hb = favBtn_.toFloat ().withSizeKeepingCentre (16.0f, 16.0f);
        if (favoritesOnly_) {
            g.setColour (col::accent);
            g.fillPath (heartPath (hb));
        } else {
            g.setColour (col::inkA (0.6f));
            g.strokePath (heartPath (hb), juce::PathStrokeType (1.4f));
        }
    }
    text (juce::String ((int)tones_.size ()) + " packs", uiFont (11.0f, false), col::inkA (0.4f),
          countRect_, juce::Justification::centredRight);

    // Pack list (scrolling)
    g.saveState ();
    g.reduceClipRegion (listArea_.getX () - 4, listArea_.getY (), listArea_.getWidth () + 8,
                        listArea_.getHeight ());
    const int dy = listArea_.getY () - (int)scrollY_;
    auto S = [dy] (juce::Rectangle<int> rr) { return rr.translated (0, dy); };

    for (size_t i = 0; i < rows_.size () && i < tones_.size (); ++i) {
        const auto& row = rows_[i];
        auto frame = S (row.frame);
        if (frame.getBottom () < listArea_.getY () || frame.getY () > listArea_.getBottom ())
            continue;

        const auto& t = tones_[i];
        const bool open = ((int)i == expanded_);
        const bool packPlaying = ((int)i == playingPack_);
        const bool packLoading = ((int)i == loadingPack_);
        const bool packCached = i < cached_.size () && cached_[i] && !packPlaying && !packLoading;
        const bool packDownloaded = i < downloaded_.size () && downloaded_[i];

        // On-device packs (downloaded or audition-cached) get a warm card tint.
        const bool onDevice = packDownloaded || (i < cached_.size () && cached_[i]);
        g.setColour (onDevice ? col::accentA (0.06f) : col::inkA (0.02f));
        g.fillRoundedRectangle (frame.toFloat (), 14.0f);
        g.setColour (packPlaying ? col::accentA (0.5f)
                     : open      ? col::accentA (0.35f)
                                 : col::inkA (0.1f));
        g.drawRoundedRectangle (frame.toFloat ().reduced (0.5f), 14.0f, 1.0f);

        // ▶ audition button
        auto play = S (row.playBtn);
        g.setColour (packPlaying  ? col::accent
                     : packCached ? col::ink
                                  : juce::Colours::transparentBlack);
        g.fillEllipse (play.toFloat ());
        g.setColour (packPlaying  ? col::accent
                     : packCached ? col::ink
                                  : col::inkA (packLoading ? 0.12f : 0.25f));
        g.drawEllipse (play.toFloat ().reduced (0.5f), 1.0f);
        text (packPlaying ? kStop : kPlay, uiFont (12.0f, false),
              packPlaying  ? col::inkOnAccent
              : packCached ? col::bg
                           : col::inkA (packLoading ? 0.45f : 1.0f),
              play, juce::Justification::centred);
        if (packLoading) {
            juce::Path arc;
            const auto b = play.toFloat ().expanded (3.0f);
            arc.addArc (b.getX (), b.getY (), b.getWidth (), b.getHeight (), 0.0f,
                        juce::MathConstants<float>::twoPi * loadProgress_, true);
            g.setColour (col::meterLime);
            g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Right trim clears the heart + badge column so long titles ellipsize
        // instead of running under the heart.
        auto head = S (row.header).withTrimmedLeft (64).withTrimmedRight (112);
        text (juce::String (t.title), uiFont (14.0f, true), col::ink,
              head.removeFromTop (row.header.getHeight () / 2 + 4),
              juce::Justification::bottomLeft);
        juce::String meta =
            juce::String (t.gear.empty () ? juce::String ("TONE3000") : juce::String (t.gear));
        if (t.downloads > 0) meta += " " + kDot + " " + kHeart + " " + juce::String (t.downloads);
        text (meta, uiFont (11.0f, false), col::inkA (0.45f), head, juce::Justification::topLeft);

        auto badge = S (row.badge);
        g.setColour (col::inkA (0.15f));
        g.drawRoundedRectangle (badge.toFloat (), 6.0f, 1.0f);
        const bool isIr = (t.format == "ir");
        text (isIr            ? "IR"
              : t.a2Count > 0 ? "A2"
                              : "A1",
              uiFontTracked (9.0f, true), isIr ? col::meterLime : col::accentAlt, badge,
              juce::Justification::centred);

        // Heart: quick add/remove from the tone deck (persisted library state).
        {
            const bool kept = i < kept_.size () && kept_[i];
            const auto hb = S (row.starBtn).toFloat ().withSizeKeepingCentre (24.0f, 24.0f);
            if (kept) {
                g.setColour (col::accent);
                g.fillPath (heartPath (hb));
            } else {
                g.setColour (col::inkA (0.35f));
                g.strokePath (heartPath (hb), juce::PathStrokeType (1.6f));
            }
        }

        if (!open) continue;

        auto dropdownBtn = [&] (juce::Rectangle<int> rr, const juce::String& label, bool menuOpen) {
            auto db = S (rr);
            g.setColour (col::inkA (0.04f));
            g.fillRoundedRectangle (db.toFloat (), 10.0f);
            g.setColour (menuOpen ? col::accentA (0.5f) : col::inkA (0.16f));
            g.drawRoundedRectangle (db.toFloat ().reduced (0.5f), 10.0f, 1.0f);
            auto inner = db.reduced (12, 0);
            text (juce::String::fromUTF8 (menuOpen ? "\xE2\x96\xB4" : "\xE2\x96\xBE"),
                  uiFont (10.0f, false), col::inkA (0.5f), inner.removeFromRight (16),
                  juce::Justification::centred);
            text (label, uiFont (12.0f, true), col::ink, inner, juce::Justification::centredLeft);
        };

        dropdownBtn (row.diBtn,
                     juce::String::fromUTF8 ("\xE2\x99\xAB") +
                         "  Demo: " + juce::String (nam::demo::kTracks[demoTrack_].display),
                     menu_ == Menu::DemoTrack);
        juce::String varLabel;
        if (models_[i].isEmpty ())
            varLabel = "Model: loading" + juce::String::fromUTF8 ("\xE2\x80\xA6");
        else if (selVariant_[i] >= 0 && selVariant_[i] < models_[i].size ())
            varLabel = "Model: " + models_[i][selVariant_[i]];
        else if (defaultName_[i].isNotEmpty ())
            varLabel = "Model: " + defaultName_[i] + " (default)";
        else varLabel = "Model: Auto (best)";
        dropdownBtn (row.varBtn, varLabel, menu_ == Menu::Variant);
        dropdownBtn (row.cabBtn, "Cab: " + juce::String (nam::demo::kCabs[cab_].display),
                     menu_ == Menu::Cab);
    }
    g.restoreState ();

    // Filters overlay (floats over the list)
    if (filtersOpen_) {
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 24, { 0, 10 })
            .drawForRectangle (g, filterPanel_);
        g.setColour (col::bgGradTop.brighter (0.03f));
        g.fillRoundedRectangle (filterPanel_.toFloat (), 14.0f);
        g.setColour (col::inkA (0.18f));
        g.drawRoundedRectangle (filterPanel_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
        for (const auto& c : filterChips_) {
            const bool on = selectedTags_.contains (c.label);
            drawPill (g, c.rect.toFloat (), on ? col::accent : juce::Colours::transparentBlack,
                      on ? col::accentA (0.7f) : col::inkA (0.2f));
            text (c.label, uiFont (11.0f, true), on ? col::inkOnAccent : col::inkA (0.7f), c.rect,
                  juce::Justification::centred);
        }
    }

    // Dropdown menu overlay (demo tracks or model variants)
    if (menu_ != Menu::None) {
        const auto panel = menuPanelRect ();
        const auto rowRects = menuRowRects ();
        if (!panel.isEmpty () && !rowRects.empty ()) {
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 24, { 0, 10 })
                .drawForRectangle (g, panel);
            g.setColour (col::bgGradTop.brighter (0.03f));
            g.fillRoundedRectangle (panel.toFloat (), 10.0f);
            g.setColour (col::inkA (0.18f));
            g.drawRoundedRectangle (panel.toFloat ().reduced (0.5f), 10.0f, 1.0f);
            g.saveState ();
            g.reduceClipRegion (panel.reduced (1));
            int selected = (menu_ == Menu::DemoTrack) ? demoTrack_
                           : (menu_ == Menu::Cab)
                               ? cab_
                               : (expanded_ >= 0 ? selVariant_[(size_t)expanded_] : -1);
            if (menu_ == Menu::Variant && selected < 0 && expanded_ >= 0)
                selected = defaultVariant_[(size_t)expanded_];   // show what Auto uses
            for (int d = 0; d < (int)rowRects.size (); ++d) {
                auto rr = rowRects[(size_t)d];
                if (rr.getBottom () < panel.getY () || rr.getY () > panel.getBottom ()) continue;
                const bool on = (d == selected);
                if (on) {
                    g.setColour (col::accentA (0.08f));
                    g.fillRect (rr);
                }
                auto inner = rr.reduced (14, 4);
                text (on ? kCheck : juce::String (), uiFont (11.0f, false), col::accentAlt,
                      inner.removeFromLeft (16), juce::Justification::centredLeft);
                const juce::String label =
                    (menu_ == Menu::DemoTrack) ? juce::String (nam::demo::kTracks[d].display)
                    : (menu_ == Menu::Cab)     ? juce::String (nam::demo::kCabs[d].display)
                                               : models_[(size_t)expanded_][d];
                text (label, uiFont (12.0f, menu_ != Menu::Variant), on ? col::accentAlt : col::ink,
                      inner, juce::Justification::centredLeft);
            }
            const int totalH = menuCount () * menuRowH () + 8;
            if (totalH > panel.getHeight ()) {
                g.setColour (col::inkA (0.25f));
                const float frac = (float)panel.getHeight () / (float)totalH;
                const float maxScroll = (float)(totalH - panel.getHeight ());
                const float pos = maxScroll > 0 ? menuScroll_ / maxScroll : 0.0f;
                const float barH = juce::jmax (18.0f, panel.getHeight () * frac);
                const float barY =
                    panel.getY () + 3 + pos * ((float)panel.getHeight () - 6.0f - barH);
                g.fillRoundedRectangle ((float)panel.getRight () - 5.0f, barY, 2.5f, barH, 1.2f);
            }
            g.restoreState ();
        }
    }

    // Status
    g.setColour (col::inkA (0.06f));
    g.fillRect (statusRect_.getX (), statusRect_.getY (), statusRect_.getWidth (), 1);
    text (status_, uiFont (11.0f, false), col::inkA (0.5f), statusRect_.reduced (20, 0),
          juce::Justification::centred);
}

void BrowseScreen::mouseDown (const juce::MouseEvent& e) {
    pressY_ = e.getPosition ().y;
    dragged_ = false;
    menuDragging_ = (menu_ != Menu::None) && menuPanelRect ().contains (e.getPosition ());
    pressScroll_ = menuDragging_ ? menuScroll_ : scrollY_;
    if (!searchBox_.contains (e.getPosition ())) unfocusAllComponents ();
}

void BrowseScreen::mouseDrag (const juce::MouseEvent& e) {
    if (!menuDragging_ && !listArea_.contains (e.getMouseDownPosition ())) return;
    const int delta = e.getPosition ().y - pressY_;
    if (std::abs (delta) > kDragThreshold) dragged_ = true;
    if (!dragged_) return;
    if (menuDragging_) {
        const auto panel = menuPanelRect ();
        const float maxScroll =
            (float)juce::jmax (0, menuCount () * menuRowH () + 8 - panel.getHeight ());
        menuScroll_ = juce::jlimit (0.0f, maxScroll, pressScroll_ - (float)delta);
    } else {
        scrollY_ = pressScroll_ - (float)delta;
        clampScroll ();
    }
    repaint ();
}

void BrowseScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragged_) {
        if (!menuDragging_) menu_ = Menu::None;   // menu drag = scroll it
        menuDragging_ = false;
        return;
    }
    menuDragging_ = false;
    const auto p = e.getPosition ();

    // Open menu consumes the tap: pick an item or dismiss.
    if (menu_ != Menu::None) {
        const auto panel = menuPanelRect ();
        if (panel.contains (p)) {
            const auto rowRects = menuRowRects ();
            for (int d = 0; d < (int)rowRects.size (); ++d)
                if (rowRects[(size_t)d].contains (p)) {
                    if (menu_ == Menu::DemoTrack) {
                        demoTrack_ = d;
                        if (onDemoTrack) onDemoTrack (d);
                    } else if (menu_ == Menu::Cab) {
                        cab_ = d;
                        if (onCab) onCab (d);
                    } else if (expanded_ >= 0) {
                        selVariant_[(size_t)expanded_] = d;
                        if (onPlayModel) onPlayModel (expanded_, d);
                    }
                    menu_ = Menu::None;
                    menuScroll_ = 0.0f;
                    repaint ();
                    return;
                }
        }
        menu_ = Menu::None;
        menuScroll_ = 0.0f;
        repaint ();
        return;
    }

    // Filters overlay consumes taps while open.
    if (filtersOpen_) {
        if (filterPanel_.contains (p)) {
            for (const auto& c : filterChips_)
                if (c.rect.contains (p)) {
                    if (selectedTags_.contains (c.label)) selectedTags_.removeString (c.label);
                    else selectedTags_.add (c.label);
                    repaint ();
                    runSearch ();
                    return;
                }
            return;
        }
        filtersOpen_ = false;
        repaint ();
        if (!filtersBtn_.contains (p)) return;   // outside tap just closes
        return;
    }

    if (backRect_.expanded (10).contains (p)) {
        if (onBack) onBack ();
        return;
    }
    if (filtersBtn_.contains (p)) {
        filtersOpen_ = true;
        relayout ();
        repaint ();
        return;
    }
    if (sortBtn_.contains (p)) {
        sort_ = (sort_ + 1) % 2;
        repaint ();
        if (onSort) onSort (sort_);   // owner re-pushes results in the new order
        return;
    }
    if (favBtn_.contains (p)) {
        favoritesOnly_ = !favoritesOnly_;
        repaint ();
        if (favoritesOnly_) {
            if (onFavorites) onFavorites ();
        } else runSearch ();
        return;
    }
    if (!listArea_.contains (p)) return;
    const juce::Point<int> cp{ p.x, p.y - listArea_.getY () + (int)scrollY_ };
    for (size_t i = 0; i < rows_.size (); ++i) {
        const auto& row = rows_[i];
        if (!row.frame.contains (cp)) continue;
        const bool open = ((int)i == expanded_);

        if (row.playBtn.contains (cp)) {
            const int sel = selVariant_[i];
            if (sel >= 0 && sel < models_[i].size ()) {
                if (onPlayModel) onPlayModel ((int)i, sel);
            } else if (onPlayPack) onPlayPack ((int)i);
            return;
        }
        if (open) {
            if (row.diBtn.contains (cp)) {
                menu_ = Menu::DemoTrack;
                menuScroll_ = 0;
                repaint ();
                return;
            }
            if (row.varBtn.contains (cp)) {
                if (!models_[i].isEmpty ()) {
                    menu_ = Menu::Variant;
                    menuScroll_ = 0;
                    repaint ();
                }
                return;
            }
            if (row.cabBtn.contains (cp)) {
                menu_ = Menu::Cab;
                menuScroll_ = 0;
                repaint ();
                return;
            }
        }
        if (row.starBtn.contains (cp)) {
            if (onKeep) onKeep ((int)i);
            return;
        }
        if (row.header.contains (cp)) {
            expanded_ = open ? -1 : (int)i;
            menu_ = Menu::None;
            relayout ();
            repaint ();
            if (!open && onExpand && models_[i].isEmpty ()) onExpand ((int)i);
            return;
        }
        return;
    }
}

void BrowseScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (menu_ != Menu::None && menuPanelRect ().contains (e.getPosition ())) {
        const auto panel = menuPanelRect ();
        const float maxScroll =
            (float)juce::jmax (0, menuCount () * menuRowH () + 8 - panel.getHeight ());
        menuScroll_ = juce::jlimit (0.0f, maxScroll, menuScroll_ - w.deltaY * 120.0f);
    } else {
        scrollY_ -= w.deltaY * 120.0f;
        clampScroll ();
    }
    repaint ();
}
