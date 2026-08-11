#include "app/ui/PlayScreen.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
// Vector heart (Android's filled U+2665 falls back to the colour-emoji font).
juce::Path heartPath (juce::Rectangle<float> b) {
    juce::Path p;
    p.startNewSubPath (0.50f, 0.32f);
    p.cubicTo (0.50f, 0.20f, 0.38f, 0.12f, 0.27f, 0.12f);
    p.cubicTo (0.11f, 0.12f, 0.04f, 0.26f, 0.04f, 0.38f);
    p.cubicTo (0.04f, 0.58f, 0.26f, 0.74f, 0.50f, 0.92f);
    p.cubicTo (0.74f, 0.74f, 0.96f, 0.58f, 0.96f, 0.38f);
    p.cubicTo (0.96f, 0.26f, 0.89f, 0.12f, 0.73f, 0.12f);
    p.cubicTo (0.62f, 0.12f, 0.50f, 0.20f, 0.50f, 0.32f);
    p.closeSubPath();
    p.applyTransform (juce::AffineTransform::scale (b.getWidth(), b.getHeight())
                          .translated (b.getX(), b.getY()));
    return p;
}
}

PlayScreen::PlayScreen() { setOpaque (true); }

void PlayScreen::setNowPlaying (juce::String name, juce::String family, juce::String author) {
    name_ = std::move (name);
    family_ = std::move (family);
    author_ = std::move (author);
    repaint();
}

void PlayScreen::setPosition (int index, int count) {
    index_ = index;
    count_ = count;
    layout();   // dot rects depend on index/count
    repaint (dotsRect_.expanded (4));
}

void PlayScreen::setArtwork (juce::Image art) {
    art_ = std::move (art);
    // Cheap blur for the cover-fill behind the aspect-fit photo: crush the
    // image tiny, then let high-quality upsampling smear it at draw time.
    artBlur_ = {};
    if (art_.isValid())
        artBlur_ = art_.rescaled (juce::jmax (6, art_.getWidth() / 24),
                                  juce::jmax (6, art_.getHeight() / 24),
                                  juce::Graphics::mediumResamplingQuality);
    repaint (artRect_);
}

void PlayScreen::setKept (bool kept) {
    if (kept_ == kept) return;
    kept_ = kept;
    repaint (artRect_);
}

void PlayScreen::setSaved (bool saved) {
    if (saved_ == saved) return;
    saved_ = saved;
    repaint (artRect_);
}

void PlayScreen::setDemoPlaying (bool on) {
    if (demoPlaying_ == on) return;
    demoPlaying_ = on;
    repaint (artRect_);
}

void PlayScreen::setPairChoices (juce::String label, juce::StringArray names, int sel) {
    pairLabel_ = std::move (label);
    pairNames_ = std::move (names);
    pairSel_ = sel;
    repaint (artRect_);
}

void PlayScreen::setCabCard (bool isCab) {
    if (cabCard_ == isCab) return;
    cabCard_ = isCab;
    layout();
    repaint (artRect_);
}

void PlayScreen::setDeckView (int v) {
    if (view_ == v) return;
    view_ = v;
    flyOpen_ = false;
    gearSel_ = 0;        // fresh filter state per view (owner re-sends groups)
    layout();
    repaint();
}

void PlayScreen::setPageNav (bool canPrev, bool canNext) {
    if (pagePrev_ == canPrev && pageNext_ == canNext) return;
    pagePrev_ = canPrev;
    pageNext_ = canNext;
    repaint (dotsRect_.expanded (40, 4));
}

void PlayScreen::notifyFilters() {
    if (onFilterGroupsChanged) onFilterGroupsChanged (filterGroups_);
}

void PlayScreen::setFilterGroups (std::vector<FilterGroup> groups) {
    filterGroups_ = std::move (groups);
    layout();
    repaint();
}

void PlayScreen::openMenu (Menu which, juce::Rectangle<int> anchor,
                           juce::StringArray options, int selected) {
    menu_ = which;
    menuOptions_ = std::move (options);
    menuSelected_ = selected;
    menuScroll_ = 0.0f;
    constexpr int rowH = 38, pad = 8;
    menuContentH_ = rowH * menuOptions_.size() + pad * 2;
    const int w = juce::jmax (anchor.getWidth(), 230);
    const int x = juce::jlimit (12, juce::jmax (12, getWidth() - w - 12), anchor.getX());
    const int maxBottom = metersRow_.getY() - 10;
    const int below = maxBottom - (anchor.getBottom() + 6);
    const int above = anchor.getY() - 6 - (topBar_.getBottom() + 6);
    if (below >= juce::jmin (menuContentH_, 160) || below >= above) {
        menuRect_ = { x, anchor.getBottom() + 6, w, juce::jmin (menuContentH_, below) };
    } else {
        const int h = juce::jmin (menuContentH_, above);
        menuRect_ = { x, anchor.getY() - 6 - h, w, h };
    }
    // Start scrolled so the current selection is visible.
    const int selTop = 8 + menuSelected_ * rowH;
    if (selTop + rowH > menuRect_.getHeight())
        menuScroll_ = (float) juce::jmin (selTop - menuRect_.getHeight() / 2,
                                          menuContentH_ - menuRect_.getHeight());
    menuScroll_ = juce::jmax (0.0f, menuScroll_);
    repaint();
}

void PlayScreen::closeMenu() {
    if (menu_ == Menu::None) return;
    menu_ = Menu::None;
    menuOptions_.clear();
    repaint();
}

void PlayScreen::setGearChoices (juce::StringArray names) {
    gearNames_ = std::move (names);
    gearSel_ = 0;
    layout();
    repaint();
}

int PlayScreen::activeFilterCount() const {
    int n = 0;
    for (const auto& gp : filterGroups_) {
        if (gp.radio) {
            // Radio groups count only when off their default (first option).
            if (! gp.selected.isEmpty() && ! gp.options.isEmpty()
                && gp.selected[0] != gp.options[0])
                ++n;
        } else {
            n += gp.selected.size();
        }
    }
    return n;
}

void PlayScreen::setTuner (juce::String note, float cents, bool active) {
    if (note == tunerNote_ && active == tunerActive_
        && std::abs (cents - tunerCents_) < 1.0f)
        return;
    tunerNote_ = std::move (note);
    tunerCents_ = cents;
    tunerActive_ = active;
    repaint (metersRow_);
}

void PlayScreen::setLevels (float in, float out) {
    // Levels now live in the global bottom meter strip; nothing to paint here.
    inLevel_  = juce::jlimit (0.0f, 1.0f, in);
    outLevel_ = juce::jlimit (0.0f, 1.0f, out);
}

void PlayScreen::resized() {
    closeMenu();
    layout();
}

void PlayScreen::layout() {
    auto r = getLocalBounds();
    r.removeFromTop (10);   // breathing room under the status bar
    metersRow_ = r.removeFromBottom (juce::jmax (78, r.getHeight() / 9)).reduced (20, 6);
    hero_    = r;

    // No top bar (wordmark and settings gear retired — audio config lives
    // in the status-orb flyout now).
    topBar_ = libRect_ = gearRect_ = liveTopRect_ = editTopRect_ = {};

    // Strip above the card: the current view's title on the left; the
    // Filters pill (and, on browse, the gear-type dropdown) pushed right.
    // Pills hug their content.
    gearDdRect_ = {};
    bool anyFilterChips = false;
    for (const auto& gp : filterGroups_)
        if (! gp.options.isEmpty()) { anyFilterChips = true; break; }
    {
        auto fs = hero_.removeFromTop (46);
        viewTitleRect_ = fs.reduced (30, 3);
        if (view_ == 2 || anyFilterChips) {
            const int n = activeFilterCount();
            const juce::String label = n > 0
                ? "Filters " + juce::String::fromUTF8 ("\xC2\xB7") + " " + juce::String (n)
                : juce::String ("Filters");
            const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                               uiFont (12.0f, true), label));
            const int fw = 14 + 16 + 10 + tw + 16;   // pad · icon · gap · label · pad
            filterBtnRect_ = { fs.getRight() - 30 - fw, fs.getY() + 3, fw, 38 };
            if (view_ == 2 && ! gearNames_.isEmpty()) {
                const auto gearLabel = gearNames_[juce::jlimit (0, gearNames_.size() - 1, gearSel_)];
                const int gtw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                    uiFont (12.0f, true), gearLabel));
                const int gw = 16 + gtw + 10 + 12 + 12;   // pad · label · gap · chevron · pad
                gearDdRect_ = { filterBtnRect_.getX() - 8 - gw, fs.getY() + 3, gw, 38 };
            }
        } else {
            filterBtnRect_ = {};
        }
    }

    // Filters flyout: chips anchored under the filter button, grouped under
    // labelled sections with separators. Groups only appear when they have
    // something to offer (favorites view derives chips from the deck).
    flyChips_.clear();
    flyChipGroup_.clear();
    flyLabels_.clear();
    if (flyOpen_ && ! filterBtnRect_.isEmpty()) {
        const int anchorY = filterBtnRect_.getBottom();
        const int chipH = 30, gap = 6;
        const int x0 = hero_.getX() + 34;
        int x = x0, y = anchorY + 12;
        bool first = true;
        const auto chipFont = uiFont (11.0f, true);
        for (size_t gi = 0; gi < filterGroups_.size(); ++gi) {
            const auto& gp = filterGroups_[gi];
            if (gp.options.isEmpty()) continue;
            if (! first) y += chipH + 14;
            first = false;
            flyLabels_.push_back ({ { x0, y, hero_.getWidth() - 68, 14 }, gp.title });
            y += 20;
            x = x0;
            for (const auto& chip : gp.options) {
                const int tw = (int) std::ceil (
                    juce::GlyphArrangement::getStringWidth (chipFont, chip));
                const int w = juce::jmax (52, tw + 26);
                if (x + w > getWidth() - 34) { x = x0; y += chipH + gap; }
                flyChips_.push_back ({ { x, y, w, chipH }, chip });
                flyChipGroup_.push_back ((int) gi);
                x += w + gap;
            }
        }
        const int contentBottom = y + chipH + 14;
        flyContentH_ = contentBottom - (anchorY + 6);
        // Cap the panel above the tuner row; content scrolls inside.
        const int maxBottom = metersRow_.getY() - 10;
        flyRect_ = { hero_.getX() + 24, anchorY + 6, hero_.getWidth() - 48,
                     juce::jmin (flyContentH_, maxBottom - (anchorY + 6)) };
        flyScroll_ = juce::jlimit (0.0f, (float) juce::jmax (0, flyContentH_ - flyRect_.getHeight()),
                                   flyScroll_);
    } else {
        flyRect_ = {};
        flyScroll_ = 0.0f;
    }

    // Full-bleed tone card; below it: pagination dots. (Deck switching moved
    // to the shell's bottom nav — no in-screen view toggle.)
    auto inner = hero_.reduced (26, 8);
    dotsRect_ = inner.removeFromBottom (24);
    inner.removeFromBottom (4);
    artRect_ = inner;
    textRect_ = transportRect_ = {};
    viewRow_ = favViewRect_ = savedViewRect_ = browseViewRect_ = {};
    // Page arrows bracket the dots row (browse pages / big-deck windows).
    pagePrevRect_ = dotsRect_.removeFromLeft (34);
    pageNextRect_ = dotsRect_.removeFromRight (34);

    // Card corner buttons (front face): download + heart top-left, in the
    // same 34px circle style as the mixer button opposite them.
    saveRect_  = { artRect_.getX() + 14, artRect_.getY() + 14, 34, 34 };
    heartRect_ = { saveRect_.getRight() + 8, saveRect_.getY(), 34, 34 };

    // Circled chevrons straddle the card's side edges (half in, half out).
    prevRect_ = { artRect_.getX() - 20,     artRect_.getCentreY() - 20, 40, 40 };
    nextRect_ = { artRect_.getRight() - 20, artRect_.getCentreY() - 20, 40, 40 };

    // Pagination dot hit rects. Dots shrink as the deck grows so a full
    // results page (25) still fits; beyond that a bar is drawn instead.
    {
        const int n = juce::jlimit (0, (int) dotRects_.size(), count_);
        const int idle = n > 12 ? 5 : 8;
        const int gap  = n > 12 ? 4 : 8;
        const int active = n > 12 ? 16 : 22;
        int total = 0;
        for (int k = 0; k < n; ++k) total += (k == index_ ? active : idle) + (k ? gap : 0);
        int x = dotsRect_.getCentreX() - total / 2;
        for (int k = 0; k < n; ++k) {
            const int w = (k == index_ ? active : idle);
            dotRects_[(size_t) k] = { x, dotsRect_.getCentreY() - 6, w, 12 };
            x += w + gap;
        }
    }

    // The tuner panel (full meters row) opens the strobe tuner.
    tunerRect_ = metersRow_;

    backBtnRect_ = { artRect_.getRight() - 48, artRect_.getY() + 14, 34, 34 };
    cardEqRect_  = backBtnRect_;   // front face: mixer icon in the same corner

    // Card-back settings rows (drawn/hit only while flipped), then the
    // PAIR-IR and DEMO AUDIO rows. Cab/IR cards show only what applies:
    // no amp sliders, no PAIR row.
    {
        auto rows = artRect_.reduced (24, 16);
        rows.removeFromTop (40);   // name + return button header
        rows.removeFromTop (22);   // QUICK SETTINGS label
        auto bottom = rows.removeFromBottom (104);
        if (cabCard_) {
            for (auto& pr : paramRows_) pr = {};   // amp sliders don't apply
        } else {
            const int rowH = juce::jmin (48, rows.getHeight() / kNumToneParams);
            for (int i = 0; i < kNumToneParams; ++i)
                paramRows_[(size_t) i] = rows.removeFromTop (rowH).reduced (0, 6);
        }
        // Demo audio first, pairing beneath it.
        auto demo = bottom.removeFromTop (46).reduced (0, 4);
        demoPlayRect_ = demo.removeFromRight (44);   // hit target stays wide
        demoPlayRect_ = demoPlayRect_.withSizeKeepingCentre (   // drawn circle: square
            juce::jmin (demoPlayRect_.getWidth(), demoPlayRect_.getHeight()),
            juce::jmin (demoPlayRect_.getWidth(), demoPlayRect_.getHeight()));
        demoRowRect_ = demo.withTrimmedRight (8);
        pairRowRect_ = bottom.removeFromTop (46).reduced (0, 4);
    }
}

void PlayScreen::toggleFlip() {
    flipped_ = ! flipped_;
    startTimerHz (60);
}

void PlayScreen::timerCallback() {
    bool busy = false;
    const float target = flipped_ ? 1.0f : 0.0f;
    if (std::abs (flip_ - target) > 0.0005f) {
        const float step = 1.0f / 11.0f;
        flip_ = juce::jlimit (0.0f, 1.0f, flip_ + (target > flip_ ? step : -step));
        busy = busy || std::abs (flip_ - target) > 0.0005f;
    }
    if (burst_ > 0.0f) {
        burst_ = juce::jmax (0.0f, burst_ - 1.0f / 40.0f);
        busy = busy || burst_ > 0.0f;
    }
    if (! busy) stopTimer();
    repaint (artRect_.expanded (12));
}

void PlayScreen::applyParamFromX (int idx, int x) {
    auto track = paramRows_[(size_t) idx].withTrimmedLeft (86);
    const float v = juce::jlimit (0.0f, 1.0f,
        (float) (x - track.getX()) / (float) juce::jmax (1, track.getWidth()));
    params_[(size_t) idx].v = v;
    if (onToneParam) onToneParam (idx, v);
    repaint (paramRows_[(size_t) idx].expanded (8));
}

void PlayScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c,
                     juce::Rectangle<int> rr, juce::Justification j) {
        g.setFont (f); g.setColour (c); g.drawText (s, rr, j, false);
    };

    // --- Strip above the card: view title left, filter pills right --------
    {
        text (view_ == 2 ? "BROWSE" : view_ == 1 ? "DOWNLOADED" : "FAVORITES",
              uiFontTracked (11.0f, true), col::inkA (0.5f),
              viewTitleRect_, juce::Justification::centredLeft);
        // Gear-type dropdown (browse strip).
        if (! gearDdRect_.isEmpty()) {
            const bool hot = gearSel_ > 0;
            drawPill (g, gearDdRect_.toFloat(),
                      hot ? col::accentA (0.12f) : col::bg.withAlpha (0.4f),
                      hot ? col::accentA (0.6f) : col::inkA (0.18f));
            auto in = gearDdRect_.reduced (14, 0);
            text (juce::String::fromUTF8 ("\xE2\x96\xBE"), uiFont (10.0f, false),
                  col::inkA (0.5f), in.removeFromRight (14), juce::Justification::centred);
            text (gearNames_[juce::jlimit (0, gearNames_.size() - 1, gearSel_)],
                  uiFont (12.0f, true), hot ? col::accentAlt : col::inkA (0.7f),
                  in, juce::Justification::centredLeft);
        }
        if (! filterBtnRect_.isEmpty()) {
            const int n = activeFilterCount();
            const bool hot = flyOpen_ || n > 0;
            drawPill (g, filterBtnRect_.toFloat(),
                      flyOpen_ ? col::accentA (0.12f) : col::bg.withAlpha (0.4f),
                      hot ? col::accentA (0.6f) : col::inkA (0.18f));
            // Funnel icon (three narrowing bars) + label.
            g.setColour (hot ? col::accentAlt : col::inkA (0.6f));
            const float cx = (float) filterBtnRect_.getX() + 22.0f;
            const float cy = (float) filterBtnRect_.getCentreY();
            const float widths[] = { 16.0f, 11.0f, 6.0f };
            for (int i = 0; i < 3; ++i)
                g.fillRoundedRectangle (cx - widths[i] * 0.5f, cy - 5.5f + (float) i * 4.5f,
                                        widths[i], 2.5f, 1.25f);
            text (n > 0 ? "Filters " + juce::String::fromUTF8 ("\xC2\xB7") + " " + juce::String (n)
                        : "Filters",
                  uiFont (12.0f, true), hot ? col::accentAlt : col::inkA (0.7f),
                  filterBtnRect_.withTrimmedLeft (40), juce::Justification::centredLeft);
        }
    }

    // --- Hero card: artwork front / settings back (flip = squash-X) -----
    {
        const float sc = std::abs (std::cos (flip_ * juce::MathConstants<float>::pi));
        const auto face = artRect_.withSizeKeepingCentre (
            juce::jmax (2, (int) ((float) artRect_.getWidth() * sc)), artRect_.getHeight());
        const bool backFace = flip_ >= 0.5f;

        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 34,
                          { 0, 18 }).drawForRectangle (g, face);
        juce::Path clip; clip.addRoundedRectangle (face.toFloat(), 14.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        if (backFace) {
            // Settings face: panel wash + quick per-tone sliders.
            juce::ColourGradient bg (col::bgGradTop.brighter (0.08f), (float) face.getCentreX(),
                                     (float) face.getY(), col::bg, (float) face.getCentreX(),
                                     (float) face.getBottom(), false);
            g.setGradientFill (bg); g.fillRect (face);
            if (flip_ > 0.92f) {
                // Header: tone name + return button (Hi-Fi design).
                text (name_, displayFont (18.0f), col::ink,
                      { artRect_.getX() + 24, artRect_.getY() + 14,
                        backBtnRect_.getX() - artRect_.getX() - 34, 34 },
                      juce::Justification::centredLeft);
                g.setColour (col::inkA (0.3f));
                g.drawEllipse (backBtnRect_.toFloat().reduced (0.5f), 1.0f);
                text (juce::String::fromUTF8 ("\xE2\x86\xA9"), uiFont (15.0f, false),
                      col::inkA (0.8f), backBtnRect_, juce::Justification::centred);
                text ("QUICK SETTINGS", uiFontTracked (9.0f, true), col::inkA (0.4f),
                      { artRect_.getX() + 24, artRect_.getY() + 52, artRect_.getWidth() - 48, 18 },
                      juce::Justification::centredLeft);

                // PAIR + DEMO rows (Hi-Fi design).
                auto ddRow = [&] (juce::Rectangle<int> rr, const juce::String& label,
                                  const juce::String& value) {
                    g.setColour (col::inkA (0.04f));
                    g.fillRoundedRectangle (rr.toFloat(), 10.0f);
                    g.setColour (col::inkA (0.16f));
                    g.drawRoundedRectangle (rr.toFloat().reduced (0.5f), 10.0f, 1.0f);
                    auto in = rr.reduced (12, 4);
                    text (label, uiFontTracked (8.0f, true), col::inkA (0.4f),
                          in.removeFromTop (14), juce::Justification::centredLeft);
                    auto val = in;
                    text (juce::String::fromUTF8 ("\xE2\x96\xBE"), uiFont (10.0f, false),
                          col::inkA (0.5f), val.removeFromRight (16), juce::Justification::centred);
                    text (value, uiFont (12.0f, true), col::ink, val, juce::Justification::centredLeft);
                };
                if (! pairRowRect_.isEmpty() && ! pairNames_.isEmpty())
                    ddRow (pairRowRect_, pairLabel_,
                           pairNames_[juce::jlimit (0, pairNames_.size() - 1, pairSel_)]);
                ddRow (demoRowRect_, "DEMO AUDIO",
                       juce::String (nam::demo::kTracks[juce::jlimit (0, nam::demo::kNumTracks - 1,
                                                                      demoSel_)].display));
                g.setColour (demoPlaying_ ? col::accent : juce::Colours::transparentBlack);
                g.fillEllipse (demoPlayRect_.toFloat().reduced (2.0f));
                g.setColour (demoPlaying_ ? col::accent : col::inkA (0.3f));
                g.drawEllipse (demoPlayRect_.toFloat().reduced (2.5f), 1.0f);
                text (juce::String::fromUTF8 (demoPlaying_ ? "\xE2\x97\xBC" : "\xE2\x96\xB6"),
                      uiFont (12.0f, false), demoPlaying_ ? col::inkOnAccent : col::inkA (0.8f),
                      demoPlayRect_, juce::Justification::centred);
                for (int i = 0; ! cabCard_ && i < kNumToneParams; ++i) {
                    const auto row = paramRows_[(size_t) i];
                    const auto& pm = params_[(size_t) i];
                    text (pm.label, uiFontTracked (10.0f, true), col::inkA (0.6f),
                          row.withWidth (80), juce::Justification::centredLeft);
                    auto track = row.withTrimmedLeft (86);
                    const float cy = (float) track.getCentreY();
                    g.setColour (col::inkA (0.12f));
                    g.fillRoundedRectangle ((float) track.getX(), cy - 2.0f,
                                            (float) track.getWidth(), 4.0f, 2.0f);
                    const float fx = (float) track.getX() + (float) track.getWidth() * pm.v;
                    g.setColour (col::accentA (0.85f));
                    g.fillRoundedRectangle ((float) track.getX(), cy - 2.0f,
                                            juce::jmax (4.0f, fx - (float) track.getX()), 4.0f, 2.0f);
                    g.setColour (col::ink);
                    g.fillEllipse (fx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
                }
            }
        } else if (art_.isValid()) {
            // Whole photo aspect-FIT on black, floating over a half-opacity
            // blurred cover-fill of itself (tiny artBlur_ smeared upscaled).
            g.setColour (juce::Colours::black);
            g.fillRect (artRect_);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            if (artBlur_.isValid()) {
                const float bs = juce::jmax (
                    (float) artRect_.getWidth()  / (float) artBlur_.getWidth(),
                    (float) artRect_.getHeight() / (float) artBlur_.getHeight());
                const float bw = artBlur_.getWidth() * bs, bh = artBlur_.getHeight() * bs;
                g.setOpacity (0.5f);
                g.drawImageTransformed (artBlur_,
                    juce::AffineTransform::scale (bs)
                        .translated (artRect_.getCentreX() - bw * 0.5f,
                                     artRect_.getCentreY() - bh * 0.5f));
                g.setOpacity (1.0f);
            }
            const float scale = juce::jmin ((float) artRect_.getWidth()  / (float) art_.getWidth(),
                                            (float) artRect_.getHeight() / (float) art_.getHeight());
            const float w = art_.getWidth() * scale, h = art_.getHeight() * scale;
            const float fx = artRect_.getCentreX() - w * 0.5f;
            const float fy = artRect_.getCentreY() - h * 0.5f;
            // Opaque backing under the photo: the blurred fill must never
            // show through it (e.g. artwork with an alpha channel).
            g.setColour (juce::Colours::black);
            g.fillRect (juce::Rectangle<float> (fx, fy, w, h));
            g.drawImageTransformed (art_, juce::AffineTransform::scale (scale)
                                              .translated (fx, fy));
            // Footer scrim keeps the in-card text readable over bright photos.
            juce::ColourGradient scrim (col::bg.withAlpha (0.88f), (float) artRect_.getCentreX(),
                                        (float) artRect_.getBottom(), col::bg.withAlpha (0.0f),
                                        (float) artRect_.getCentreX(),
                                        (float) (artRect_.getBottom() - 170), false);
            g.setGradientFill (scrim); g.fillRect (artRect_);
        } else {
            juce::ColourGradient ag (col::bgGradTop.brighter (0.06f), (float) artRect_.getCentreX(),
                                     (float) artRect_.getY(), col::bg, (float) artRect_.getCentreX(),
                                     (float) artRect_.getBottom(), false);
            g.setGradientFill (ag); g.fillRect (artRect_);
            juce::ColourGradient glow (col::accentA (0.16f), (float) artRect_.getCentreX(),
                                       (float) artRect_.getBottom(), col::accent.withAlpha (0.0f),
                                       (float) artRect_.getCentreX(), (float) artRect_.getY(), false);
            g.setGradientFill (glow); g.fillRect (artRect_);
            // big faint tone initial as "album art"
            text (name_.substring (0, 1).toUpperCase(),
                  displayFont (artRect_.getHeight() * 0.5f), col::inkA (0.10f),
                  artRect_.withTrimmedBottom (110), juce::Justification::centred);
        }

        // Front footer: tone text + heart live INSIDE the card (Hi-Fi design).
        if (! backFace) {
            // Flip affordance: circled mixer (3 vertical sliders), top-right.
            g.setColour (col::bg.withAlpha (0.35f));
            g.fillEllipse (cardEqRect_.toFloat());
            g.setColour (col::inkA (0.3f));
            g.drawEllipse (cardEqRect_.toFloat().reduced (0.5f), 1.0f);
            {
                const auto eb = cardEqRect_.toFloat().withSizeKeepingCentre (15.0f, 15.0f);
                g.setColour (col::inkA (0.8f));
                const float ky[3] = { 0.30f, 0.65f, 0.45f };   // staggered knobs
                for (int i = 0; i < 3; ++i) {
                    const float x = eb.getCentreX() + (float) (i - 1) * 5.5f;
                    g.fillRoundedRectangle (x - 0.75f, eb.getY(), 1.5f, eb.getHeight(), 0.75f);
                    g.fillEllipse (x - 2.75f, eb.getY() + eb.getHeight() * ky[i] - 2.75f,
                                   5.5f, 5.5f);
                }
            }

            auto ft = face.reduced (18, 0).withTrimmedBottom (16);
            auto block = ft.removeFromBottom (author_.isNotEmpty() ? 84 : 62)
                           .withTrimmedRight (12);   // corner buttons live up top now
            text (family_.toUpperCase(), uiFontTracked (11.0f, false), col::inkA (0.45f),
                  block.removeFromTop (18), juce::Justification::centredLeft);
            text (name_, displayFont (26.0f), col::ink,
                  block.removeFromTop (36), juce::Justification::centredLeft);
            if (author_.isNotEmpty())
                text ("by " + author_, uiFont (12.0f, false), col::inkA (0.5f),
                      block.removeFromTop (22), juce::Justification::centredLeft);

            g.setColour (kept_ ? col::accentA (0.14f) : col::bg.withAlpha (0.35f));
            g.fillEllipse (heartRect_.toFloat());
            g.setColour (kept_ ? col::accentA (0.7f) : col::inkA (0.3f));
            g.drawEllipse (heartRect_.toFloat().reduced (0.5f), 1.0f);
            const auto hb = heartRect_.toFloat().withSizeKeepingCentre (16.0f, 16.0f);
            if (kept_) { g.setColour (col::accent); g.fillPath (heartPath (hb)); }
            else       { g.setColour (col::inkA (0.6f)); g.strokePath (heartPath (hb),
                                                                       juce::PathStrokeType (1.5f)); }

            // Download / saved toggle (left of the heart).
            g.setColour (saved_ ? col::accentA (0.14f) : col::bg.withAlpha (0.35f));
            g.fillEllipse (saveRect_.toFloat());
            g.setColour (saved_ ? col::accentA (0.7f) : col::inkA (0.3f));
            g.drawEllipse (saveRect_.toFloat().reduced (0.5f), 1.0f);
            text (juce::String::fromUTF8 (saved_ ? "\xE2\x9C\x93" : "\xE2\x86\x93"),
                  uiFont (15.0f, false), saved_ ? col::accent : col::inkA (0.6f),
                  saveRect_, juce::Justification::centred);

            // Heart-pop burst on keep.
            if (burst_ > 0.001f) {
                const float s = 60.0f + (1.0f - burst_) * 60.0f;
                const auto bb = face.toFloat().withSizeKeepingCentre (s, s);
                g.setColour (col::accent.withAlpha (burst_));
                g.fillPath (heartPath (bb));
            }
        }
        g.restoreState();
        g.setColour (col::inkA (backFace ? 0.14f : 0.10f));
        g.drawRoundedRectangle (face.toFloat(), 14.0f, 1.0f);
    }

    // --- Transport: circled side chevrons + dots pagination --------------
    // The buttons straddle the card edge, so they paint after it (on top).
    auto chevBtn = [&] (juce::Rectangle<int> rr, const char* glyph) {
        g.setColour (col::bg.withAlpha (0.72f));
        g.fillEllipse (rr.toFloat());
        g.setColour (col::inkA (0.28f));
        g.drawEllipse (rr.toFloat().reduced (0.5f), 1.0f);
        text (juce::String::fromUTF8 (glyph), uiFont (22.0f, false),
              col::inkA (0.75f), rr, juce::Justification::centred);
    };
    chevBtn (prevRect_, "\xE2\x80\xB9");
    chevBtn (nextRect_, "\xE2\x80\xBA");
    // Page arrows bracketing the dots (dimmed when that direction is empty).
    if (pagePrev_ || pageNext_) {
        text (juce::String::fromUTF8 ("\xE2\x80\xB9"), uiFont (17.0f, true),
              col::inkA (pagePrev_ ? 0.6f : 0.15f), pagePrevRect_,
              juce::Justification::centred);
        text (juce::String::fromUTF8 ("\xE2\x80\xBA"), uiFont (17.0f, true),
              col::inkA (pageNext_ ? 0.6f : 0.15f), pageNextRect_,
              juce::Justification::centred);
    }
    if (count_ > 0 && count_ <= (int) dotRects_.size()) {
        for (int k = 0; k < count_; ++k) {
            g.setColour (k == index_ ? col::accent : col::inkA (0.2f));
            g.fillRoundedRectangle (dotRects_[(size_t) k].toFloat()
                                        .withSizeKeepingCentre ((float) dotRects_[(size_t) k].getWidth(), 8.0f),
                                    4.0f);
        }
    } else if (count_ > 0 && index_ >= 0) {
        // Big decks: thin progress bar instead of a dot per tone.
        auto bar = dotsRect_.reduced (60, 11);
        g.setColour (col::inkA (0.14f));
        g.fillRoundedRectangle (bar.toFloat(), 1.5f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (bar.toFloat().withWidth (
            bar.getWidth() * (float) (index_ + 1) / (float) count_), 1.5f);
    }

    // --- Tuner row (levels live in the global bottom meter) -------------
    {
        juce::Rectangle<int> tuner = metersRow_;

        auto panel = [&] (juce::Rectangle<int> rr) {
            g.setColour (col::inkA (0.03f));
            g.fillRoundedRectangle (rr.toFloat(), 12.0f);
            g.setColour (col::inkA (0.12f));
            g.drawRoundedRectangle (rr.toFloat().reduced (0.5f), 12.0f, 1.0f);
        };

        // Tuner panel: detected note + cents-deviation bars. The centre bar
        // lights lime when in tune (within ±7 cents); off-pitch lights the
        // bar nearest the deviation in accent orange.
        panel (tuner);
        auto ti = tuner.reduced (18, 0);
        text (tunerActive_ ? tunerNote_ : juce::String::fromUTF8 ("\xE2\x80\x93"),
              displayFont (tunerActive_ && tunerNote_.length() > 2 ? 20.0f : 26.0f),
              tunerActive_ ? col::ink : col::inkA (0.35f),
              ti.removeFromLeft (40), juce::Justification::centred);
        text ("TUNER", uiFontTracked (10.0f, true), col::inkA (0.4f),
              ti.removeFromRight (48), juce::Justification::centred);
        {
            // Pedal-style LED segments (same look as the expanded BARS mode):
            // 11 segments over -50..+50 cents, centre = in tune.
            auto bars = ti.reduced (6, 0);
            const int n = 11, gap = 5;
            const int segW = juce::jmax (3, (bars.getWidth() - (n - 1) * gap) / n);
            const int mid = n / 2;
            int lit = -1;
            bool inTune = false;
            if (tunerActive_) {
                const float c = juce::jlimit (-49.0f, 49.0f, tunerCents_);
                inTune = std::abs (c) <= 7.0f;
                lit = inTune ? mid
                             : juce::jlimit (0, n - 1,
                                   (int) std::floor ((c + 50.0f) / (100.0f / (float) n)));
            }
            const int baseH = 14;
            for (int i = 0; i < n; ++i) {
                const int grow = (i == mid) ? baseH / 2 : baseH / 5 * std::abs (i - mid) / mid;
                const int h = baseH + (i == mid ? baseH / 2 : grow);
                juce::Rectangle<int> seg (bars.getX() + i * (segW + gap),
                                          bars.getCentreY() - h / 2, segW, h);
                const bool on = (i == lit);
                const juce::Colour c = ! on ? col::inkA (0.10f)
                                     : i == mid ? col::meterLime
                                                : col::accent.withAlpha (0.9f);
                if (on) {   // glow
                    g.setColour (c.withAlpha (0.25f));
                    g.fillRoundedRectangle (seg.toFloat().expanded (4.0f), 7.0f);
                }
                g.setColour (c);
                g.fillRoundedRectangle (seg.toFloat(), 3.0f);
            }
        }

    }

    // Filters flyout floats over the card; content scrolls when it exceeds
    // the panel height.
    if (! flyRect_.isEmpty()) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (flyRect_.toFloat(), 14.0f);
        g.setColour (col::inkA (0.18f));
        g.drawRoundedRectangle (flyRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (flyRect_.toFloat().reduced (1.0f), 13.0f);
        g.reduceClipRegion (clip);
        g.addTransform (juce::AffineTransform::translation (0.0f, -flyScroll_));
        for (size_t li = 0; li < flyLabels_.size(); ++li) {
            const auto& [rr, name] = flyLabels_[li];
            if (li > 0) {   // separator hairline above each later group
                g.setColour (col::inkA (0.10f));
                g.fillRect (rr.getX() - 6, rr.getY() - 9, rr.getWidth() + 12, 1);
            }
            text (name, uiFontTracked (9.0f, true), col::inkA (0.4f),
                  rr, juce::Justification::centredLeft);
        }
        for (size_t ci = 0; ci < flyChips_.size(); ++ci) {
            const auto& [rr, label] = flyChips_[ci];
            const auto& gp = filterGroups_[(size_t) flyChipGroup_[ci]];
            const bool on = gp.selected.contains (label);
            drawPill (g, rr.toFloat(), on ? col::accent : juce::Colours::transparentBlack,
                      on ? col::accent : col::inkA (0.2f));
            text (label, uiFont (11.0f, true), on ? col::inkOnAccent : col::inkA (0.7f),
                  rr, juce::Justification::centred);
        }
        g.restoreState();
        // Scroll affordance: thin track on the right when content overflows.
        if (flyContentH_ > flyRect_.getHeight()) {
            const float frac = (float) flyRect_.getHeight() / (float) flyContentH_;
            const float thumbH = juce::jmax (24.0f, flyRect_.getHeight() * frac);
            const float travel = (float) flyRect_.getHeight() - thumbH - 8.0f;
            const float pos = flyScroll_ / (float) (flyContentH_ - flyRect_.getHeight());
            g.setColour (col::inkA (0.25f));
            g.fillRoundedRectangle ((float) flyRect_.getRight() - 7.0f,
                                    (float) flyRect_.getY() + 4.0f + travel * pos,
                                    3.0f, thumbH, 1.5f);
        }
    }

    // Dropdown overlay (gear / pair / demo pickers) — same panel language
    // as the filters flyout, anchored to its field.
    if (menu_ != Menu::None) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (menuRect_.toFloat(), 14.0f);
        g.setColour (col::inkA (0.18f));
        g.drawRoundedRectangle (menuRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (menuRect_.toFloat().reduced (1.0f), 13.0f);
        g.reduceClipRegion (clip);
        constexpr int rowH = 38;
        for (int i = 0; i < menuOptions_.size(); ++i) {
            const juce::Rectangle<int> row (menuRect_.getX() + 6,
                                            menuRect_.getY() + 8 + i * rowH - (int) menuScroll_,
                                            menuRect_.getWidth() - 12, rowH);
            if (row.getBottom() < menuRect_.getY() || row.getY() > menuRect_.getBottom())
                continue;
            const bool sel = (i == menuSelected_);
            if (sel) {
                g.setColour (col::accentA (0.10f));
                g.fillRoundedRectangle (row.toFloat(), 9.0f);
            }
            auto in = row.reduced (12, 0);
            text (sel ? juce::String::fromUTF8 ("\xE2\x9C\x93") : juce::String(),
                  uiFont (12.0f, false), col::accentAlt,
                  in.removeFromLeft (18), juce::Justification::centredLeft);
            text (menuOptions_[i], uiFont (13.0f, sel), sel ? col::accentAlt : col::inkA (0.85f),
                  in, juce::Justification::centredLeft);
        }
        g.restoreState();
        if (menuContentH_ > menuRect_.getHeight()) {
            const float frac = (float) menuRect_.getHeight() / (float) menuContentH_;
            const float thumbH = juce::jmax (24.0f, menuRect_.getHeight() * frac);
            const float travel = (float) menuRect_.getHeight() - thumbH - 8.0f;
            const float pos = menuScroll_ / (float) (menuContentH_ - menuRect_.getHeight());
            g.setColour (col::inkA (0.25f));
            g.fillRoundedRectangle ((float) menuRect_.getRight() - 7.0f,
                                    (float) menuRect_.getY() + 4.0f + travel * pos,
                                    3.0f, thumbH, 1.5f);
        }
    }

}

void PlayScreen::mouseUp (const juce::MouseEvent& e) {
    if (menuPressed_) {
        if (! menuMoved_) {
            const int i = (e.getPosition().y - menuRect_.getY() - 8 + (int) menuScroll_) / 38;
            if (i >= 0 && i < menuOptions_.size()) {
                switch (menu_) {
                    case Menu::Gear:
                        gearSel_ = i;
                        layout();   // pill width tracks the label
                        if (onGearSelect) onGearSelect (i);
                        break;
                    case Menu::Pair:
                        pairSel_ = i;
                        if (onSelectPair) onSelectPair (i);
                        break;
                    case Menu::Demo:
                        demoSel_ = i;
                        if (onSelectDemoTrack) onSelectDemoTrack (i);
                        break;
                    default: break;
                }
            }
            closeMenu();
        }
        menuPressed_ = menuMoved_ = false;
        return;
    }
    if (flyPressed_) {
        // Tap (no scroll drag) selects the chip under the finger.
        if (! flyMoved_) {
            const juce::Point<int> cp { e.getPosition().x,
                                        e.getPosition().y + (int) flyScroll_ };
            for (size_t ci = 0; ci < flyChips_.size(); ++ci) {
                const auto& [rr, label] = flyChips_[ci];
                if (! rr.contains (cp)) continue;
                auto& gp = filterGroups_[(size_t) flyChipGroup_[ci]];
                if (gp.radio) {
                    gp.selected.clearQuick();
                    gp.selected.add (label);
                } else if (gp.selected.contains (label)) {
                    gp.selected.removeString (label);
                } else {
                    gp.selected.add (label);
                }
                layout();   // pill width tracks the active-filter count
                repaint();
                notifyFilters();
                break;
            }
        }
        flyPressed_ = flyMoved_ = false;
        return;
    }
    if (dragParam_ >= 0) { dragParam_ = -1; return; }
    const int dx = e.getPosition().x - pressPos_.x;
    const int dy = e.getPosition().y - pressPos_.y;
    const bool tap = std::abs (dx) < 12 && std::abs (dy) < 12;

    // Settings face: any tap that isn't an interactive element flips back
    // (sliders end up in dragParam_; buttons acted in mouseDown). Swipes
    // that START outside the card still step tones — the new tone keeps
    // showing whichever face is up (flip state is untouched).
    if (flipped_) {
        if (tap && pressPos_.y >= artRect_.getY()   // filter strip taps never flip
                && ! prevRect_.contains (pressPos_) && ! nextRect_.contains (pressPos_)
                && ! gearRect_.contains (pressPos_) && ! tunerRect_.contains (pressPos_)
                && ! dotsRect_.contains (pressPos_) && ! viewRow_.contains (pressPos_)
                && ! pairRowRect_.contains (pressPos_) && ! demoRowRect_.contains (pressPos_)
                && ! demoPlayRect_.contains (pressPos_)) {
            toggleFlip();
            return;
        }
        if (! artRect_.contains (pressPos_) && hero_.contains (pressPos_)
            && std::abs (dx) > 60 && std::abs (dx) > std::abs (dy) * 2) {
            if (dx < 0) { if (onNext) onNext(); }
            else        { if (onPrev) onPrev(); }
        }
        return;
    }

    // Swipe DOWN on the card keeps/unkeeps it (Hi-Fi design).
    if (artRect_.contains (pressPos_) && dy > 50 && std::abs (dy) > std::abs (dx) * 3 / 2) {
        if (onKeepToggle) onKeepToggle();
        if (! kept_) { burst_ = 1.0f; startTimerHz (60); }
        return;
    }

    // A tap on the card flips it around to the settings face. The circled
    // chevrons overlap the card edge, so they must not flip it.
    if (tap && artRect_.contains (pressPos_) && ! heartRect_.expanded (4).contains (pressPos_)
        && ! saveRect_.expanded (4).contains (pressPos_)
        && ! prevRect_.expanded (4).contains (pressPos_)
        && ! nextRect_.expanded (4).contains (pressPos_)) {
        toggleFlip();
        return;
    }

    // Swipe across the hero area steps through the collection (front only).
    if (! hero_.contains (pressPos_)) return;
    if (std::abs (dx) > 60 && std::abs (dx) > std::abs (dy) * 2) {
        if (dx < 0) { if (onNext) onNext(); }
        else        { if (onPrev) onPrev(); }
    }
}

void PlayScreen::mouseDrag (const juce::MouseEvent& e) {
    if (menuPressed_) {
        const int dy = e.getPosition().y - menuPressPos_.y;
        if (std::abs (dy) > 8) menuMoved_ = true;
        if (menuMoved_) {
            menuScroll_ = juce::jlimit (0.0f,
                (float) juce::jmax (0, menuContentH_ - menuRect_.getHeight()),
                menuPressScroll_ - (float) dy);
            repaint (menuRect_.expanded (2));
        }
        return;
    }
    if (flyPressed_) {
        const int dy = e.getPosition().y - flyPressPos_.y;
        if (std::abs (dy) > 8) flyMoved_ = true;
        if (flyMoved_) {
            flyScroll_ = juce::jlimit (0.0f,
                (float) juce::jmax (0, flyContentH_ - flyRect_.getHeight()),
                flyPressScroll_ - (float) dy);
            repaint (flyRect_.expanded (2));
        }
        return;
    }
    if (dragParam_ >= 0) applyParamFromX (dragParam_, e.getPosition().x);
}

void PlayScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    pressPos_ = p;

    // Open dropdown overlay consumes presses (tap selects, drag scrolls).
    if (menu_ != Menu::None) {
        if (menuRect_.contains (p)) {
            menuPressed_ = true;
            menuMoved_ = false;
            menuPressPos_ = p;
            menuPressScroll_ = menuScroll_;
            return;
        }
        closeMenu();
        return;   // outside tap just closes
    }

    // Filters flyout consumes presses while open (tap selects, drag scrolls).
    if (! flyRect_.isEmpty()) {
        if (flyRect_.contains (p)) {
            flyPressed_ = true;
            flyMoved_ = false;
            flyPressPos_ = p;
            flyPressScroll_ = flyScroll_;
            return;
        }
        flyOpen_ = false;
        layout();
        repaint();
        return;   // outside tap just closes
    }

    if (! gearDdRect_.isEmpty() && gearDdRect_.contains (p)) {
        openMenu (Menu::Gear, gearDdRect_, gearNames_, gearSel_);
        return;
    }
    if (! filterBtnRect_.isEmpty() && filterBtnRect_.contains (p)) {
        flyOpen_ = true;
        layout();
        repaint();
        return;
    }

    // Settings face: sliders + PAIR/DEMO controls.
    if (flipped_ && flip_ >= 1.0f) {
        for (int i = 0; i < kNumToneParams; ++i)
            if (paramRows_[(size_t) i].expanded (0, 6).contains (p)) {
                dragParam_ = i;
                applyParamFromX (i, p.x);
                return;
            }
        if (pairRowRect_.contains (p) && ! pairNames_.isEmpty()) {
            openMenu (Menu::Pair, pairRowRect_, pairNames_, pairSel_);
            return;
        }
        if (demoRowRect_.contains (p)) {
            juce::StringArray tracks;
            for (int i = 0; i < nam::demo::kNumTracks; ++i)
                tracks.add (nam::demo::kTracks[i].display);
            openMenu (Menu::Demo, demoRowRect_, std::move (tracks), demoSel_);
            return;
        }
        if (demoPlayRect_.expanded (4).contains (p)) { if (onToggleDemo) onToggleDemo(); return; }
    }

    // Front-face heart + download buttons.
    if (! flipped_ && heartRect_.expanded (4).contains (p)) {
        if (onKeepToggle) onKeepToggle();
        if (! kept_) { burst_ = 1.0f; startTimerHz (60); }   // popping INTO the deck
        return;
    }
    if (! flipped_ && saveRect_.expanded (4).contains (p)) {
        if (onSaveToggle) onSaveToggle();
        return;
    }

    if (prevRect_.expanded (6).contains (p)) { if (onPrev) onPrev(); return; }
    if (nextRect_.expanded (6).contains (p)) { if (onNext) onNext(); return; }
    if (pagePrev_ && pagePrevRect_.expanded (4).contains (p)) {
        if (onPageDelta) onPageDelta (-1);
        return;
    }
    if (pageNext_ && pageNextRect_.expanded (4).contains (p)) {
        if (onPageDelta) onPageDelta (+1);
        return;
    }
    if (tunerRect_.contains (p)) { if (onTuner) onTuner(); return; }
    if (dotsRect_.contains (p) && count_ <= (int) dotRects_.size())
        for (int k = 0; k < count_; ++k)
            if (dotRects_[(size_t) k].expanded (2, 7).contains (p)) {
                if (onSelectIndex) onSelectIndex (k);
                return;
            }
}
