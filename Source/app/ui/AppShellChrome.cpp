#include "app/ui/AppShell.h"
#include "app/ui/NamLookAndFeel.h"

// AppShell's persistent chrome: the bottom nav (deck buttons, status orb,
// stacks, more-menu), the orb flyout (ENGINE / I/O / TEST TONE) with its
// pickers, and their hit-testing. Split out of AppShell.cpp per the
// no-god-files rule; screen orchestration stays in AppShell.cpp.

void AppShell::paint (juce::Graphics& g) {
    // Global bottom chrome only — screens paint everything above it.
    g.setColour (nam::ui::col::bg);
    g.fillRect (navBar_);

    // Nav buttons flanking the orb: vector icon over a micro-label. Deck
    // buttons light up for the Play deck they select; STACKS for its screen.
    {
        const bool onPlay = (current_ == play_.get ());
        auto navBtn = [&] (juce::Rectangle<int> r, const char* label, bool active, int iconKind) {
            const auto c = active ? nam::ui::col::accent : nam::ui::col::inkA (0.45f);
            const auto ib = r.withSizeKeepingCentre (18, 18).toFloat ().translated (0.0f, -8.0f);
            g.setColour (c);
            switch (iconKind) {
                case 0: {   // magnifier (browse)
                    const juce::Rectangle<float> lens (ib.getX () + 1.0f, ib.getY () + 1.0f, 11.0f,
                                                       11.0f);
                    g.drawEllipse (lens, 1.6f);
                    g.drawLine ({ { lens.getRight () - 1.5f, lens.getBottom () - 1.5f },
                                  { ib.getRight () - 1.0f, ib.getBottom () - 1.0f } },
                                1.8f);
                    break;
                }
                case 1: {   // heart (favorites)
                    juce::Path p;
                    p.startNewSubPath (0.50f, 0.32f);
                    p.cubicTo (0.50f, 0.20f, 0.38f, 0.12f, 0.27f, 0.12f);
                    p.cubicTo (0.11f, 0.12f, 0.04f, 0.26f, 0.04f, 0.38f);
                    p.cubicTo (0.04f, 0.58f, 0.26f, 0.74f, 0.50f, 0.92f);
                    p.cubicTo (0.74f, 0.74f, 0.96f, 0.58f, 0.96f, 0.38f);
                    p.cubicTo (0.96f, 0.26f, 0.89f, 0.12f, 0.73f, 0.12f);
                    p.cubicTo (0.62f, 0.12f, 0.50f, 0.20f, 0.50f, 0.32f);
                    p.closeSubPath ();
                    p.applyTransform (juce::AffineTransform::scale (ib.getWidth (), ib.getHeight ())
                                          .translated (ib.getX (), ib.getY ()));
                    if (active) g.fillPath (p);
                    else g.strokePath (p, juce::PathStrokeType (1.5f));
                    break;
                }
                case 2: {   // down arrow into tray (downloaded)
                    const float cx = ib.getCentreX ();
                    g.drawLine ({ { cx, ib.getY () + 1.0f }, { cx, ib.getBottom () - 6.0f } },
                                1.8f);
                    juce::Path a;
                    a.addTriangle (cx - 4.5f, ib.getBottom () - 8.5f, cx + 4.5f,
                                   ib.getBottom () - 8.5f, cx, ib.getBottom () - 3.5f);
                    g.fillPath (a);
                    g.fillRoundedRectangle (ib.getX (), ib.getBottom () - 1.5f, ib.getWidth (),
                                            1.8f, 0.9f);
                    break;
                }
                case 3: {   // three stacked bars (stacks)
                    for (int i = 0; i < 3; ++i)
                        g.fillRoundedRectangle (ib.getX () + (float)(2 - i) * 1.5f,
                                                ib.getY () + 1.0f + (float)i * 5.5f,
                                                ib.getWidth () - (float)(2 - i) * 3.0f, 3.0f, 1.5f);
                    break;
                }
                default: {   // horizontal three dots (more)
                    for (int i = 0; i < 3; ++i)
                        g.fillEllipse (ib.getX () + (float)i * 6.5f, ib.getCentreY () - 1.75f, 3.5f,
                                       3.5f);
                    break;
                }
            }
            g.setFont (nam::ui::uiFontTracked (8.0f, true));
            g.setColour (c);
            g.drawText (label, r.withTrimmedTop (r.getHeight () / 2 + 6),
                        juce::Justification::centredTop, false);
        };
        navBtn (navBrowseRect_, "BROWSE", onPlay && deckMode_ == 2, 0);
        navBtn (navFavRect_, "FAVORITES", onPlay && deckMode_ == 0, 1);
        navBtn (navStacksRect_, "STACKS", current_ == stacks_.get (), 3);
        navBtn (navMoreRect_, "MORE", moreOpen_ || (onPlay && deckMode_ == 1), 4);
    }

    // Status orb: input level = left arc, output level = right arc, both
    // filling upward from 6 o'clock. Centre shows the round-trip latency
    // (green -> red as it degrades). A muted side's track turns red.
    {
        const auto ob = orbRect_.toFloat ().reduced (3.0f);
        const auto cx = ob.getCentreX (), cy = ob.getCentreY ();
        const float r = ob.getWidth () * 0.5f;
        constexpr float pi = juce::MathConstants<float>::pi;

        auto levelFrac = [] (float peak) {
            const float db = peak > 0.001f ? 20.0f * std::log10 (peak) : -60.0f;
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        auto arc = [&] (float fromRad, float toRad, juce::Colour c, float thickness) {
            juce::Path p;
            p.addCentredArc (cx, cy, r, r, 0.0f, fromRad, toRad, true);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        };

        // Tracks (6 o'clock up each side). Red = that side is muted.
        arc (pi, 2.0f * pi, inMuted_ ? juce::Colour (0x55ff3b30) : nam::ui::col::inkA (0.10f),
             3.0f);
        arc (pi, 0.0f, outMuted_ ? juce::Colour (0x55ff3b30) : nam::ui::col::inkA (0.10f), 3.0f);

        // Levels: input grows both ways out of 9 o'clock, output out of
        // 3 o'clock (full level = that side's whole semicircle).
        if (!inMuted_) {
            const float f = levelFrac (meterInPeak_);
            if (f > 0.01f)
                arc (1.5f * pi - f * 0.5f * pi, 1.5f * pi + f * 0.5f * pi, nam::ui::col::meterLime,
                     3.5f);
        }
        if (!outMuted_) {
            const float f = levelFrac (meterOutPeak_);
            if (f > 0.01f)
                arc (0.5f * pi - f * 0.5f * pi, 0.5f * pi + f * 0.5f * pi, nam::ui::col::meterBlue,
                     3.5f);   // output = blue (vs lime input)
        }

        // Centre: latency ms over a "LATENCY" micro-label, or MUTE when the
        // output is silenced.
        const float bad = juce::jlimit (0.0f, 1.0f, ((float)latencyMs_ - 20.0f) / 30.0f);
        const auto readout =
            outMuted_ ? juce::Colour (0xffff3b30)
                      : nam::ui::col::meterLime.interpolatedWith (juce::Colour (0xffff3b30), bad);
        if (outMuted_) {
            g.setFont (nam::ui::uiFont (9.0f, true));
            g.setColour (readout);
            g.drawText ("MUTE", orbRect_, juce::Justification::centred, false);
        } else {
            g.setFont (nam::ui::uiFont (10.0f, true));
            g.setColour (readout);
            g.drawText (latencyMs_ > 0.0 ? juce::String ((int)std::round (latencyMs_))
                                         : juce::String ("--"),
                        orbRect_.withTrimmedBottom (10), juce::Justification::centred, false);
            g.setFont (nam::ui::uiFontTracked (5.5f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText ("LATENCY", orbRect_.withTrimmedTop (orbRect_.getHeight () / 2 + 2),
                        juce::Justification::centredTop, false);
        }
    }
}

void AppShell::paintOverChildren (juce::Graphics& g) {
    // I/O device picker (replaces the panel while choosing a device).
    if (ioPanelOpen_ && ioPicker_ != 0) {
        auto shortName = [] (juce::String n) {
            return n.replace ("USB-Audio - ", "").replace (" USB headset", "").trim ();
        };
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (ioPickerRect_.toFloat (), 14.0f);
        g.setColour (nam::ui::col::inkA (0.18f));
        g.drawRoundedRectangle (ioPickerRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
        g.setFont (nam::ui::uiFontTracked (9.0f, true));
        g.setColour (nam::ui::col::inkA (0.4f));
        g.drawText (ioPicker_ == 2   ? "OUTPUT DEVICE"
                    : ioPicker_ == 1 ? "INPUT DEVICE"
                    : ioPicker_ == 3 ? "SAMPLE RATE"
                                     : "BUFFER SIZE",
                    ioPickerRect_.getX () + 18, ioPickerRect_.getY () + 10,
                    ioPickerRect_.getWidth () - 36, 16, juce::Justification::centredLeft, false);
        g.saveState ();
        juce::Path clip;
        clip.addRoundedRectangle (ioPickerRect_.toFloat ().reduced (1.0f).withTrimmedTop (30.0f),
                                  13.0f);
        g.reduceClipRegion (clip);
        constexpr int rowH = 44, titleH = 34, pad = 8;
        for (int i = 0; i < ioPickerItems_.size (); ++i) {
            const juce::Rectangle<int> row (ioPickerRect_.getX () + 6,
                                            ioPickerRect_.getY () + titleH + pad + i * rowH -
                                                (int)ioPickerScroll_,
                                            ioPickerRect_.getWidth () - 12, rowH);
            if (row.getBottom () < ioPickerRect_.getY () ||
                row.getY () > ioPickerRect_.getBottom ())
                continue;
            const bool sel = ioPickerItems_[i] == ioPickerCurrent_;
            if (sel) {
                g.setColour (nam::ui::col::accentA (0.10f));
                g.fillRoundedRectangle (row.toFloat (), 9.0f);
            }
            auto in = row.reduced (12, 0);
            g.setFont (nam::ui::uiFont (12.0f, false));
            g.setColour (nam::ui::col::accentAlt);
            g.drawText (sel ? juce::String::fromUTF8 ("\xE2\x9C\x93") : juce::String (),
                        in.removeFromLeft (18), juce::Justification::centredLeft, false);
            g.setFont (nam::ui::uiFont (13.0f, sel));
            g.setColour (sel ? nam::ui::col::accentAlt : nam::ui::col::inkA (0.85f));
            g.drawText (shortName (ioPickerItems_[i]), in, juce::Justification::centredLeft, false);
        }
        g.restoreState ();
        if (ioPickerContentH_ > ioPickerRect_.getHeight ()) {
            const float frac = (float)ioPickerRect_.getHeight () / (float)ioPickerContentH_;
            const float thumbH = juce::jmax (24.0f, ioPickerRect_.getHeight () * frac);
            const float travel = (float)ioPickerRect_.getHeight () - thumbH - 8.0f;
            const float pos =
                ioPickerScroll_ / (float)(ioPickerContentH_ - ioPickerRect_.getHeight ());
            g.setColour (nam::ui::col::inkA (0.25f));
            g.fillRoundedRectangle ((float)ioPickerRect_.getRight () - 7.0f,
                                    (float)ioPickerRect_.getY () + 4.0f + travel * pos, 3.0f,
                                    thumbH, 1.5f);
        }
        return;
    }

    // I/O mute panel (over the current screen, anchored to the orb).
    if (ioPanelOpen_) {
        g.setColour (nam::ui::col::bg);
        g.fillRoundedRectangle (ioPanelRect_.toFloat (), 14.0f);
        g.setColour (nam::ui::col::inkA (0.03f));
        g.fillRoundedRectangle (ioPanelRect_.toFloat (), 14.0f);
        g.setColour (nam::ui::col::inkA (0.16f));
        g.drawRoundedRectangle (ioPanelRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);

        auto shortName = [] (juce::String n) {
            return n.replace ("USB-Audio - ", "")
                .replace (" USB headset", "")
                .replace ("System Default (Input)", "System Default")
                .replace ("System Default (Output)", "System Default")
                .trim ();
        };
        juce::String inName ("--"), outName ("--"), rate ("--"), buffer ("--");
        if (getDevices_) {
            const auto st = getDevices_ ();
            inName = shortName (st.currentInput);
            outName = shortName (st.currentOutput);
            rate = st.currentRate.isNotEmpty () ? st.currentRate : juce::String ("--");
            buffer = st.currentBuffer.isNotEmpty () ? st.currentBuffer : juce::String ("--");
        }

        // ENGINE row: sample rate + buffer as tap-to-pick pills.
        {
            g.setColour (nam::ui::col::inkA (0.04f));
            g.fillRoundedRectangle (ioEngRow_.toFloat (), 10.0f);
            auto inner = ioEngRow_.reduced (14, 6);
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText ("ENGINE", inner.removeFromTop (inner.getHeight () / 2),
                        juce::Justification::bottomLeft, false);
            g.setFont (nam::ui::uiFont (11.0f, false));
            g.setColour (nam::ui::col::inkA (0.55f));
            g.drawText (latencyMs_ > 0.0
                            ? juce::String ((int)std::round (latencyMs_)) + " ms round trip"
                            : juce::String (),
                        inner, juce::Justification::topLeft, false);
            auto enginePill = [&] (juce::Rectangle<int> rr, const juce::String& value,
                                   const char* caption) {
                nam::ui::drawPill (g, rr.toFloat (), juce::Colours::transparentBlack,
                                   nam::ui::col::inkA (0.2f));
                auto in = rr.reduced (10, 0);
                g.setFont (nam::ui::uiFont (10.0f, false));
                g.setColour (nam::ui::col::inkA (0.5f));
                g.drawText (juce::String::fromUTF8 ("\xE2\x96\xBE"), in.removeFromRight (12),
                            juce::Justification::centred, false);
                g.setFont (nam::ui::uiFont (12.0f, true));
                g.setColour (nam::ui::col::ink);
                g.drawText (value, in, juce::Justification::centred, false);
                g.setFont (nam::ui::uiFontTracked (7.0f, true));
                g.setColour (nam::ui::col::inkA (0.4f));
                g.drawText (juce::String (caption),
                            juce::Rectangle<int> (rr.getX (), rr.getY () - 12, rr.getWidth (), 10),
                            juce::Justification::centred, false);
            };
            enginePill (ioRatePill_, rate, "RATE");
            enginePill (ioBufPill_, buffer, "BUFFER");
        }

        auto row = [&] (juce::Rectangle<int> rr, const juce::String& label,
                        const juce::String& device, bool muted, float level,
                        juce::Colour levelColour) {
            g.setColour (nam::ui::col::inkA (muted ? 0.02f : 0.04f));
            g.fillRoundedRectangle (rr.toFloat (), 10.0f);
            auto inner = rr.reduced (14, 6);
            auto toggle = inner.removeFromRight (74);
            auto meter =
                inner.removeFromRight (juce::jmax (60, inner.getWidth () / 3)).reduced (10, 0);
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (nam::ui::col::inkA (0.45f));
            g.drawText (label, inner.removeFromTop (inner.getHeight () / 2),
                        juce::Justification::bottomLeft, false);
            g.setFont (nam::ui::uiFont (13.0f, true));
            g.setColour (muted ? nam::ui::col::inkA (0.4f) : nam::ui::col::ink);
            g.drawText (device, inner, juce::Justification::topLeft, false);
            // Horizontal level meter (Hi-Fi design), dead when muted.
            {
                const auto track = meter.withSizeKeepingCentre (meter.getWidth (), 5).toFloat ();
                g.setColour (nam::ui::col::inkA (0.10f));
                g.fillRoundedRectangle (track, 2.5f);
                const float db = level > 0.001f ? 20.0f * std::log10 (level) : -60.0f;
                const float f = muted ? 0.0f : juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                if (f > 0.01f) {
                    g.setColour (levelColour);
                    g.fillRoundedRectangle (
                        track.withWidth (juce::jmax (4.0f, track.getWidth () * f)), 2.5f);
                }
            }
            nam::ui::drawPill (g, toggle.toFloat (),
                               muted ? juce::Colour (0x33ff3b30) : juce::Colours::transparentBlack,
                               muted ? juce::Colour (0xaaff3b30) : nam::ui::col::inkA (0.2f));
            g.setFont (nam::ui::uiFontTracked (10.0f, true));
            g.setColour (muted ? juce::Colour (0xffff3b30) : nam::ui::col::inkA (0.6f));
            g.drawText (muted ? "MUTED" : "MUTE", toggle, juce::Justification::centred, false);
        };
        row (ioInRow_, "INPUT", inName, inMuted_, meterInPeak_, nam::ui::col::meterLime);
        row (ioOutRow_, "OUTPUT", outName, outMuted_, meterOutPeak_, nam::ui::col::meterBlue);

        // TEST TONE: plays a reference sine through the output path.
        nam::ui::drawPill (g, ioTestRect_.toFloat (),
                           testToneOn_ ? nam::ui::col::accentA (0.16f)
                                       : juce::Colours::transparentBlack,
                           testToneOn_ ? nam::ui::col::accentA (0.8f) : nam::ui::col::inkA (0.2f));
        g.setFont (nam::ui::uiFontTracked (10.0f, true));
        g.setColour (testToneOn_ ? nam::ui::col::accent : nam::ui::col::inkA (0.6f));
        g.drawText (testToneOn_ ? "STOP TONE" : "TEST TONE", ioTestRect_,
                    juce::Justification::centred, false);
    }

    // ⋯ menu (right corner, above the nav) — painted last (overlay rule).
    if (moreOpen_) {
        g.setColour (juce::Colour (0xf214101f));
        g.fillRoundedRectangle (moreRect_.toFloat (), 14.0f);
        g.setColour (nam::ui::col::inkA (0.18f));
        g.drawRoundedRectangle (moreRect_.toFloat ().reduced (0.5f), 14.0f, 1.0f);
        const bool active = (current_ == play_.get () && deckMode_ == 1);
        if (active) {
            g.setColour (nam::ui::col::accentA (0.10f));
            g.fillRoundedRectangle (moreDownloadedRect_.toFloat (), 9.0f);
        }
        // Down-arrow-into-tray icon + label (DOWNLOADED lives here now).
        auto in = moreDownloadedRect_.reduced (14, 10);
        const auto ib = in.removeFromLeft (18).toFloat ().withSizeKeepingCentre (16.0f, 16.0f);
        g.setColour (active ? nam::ui::col::accent : nam::ui::col::inkA (0.7f));
        const float cx = ib.getCentreX ();
        g.drawLine ({ { cx, ib.getY () + 1.0f }, { cx, ib.getBottom () - 6.0f } }, 1.8f);
        juce::Path a;
        a.addTriangle (cx - 4.0f, ib.getBottom () - 8.0f, cx + 4.0f, ib.getBottom () - 8.0f, cx,
                       ib.getBottom () - 3.5f);
        g.fillPath (a);
        g.fillRoundedRectangle (ib.getX (), ib.getBottom () - 1.5f, ib.getWidth (), 1.8f, 0.9f);
        g.setFont (nam::ui::uiFont (13.0f, true));
        g.setColour (active ? nam::ui::col::accent : nam::ui::col::inkA (0.85f));
        g.drawText ("Downloaded", in.withTrimmedLeft (12), juce::Justification::centredLeft, false);

        // TEMP Task 4: debug trigger for the paywall overlay (removed in
        // Task 5's commit alongside AndroidAudioApp's #if 0 wiring cleanup).
        g.setFont (nam::ui::uiFont (13.0f, true));
        g.setColour (nam::ui::col::accentA (0.85f));
        g.drawText ("Paywall (debug)", morePaywallDebugRect_.reduced (14, 10).withTrimmedLeft (30),
                    juce::Justification::centredLeft, false);
    }
}

void AppShell::closeIoPanel () {
    if (!ioPanelOpen_) return;
    ioPanelOpen_ = false;
    ioPicker_ = 0;
    if (testToneOn_) {   // never leave the check tone running unattended
        testToneOn_ = false;
        if (svc_.setTestTone) svc_.setTestTone (false);
    }
    ioScrim_.setVisible (false);
    repaint ();
}

void AppShell::openMoreMenu () {
    // Content-sized, right-anchored above the nav (house overlay style).
    // TEMP Task 4: a second "PAYWALL (DEBUG)" row is appended below
    // DOWNLOADED for emulator verification; removed in Task 5's commit.
    constexpr int rowH = 46, w = 210, numRows = 2;
    moreRect_ = { getWidth () - w - 12, navBar_.getY () - rowH * numRows - 20, w,
                  rowH * numRows + 12 };
    auto rows = moreRect_.reduced (6);
    moreDownloadedRect_ = rows.removeFromTop (rowH);
    morePaywallDebugRect_ = rows.removeFromTop (rowH);
    moreOpen_ = true;
    moreScrim_.setBounds (contentBounds ());
    moreScrim_.setVisible (true);
    moreScrim_.toFront (false);
    repaint ();
}

void AppShell::closeMoreMenu () {
    if (!moreOpen_) return;
    moreOpen_ = false;
    moreScrim_.setVisible (false);
    repaint ();
}

void AppShell::openIoPicker (bool output) {
    if (!getDevices_) return;
    const auto st = getDevices_ ();
    ioPickerItems_ = output ? st.outputs : st.inputs;
    ioPickerCurrent_ = output ? st.currentOutput : st.currentInput;
    ioPicker_ = output ? 2 : 1;
    ioPickerScroll_ = 0.0f;
    constexpr int rowH = 44, titleH = 34, pad = 8;
    ioPickerContentH_ = titleH + pad + rowH * ioPickerItems_.size () + pad;
    const int pw = juce::jmin (380, getWidth () - 48);
    // Height-capped (overlay rule): never more than ~55% of the content.
    const int maxH = contentBounds ().getHeight () * 55 / 100;
    const int h = juce::jmin (ioPickerContentH_, maxH);
    ioPickerRect_ = { getWidth () / 2 - pw / 2, navBar_.getY () - h - 10, pw, h };
    repaint ();
}

void AppShell::openEnginePicker (bool buffer) {
    if (!getDevices_) return;
    const auto st = getDevices_ ();
    ioPickerItems_ = buffer ? st.buffers : st.rates;
    ioPickerCurrent_ = buffer ? st.currentBuffer : st.currentRate;
    ioPicker_ = buffer ? 4 : 3;
    ioPickerScroll_ = 0.0f;
    constexpr int rowH = 44, titleH = 34, pad = 8;
    ioPickerContentH_ = titleH + pad + rowH * ioPickerItems_.size () + pad;
    const int pw = juce::jmin (380, getWidth () - 48);
    const int maxH = contentBounds ().getHeight () * 55 / 100;   // overlay rule
    const int h = juce::jmin (ioPickerContentH_, maxH);
    ioPickerRect_ = { getWidth () / 2 - pw / 2, navBar_.getY () - h - 10, pw, h };
    repaint ();
}

void AppShell::handleIoPanelTap (juce::Point<int> p) {
    if (ioPicker_ != 0) {
        if (ioPickerRect_.contains (p)) {
            ioPickerPressed_ = true;
            ioPickerMoved_ = false;
            ioPickerPressPos_ = p;
            ioPickerPressScroll_ = ioPickerScroll_;
        } else {
            ioPicker_ = 0;   // back to the mute panel
            repaint ();
        }
        return;
    }
    // Main panel: ENGINE pills open the rate/buffer pickers; the toggle
    // pill mutes; the device area opens the device picker.
    if (ioRatePill_.expanded (4).contains (p)) {
        openEnginePicker (false);
        return;
    }
    if (ioBufPill_.expanded (4).contains (p)) {
        openEnginePicker (true);
        return;
    }
    if (ioTestRect_.expanded (6).contains (p)) {
        testToneOn_ = !testToneOn_;
        if (testToneOn_ && outMuted_ && muteOutput_) {   // hearing it is the point
            outMuted_ = false;
            muteOutput_ (false);
        }
        if (svc_.setTestTone) svc_.setTestTone (testToneOn_);
        repaint (ioPanelRect_.expanded (4));
        return;
    }
    if (ioEngRow_.contains (p)) return;   // row body: nothing to toggle
    if (ioInRow_.contains (p)) {
        if (p.x > ioInRow_.getRight () - 96) {
            inMuted_ = !inMuted_;
            if (muteInput_) muteInput_ (inMuted_);
            repaint (ioPanelRect_);
            repaint (orbRect_.expanded (4));
        } else {
            openIoPicker (false);
        }
        return;
    }
    if (ioOutRow_.contains (p)) {
        if (p.x > ioOutRow_.getRight () - 96) {
            outMuted_ = !outMuted_;
            if (muteOutput_) muteOutput_ (outMuted_);
            repaint (ioPanelRect_);
            repaint (orbRect_.expanded (4));
        } else {
            openIoPicker (true);
        }
        return;
    }
    closeIoPanel ();
}

void AppShell::handleIoDragAt (juce::Point<int> p) {
    if (!ioPickerPressed_) return;
    const int dy = p.y - ioPickerPressPos_.y;
    if (std::abs (dy) > 8) ioPickerMoved_ = true;
    if (ioPickerMoved_) {
        ioPickerScroll_ = juce::jlimit (
            0.0f, (float)juce::jmax (0, ioPickerContentH_ - ioPickerRect_.getHeight ()),
            ioPickerPressScroll_ - (float)dy);
        repaint (ioPickerRect_.expanded (2));
    }
}

void AppShell::handleIoUpAt (juce::Point<int> p) {
    if (!ioPickerPressed_) return;
    if (!ioPickerMoved_) {
        constexpr int rowH = 44, titleH = 34, pad = 8;
        const int i = (p.y - ioPickerRect_.getY () - titleH - pad + (int)ioPickerScroll_) / rowH;
        if (i >= 0 && i < ioPickerItems_.size ()) {
            const auto name = ioPickerItems_[i];
            switch (ioPicker_) {
                case 2:
                    if (selectOutput_) selectOutput_ (name);
                    break;
                case 1:
                    if (selectInput_) selectInput_ (name);
                    break;
                case 3:
                    if (selectRate_) selectRate_ (name);
                    break;
                default:
                    if (selectBuffer_) selectBuffer_ (name);
                    break;
            }
            ioPicker_ = 0;   // back to the panel with the new pick shown
            repaint ();
        }
    }
    ioPickerPressed_ = ioPickerMoved_ = false;
}

void AppShell::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();

    if (ioPanelOpen_) {   // taps on the nav while the panel is up just close it
        closeIoPanel ();
        return;
    }
    if (moreOpen_) {
        closeMoreMenu ();
        return;
    }

    if (orbRect_.expanded (6).contains (p)) {
        ioPanelOpen_ = true;
        ioScrim_.setBounds (contentBounds ());
        ioScrim_.setVisible (true);
        ioScrim_.toFront (false);
        repaint ();
        return;
    }

    // Nav buttons: deck pickers left of the orb; STACKS + ⋯ menu right.
    if (navBrowseRect_.contains (p)) {
        setDeckMode (2);
        return;
    }
    if (navFavRect_.contains (p)) {
        setDeckMode (0);
        return;
    }
    if (navStacksRect_.contains (p)) {
        show (Screen::Stacks);
        repaint (navBar_);
        return;
    }
    if (navMoreRect_.contains (p)) {
        openMoreMenu ();
        return;
    }

    // Tapping the bar anywhere else returns home to Play.
    if (navBar_.contains (p) && current_ != play_.get ()) show (Screen::Play);
}
