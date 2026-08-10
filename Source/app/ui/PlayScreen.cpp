#include "app/ui/PlayScreen.h"
#include "app/ui/DemoTrackCatalog.h"
#include "app/ui/NamLookAndFeel.h"

#include <cmath>

using namespace nam::ui;

namespace {
const char* kTagChips[]  = { "clean", "blues", "vintage", "metal", "high gain" };
const char* kMakeChips[] = { "Marshall", "Fender", "Vox", "Mesa", "Orange", "EVH" };

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
    repaint (artRect_);
}

void PlayScreen::setKept (bool kept) {
    if (kept_ == kept) return;
    kept_ = kept;
    repaint (artRect_);
}

void PlayScreen::setDemoPlaying (bool on) {
    if (demoPlaying_ == on) return;
    demoPlaying_ = on;
    repaint (artRect_);
}

void PlayScreen::setCabChoices (juce::StringArray names, int sel) {
    cabNames_ = std::move (names);
    cabSel_ = sel;
    repaint (artRect_);
}

void PlayScreen::setDeckView (int v) {
    if (view_ == v) return;
    view_ = v;
    flyOpen_ = false;
    layout();
    startTimerHz (60);   // slide the toggle thumb
    repaint();
}

void PlayScreen::composeBrowseQuery() {
    juce::String q;
    if (gearSel_ == 1) q << "#ir ";
    for (const auto& t : selTags_)  q << t << " ";
    for (const auto& m : selMakes_) q << m << " ";
    if (onBrowseQuery) onBrowseQuery (q.trim());
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

void PlayScreen::resized() { layout(); }

void PlayScreen::layout() {
    auto r = getLocalBounds();
    topBar_  = r.removeFromTop (juce::jmax (52, r.getHeight() / 15));
    metersRow_ = r.removeFromBottom (juce::jmax (78, r.getHeight() / 9)).reduced (20, 6);
    hero_    = r;

    // Top bar: NAM PLAYER wordmark left; edit / live / settings icons right.
    libRect_ = {};
    gearRect_    = { topBar_.getRight() - 58,  topBar_.getCentreY() - 21, 42, 42 };
    liveTopRect_ = editTopRect_ = {};   // gear only (Hi-Fi design)

    // View toggle slider (one pill, sliding thumb) + filters, under the bar.
    auto strip = hero_.removeFromTop (44);
    viewRow_ = strip.reduced (26, 5);
    {
        const int w = 118, h = viewRow_.getHeight();
        const bool browse = view_ == 1;
        const int total = w * 2 + 6 + (browse ? h + 10 : 0);
        int x = viewRow_.getCentreX() - total / 2;
        favViewRect_    = { x + 3, viewRow_.getY() + 3, w, h - 6 };
        browseViewRect_ = { x + 3 + w, viewRow_.getY() + 3, w, h - 6 };
        x += w * 2 + 6 + 10;
        filterBtnRect_  = browse ? juce::Rectangle<int> { x, viewRow_.getY(), h, h }
                                 : juce::Rectangle<int> {};
    }

    // Filters flyout (browse view): chips laid out under the strip.
    flyChips_.clear();
    if (flyOpen_ && view_ == 1) {
        const int chipH = 30, gap = 6;
        int x = hero_.getX() + 34, y = viewRow_.getBottom() + 14;
        auto place = [&] (const juce::String& label) {
            const int w = juce::jmax (56, label.length() * 8 + 24);
            if (x + w > getWidth() - 34) { x = hero_.getX() + 34; y += chipH + gap; }
            flyChips_.push_back ({ { x, y, w, chipH }, label });
            x += w + gap;
        };
        place ("AMPS"); place ("CABS");
        x = hero_.getX() + 34; y += chipH + gap + 6;
        for (const char* t : kTagChips)  place (t);
        x = hero_.getX() + 34; y += chipH + gap + 6;
        for (const char* m : kMakeChips) place (m);
        flyRect_ = { hero_.getX() + 24, viewRow_.getBottom() + 6,
                     hero_.getWidth() - 48, (y + chipH + 12) - (viewRow_.getBottom() + 6) };
    } else {
        flyRect_ = {};
    }

    // Full-bleed tone card with the text footer INSIDE it; dots row below.
    auto inner = hero_.reduced (26, 8);
    dotsRect_ = inner.removeFromBottom (26);
    inner.removeFromBottom (4);
    artRect_ = inner;
    textRect_ = transportRect_ = {};

    // Card footer heart (front face).
    heartRect_ = { artRect_.getRight() - 64, artRect_.getBottom() - 64, 46, 46 };

    // Chevrons float at the card's outer edges.
    prevRect_ = { hero_.getX() + 2,        artRect_.getCentreY() - 34, 26, 68 };
    nextRect_ = { hero_.getRight() - 28,   artRect_.getCentreY() - 34, 26, 68 };

    // Pagination dot hit rects (capped; beyond that a bar is drawn instead).
    {
        const int n = juce::jlimit (0, (int) dotRects_.size(), count_);
        const int active = 22, idle = 8, gap = 8;
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

    // Card-back settings rows (drawn/hit only while flipped), then the
    // PAIR-IR and DEMO AUDIO rows (Hi-Fi design).
    {
        auto rows = artRect_.reduced (24, 16);
        rows.removeFromTop (40);   // name + return button header
        rows.removeFromTop (22);   // QUICK SETTINGS label
        auto bottom = rows.removeFromBottom (104);
        const int rowH = juce::jmin (48, rows.getHeight() / kNumToneParams);
        for (int i = 0; i < kNumToneParams; ++i)
            paramRows_[(size_t) i] = rows.removeFromTop (rowH).reduced (0, 6);
        pairRowRect_ = bottom.removeFromTop (46).reduced (0, 4);
        auto demo = bottom.removeFromTop (46).reduced (0, 4);
        demoPlayRect_ = demo.removeFromRight (44);
        demoRowRect_ = demo.withTrimmedRight (8);
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
    const float slideTarget = (float) view_;
    if (std::abs (viewSlide_ - slideTarget) > 0.001f) {
        const float step = 1.0f / 8.0f;
        viewSlide_ = juce::jlimit (0.0f, 1.0f,
            viewSlide_ + (slideTarget > viewSlide_ ? step : -step));
        busy = busy || std::abs (viewSlide_ - slideTarget) > 0.001f;
        repaint (viewRow_.expanded (4));
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

    // --- Top bar: wordmark + settings gear (Hi-Fi design) ----------------
    {
        auto brand = topBar_.reduced (20, 0);
        g.setFont (uiFontTracked (11.0f, true));
        g.setColour (col::inkA (0.55f));
        const juce::String namPart ("NAM ");
        g.drawText (namPart, brand, juce::Justification::centredLeft, false);
        const int nw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                           uiFontTracked (11.0f, true), namPart));
        g.setColour (col::accent);
        g.drawText ("PLAYER", brand.withTrimmedLeft (nw), juce::Justification::centredLeft, false);
        text (juce::String::fromUTF8 ("\xE2\x9A\x99"), uiFont (22.0f, false),
              col::inkA (0.6f), gearRect_, juce::Justification::centred);
    }

    // --- View toggle slider (single pill, animated thumb) + Filters ------
    {
        const auto container = favViewRect_.getUnion (browseViewRect_).expanded (3);
        drawPill (g, container.toFloat(), col::bg.withAlpha (0.4f), col::inkA (0.16f));
        // Sliding accent thumb.
        const float tx = (float) favViewRect_.getX()
                       + viewSlide_ * (float) (browseViewRect_.getX() - favViewRect_.getX());
        const juce::Rectangle<float> thumb (tx, (float) favViewRect_.getY(),
                                            (float) favViewRect_.getWidth(),
                                            (float) favViewRect_.getHeight());
        drawPill (g, thumb, col::accent, col::accent);
        const auto favCol    = viewSlide_ < 0.5f ? col::inkOnAccent : col::inkA (0.55f);
        const auto browseCol = viewSlide_ >= 0.5f ? col::inkOnAccent : col::inkA (0.55f);
        text ("FAVORITES", uiFontTracked (10.0f, true), favCol,
              favViewRect_.withTrimmedLeft (14), juce::Justification::centred);
        {   // vector heart before the label (emoji fallback ruins the glyph)
            const auto hb = juce::Rectangle<float> (
                (float) favViewRect_.getX() + 13.0f,
                (float) favViewRect_.getCentreY() - 6.0f, 12.0f, 12.0f);
            g.setColour (favCol);
            g.fillPath (heartPath (hb));
        }
        text ("BROWSE", uiFontTracked (10.0f, true), browseCol,
              browseViewRect_, juce::Justification::centred);
        if (! filterBtnRect_.isEmpty()) {
            const int n = selTags_.size() + selMakes_.size() + (gearSel_ == 1 ? 1 : 0);
            const bool hot = flyOpen_ || n > 0;
            g.setColour (flyOpen_ ? col::accentA (0.12f) : col::bg.withAlpha (0.4f));
            g.fillEllipse (filterBtnRect_.toFloat());
            g.setColour (hot ? col::accentA (0.6f) : col::inkA (0.18f));
            g.drawEllipse (filterBtnRect_.toFloat().reduced (0.5f), 1.0f);
            // Funnel-style filter icon: three centred bars, narrowing down.
            g.setColour (hot ? col::accentAlt : col::inkA (0.6f));
            const float cx = (float) filterBtnRect_.getCentreX();
            const float cy = (float) filterBtnRect_.getCentreY();
            const float widths[] = { 18.0f, 12.0f, 6.0f };
            for (int i = 0; i < 3; ++i)
                g.fillRoundedRectangle (cx - widths[i] * 0.5f, cy - 6.0f + (float) i * 5.0f,
                                        widths[i], 2.5f, 1.25f);
            if (n > 0) {   // active-filter badge
                g.setColour (col::accent);
                g.fillEllipse ((float) filterBtnRect_.getRight() - 12.0f,
                               (float) filterBtnRect_.getY() + 2.0f, 9.0f, 9.0f);
            }
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
                ddRow (pairRowRect_, "PAIR CAB",
                       cabNames_.isEmpty() ? juce::String ("--")
                                           : cabNames_[juce::jlimit (0, cabNames_.size() - 1, cabSel_)]);
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
                for (int i = 0; i < kNumToneParams; ++i) {
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
            // Cover-fit the TONE3000 photo (centre crop, preserve aspect).
            const float scale = juce::jmax ((float) artRect_.getWidth()  / (float) art_.getWidth(),
                                            (float) artRect_.getHeight() / (float) art_.getHeight());
            const float w = art_.getWidth() * scale, h = art_.getHeight() * scale;
            g.drawImageTransformed (art_,
                juce::AffineTransform::scale (scale)
                    .translated (artRect_.getCentreX() - w * 0.5f,
                                 artRect_.getCentreY() - h * 0.5f));
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
            auto ft = face.reduced (18, 0).withTrimmedBottom (16);
            auto block = ft.removeFromBottom (author_.isNotEmpty() ? 84 : 62)
                           .withTrimmedRight (56);
            text (family_.toUpperCase(), uiFontTracked (11.0f, false), col::inkA (0.45f),
                  block.removeFromTop (18), juce::Justification::centredLeft);
            text (name_, displayFont (26.0f), col::ink,
                  block.removeFromTop (36), juce::Justification::centredLeft);
            if (author_.isNotEmpty())
                text ("by " + author_, uiFont (12.0f, false), col::inkA (0.5f),
                      block.removeFromTop (22), juce::Justification::centredLeft);

            g.setColour (kept_ ? col::accentA (0.14f) : col::bg.withAlpha (0.35f));
            g.fillEllipse (heartRect_.toFloat());
            g.setColour (kept_ ? col::accentA (0.7f) : col::inkA (0.25f));
            g.drawEllipse (heartRect_.toFloat().reduced (0.5f), 1.0f);
            const auto hb = heartRect_.toFloat().withSizeKeepingCentre (20.0f, 20.0f);
            if (kept_) { g.setColour (col::accent); g.fillPath (heartPath (hb)); }
            else       { g.setColour (col::inkA (0.6f)); g.strokePath (heartPath (hb),
                                                                       juce::PathStrokeType (1.5f)); }

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

    // Filters flyout floats over the card (browse view).
    if (! flyRect_.isEmpty()) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (flyRect_.toFloat(), 14.0f);
        g.setColour (col::inkA (0.18f));
        g.drawRoundedRectangle (flyRect_.toFloat().reduced (0.5f), 14.0f, 1.0f);
        for (const auto& [rr, label] : flyChips_) {
            const bool isGear = (label == "AMPS" || label == "CABS");
            const bool on = isGear ? ((label == "CABS") == (gearSel_ == 1))
                          : selTags_.contains (label) || selMakes_.contains (label);
            drawPill (g, rr.toFloat(), on ? col::accent : juce::Colours::transparentBlack,
                      on ? col::accent : col::inkA (0.2f));
            text (label, uiFont (11.0f, true), on ? col::inkOnAccent : col::inkA (0.7f),
                  rr, juce::Justification::centred);
        }
    }

    // --- Transport: side chevrons + dots pagination ----------------------
    text (juce::String::fromUTF8 ("\xE2\x80\xB9"), uiFont (26.0f, false),
          col::inkA (0.5f), prevRect_, juce::Justification::centred);
    text (juce::String::fromUTF8 ("\xE2\x80\xBA"), uiFont (26.0f, false),
          col::inkA (0.5f), nextRect_, juce::Justification::centred);
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
}

void PlayScreen::mouseUp (const juce::MouseEvent& e) {
    if (dragParam_ >= 0) { dragParam_ = -1; return; }
    const int dx = e.getPosition().x - pressPos_.x;
    const int dy = e.getPosition().y - pressPos_.y;
    const bool tap = std::abs (dx) < 12 && std::abs (dy) < 12;

    // Settings face: any tap that isn't an interactive element flips back
    // (sliders end up in dragParam_; buttons acted in mouseDown). Swipes
    // that START outside the card still step tones — the new tone keeps
    // showing whichever face is up (flip state is untouched).
    if (flipped_) {
        if (tap && ! prevRect_.contains (pressPos_) && ! nextRect_.contains (pressPos_)
                && ! gearRect_.contains (pressPos_) && ! tunerRect_.contains (pressPos_)
                && ! dotsRect_.contains (pressPos_) && ! viewRow_.contains (pressPos_)
                && ! pairRowRect_.contains (pressPos_) && ! demoRowRect_.contains (pressPos_)
                && ! demoPlayRect_.contains (pressPos_)
                && ! editTopRect_.contains (pressPos_) && ! liveTopRect_.contains (pressPos_)) {
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

    // A tap on the card flips it around to the settings face.
    if (tap && artRect_.contains (pressPos_) && ! heartRect_.expanded (4).contains (pressPos_)) {
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
    if (dragParam_ >= 0) applyParamFromX (dragParam_, e.getPosition().x);
}

void PlayScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    pressPos_ = p;

    // Filters flyout consumes taps while open.
    if (! flyRect_.isEmpty()) {
        if (flyRect_.contains (p)) {
            for (const auto& [rr, label] : flyChips_)
                if (rr.contains (p)) {
                    if (label == "AMPS" || label == "CABS") {
                        gearSel_ = (label == "CABS") ? 1 : 0;
                    } else {
                        auto& list = juce::StringArray (kTagChips, 5).contains (label)
                                   ? selTags_ : selMakes_;
                        if (list.contains (label)) list.removeString (label);
                        else                       list.add (label);
                    }
                    repaint();
                    composeBrowseQuery();
                    return;
                }
            return;
        }
        flyOpen_ = false;
        layout();
        repaint();
        return;   // outside tap just closes
    }

    if (gearRect_.expanded (4).contains (p)) { if (onSettings) onSettings(); return; }

    if (favViewRect_.contains (p))    { if (onViewChange) onViewChange (0); return; }
    if (browseViewRect_.contains (p)) { if (onViewChange) onViewChange (1); return; }
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
        if (pairRowRect_.contains (p)) {
            juce::PopupMenu m;
            for (int i = 0; i < cabNames_.size(); ++i)
                m.addItem (i + 1, cabNames_[i], true, i == cabSel_);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this] (int r) {
                    if (r > 0) { cabSel_ = r - 1; if (onSelectCab) onSelectCab (r - 1); repaint (artRect_); }
                });
            return;
        }
        if (demoRowRect_.contains (p)) {
            juce::PopupMenu m;
            for (int i = 0; i < nam::demo::kNumTracks; ++i)
                m.addItem (i + 1, nam::demo::kTracks[i].display, true, i == demoSel_);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this] (int r) {
                    if (r > 0) { demoSel_ = r - 1; if (onSelectDemoTrack) onSelectDemoTrack (r - 1); repaint (artRect_); }
                });
            return;
        }
        if (demoPlayRect_.expanded (4).contains (p)) { if (onToggleDemo) onToggleDemo(); return; }
    }

    // Front-face heart button.
    if (! flipped_ && heartRect_.expanded (4).contains (p)) {
        if (onKeepToggle) onKeepToggle();
        if (! kept_) { burst_ = 1.0f; startTimerHz (60); }   // popping INTO the deck
        return;
    }

    if (prevRect_.expanded (6).contains (p)) { if (onPrev) onPrev(); return; }
    if (nextRect_.expanded (6).contains (p)) { if (onNext) onNext(); return; }
    if (tunerRect_.contains (p)) { if (onTuner) onTuner(); return; }
    if (dotsRect_.contains (p) && count_ <= (int) dotRects_.size())
        for (int k = 0; k < count_; ++k)
            if (dotRects_[(size_t) k].expanded (4, 7).contains (p)) {
                if (onSelectIndex) onSelectIndex (k);
                return;
            }
}
