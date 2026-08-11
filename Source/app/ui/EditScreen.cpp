#include "app/ui/EditScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

namespace {
// UTF-8 glyphs must go through fromUTF8 so JUCE decodes them (and can fall back
// to a system font for ones Work Sans lacks, e.g. the lock emoji).
const juce::String kMinus = juce::String::fromUTF8 ("\xE2\x88\x92");      // −
const juce::String kDot = juce::String::fromUTF8 ("\xC2\xB7");            // ·
const juce::String kMult = juce::String::fromUTF8 ("\xC3\x97");           // ×
const juce::String kLock = juce::String::fromUTF8 ("\xF0\x9F\x94\x92");   // 🔒
const juce::String kChevD = juce::String::fromUTF8 ("\xE2\x96\xBE");      // ▾
const juce::String kChevR = juce::String::fromUTF8 ("\xE2\x96\xB8");      // ▸

juce::String signedDb (double v) {
    const int d = (int)std::lround ((v - 50.0) / 8.0);
    return (d >= 0 ? "+" : "") + juce::String (d) + "dB";
}
}   // namespace

EditScreen::EditScreen (dsp::ToneEngine& engine) : engine_ (engine) {
    setOpaque (true);

    auto makeFader = [this] (const juce::String& name, double initial,
                             std::function<juce::String (double)> fmt,
                             std::function<void (double)> apply) {
        Fader f;
        f.name = name;
        f.fmt = std::move (fmt);
        f.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                   juce::Slider::NoTextBox);
        f.slider->setRange (0.0, 100.0, 1.0);
        f.slider->setValue (initial, juce::dontSendNotification);
        auto* raw = f.slider.get ();
        f.slider->onValueChange = [this, apply, raw] {
            apply (raw->getValue ());
            ++changed_;
            repaint ();
        };
        addChildComponent (*f.slider);
        return f;
    };

    // --- GATE ---
    {
        Stage s;
        s.key = "GATE";
        s.faders.push_back (makeFader (
            "thresh", 28.0,
            [] (double v) {
                return kMinus + juce::String ((int)std::lround (70 - v * 0.5)) + " dB";
            },
            [this] (double v) { engine_.setGateThresholdDb ((float)-(70.0 - v * 0.5)); }));
        s.summary = [this] {
            return kMinus +
                   juce::String (
                       (int)std::lround (70 - stages_[0].faders[0].slider->getValue () * 0.5)) +
                   " dB";
        };
        stages_.push_back (std::move (s));
    }

    // --- AMP (open by default) ---
    {
        Stage s;
        s.key = "AMP";
        s.open = true;
        s.faders.push_back (makeFader (
            "gain", 70.0, [] (double v) { return juce::String ((int)v) + "%"; },
            [this] (double v) { engine_.setInputDb ((float)(-24.0 + v * 0.48)); }));
        s.faders.push_back (makeFader (
            "level", 50.0, [] (double v) { return juce::String ((int)v) + "%"; },
            [this] (double v) { engine_.setOutputDb ((float)(-24.0 + v * 0.48)); }));
        s.summary = [this] {
            return "gain " + juce::String ((int)stages_[1].faders[0].slider->getValue ()) + " " +
                   kDot + " level " + juce::String ((int)stages_[1].faders[1].slider->getValue ());
        };
        stages_.push_back (std::move (s));
    }

    // --- CAB (lockable; cut faders are visual for now — IR has no live cut params) ---
    {
        Stage s;
        s.key = "CAB";
        s.lockable = true;
        s.faders.push_back (makeFader (
            "low cut", 20.0,
            [] (double v) { return juce::String ((int)std::lround (40 + v * 2.6)) + "Hz"; },
            [] (double) {}));
        s.faders.push_back (makeFader (
            "hi cut", 75.0,
            [] (double v) { return juce::String (6 + (int)std::lround (v * 0.06)) + "kHz"; },
            [] (double) {}));
        s.summary = [] { return "Greenback 4" + kMult + "12"; };
        stages_.push_back (std::move (s));
    }

    // --- EQ ---
    {
        Stage s;
        s.key = "EQ";
        s.faders.push_back (makeFader ("low", 50.0, signedDb, [this] (double v) {
            engine_.setLowDb ((float)((v - 50.0) / 8.0));
        }));
        s.faders.push_back (makeFader ("mid", 44.0, signedDb, [this] (double v) {
            engine_.setMidDb ((float)((v - 50.0) / 8.0));
        }));
        s.faders.push_back (makeFader ("high", 56.0, signedDb, [this] (double v) {
            engine_.setHighDb ((float)((v - 50.0) / 8.0));
        }));
        s.summary = [this] {
            auto& f = stages_[3].faders;
            return signedDb (f[0].slider->getValue ()) + " " + signedDb (f[1].slider->getValue ()) +
                   " " + signedDb (f[2].slider->getValue ());
        };
        stages_.push_back (std::move (s));
    }

    // --- DELAY ---
    {
        Stage s;
        s.key = "DELAY";
        s.faders.push_back (makeFader (
            "time", 0.0,
            [] (double v) {
                return v == 0 ? juce::String ("off")
                              : juce::String ((int)std::lround (80 + v * 6)) + "ms";
            },
            [this] (double v) {
                engine_.setDelayTimeMs ((float)(80.0 + v * 6.0));
                engine_.setDelayEnabled (v > 0);
            }));
        s.faders.push_back (makeFader (
            "mix", 0.0, [] (double v) { return juce::String ((int)v) + "%"; },
            [this] (double v) { engine_.setDelayMix ((float)(v / 100.0)); }));
        s.summary = [this] {
            auto& f = stages_[4].faders;
            return f[0].slider->getValue () == 0
                       ? juce::String ("off")
                       : juce::String ((int)std::lround (80 + f[0].slider->getValue () * 6)) +
                             "ms " + kDot + " " + juce::String ((int)f[1].slider->getValue ()) +
                             "%";
        };
        stages_.push_back (std::move (s));
    }

    // --- REVERB ---
    {
        Stage s;
        s.key = "REVERB";
        s.faders.push_back (makeFader (
            "size", 25.0,
            [] (double v) {
                return v < 33   ? juce::String ("room")
                       : v < 66 ? juce::String ("hall")
                                : juce::String ("plate");
            },
            [this] (double v) { engine_.setReverbRoomSize ((float)(v / 100.0)); }));
        s.faders.push_back (makeFader (
            "mix", 20.0, [] (double v) { return juce::String ((int)v) + "%"; },
            [this] (double v) {
                engine_.setReverbMix ((float)(v / 100.0));
                engine_.setReverbEnabled (v > 0);
            }));
        s.summary = [this] {
            auto& f = stages_[5].faders;
            const double sz = f[0].slider->getValue ();
            return juce::String (sz < 33   ? "room"
                                 : sz < 66 ? "hall"
                                           : "plate") +
                   " " + kDot + " " + juce::String ((int)f[1].slider->getValue ()) + "%";
        };
        stages_.push_back (std::move (s));
    }

    // Push initial state to the engine so the sound matches the UI.
    engine_.setGateEnabled (true);
    engine_.setEqEnabled (true);
    for (auto& s : stages_)
        for (auto& f : s.faders)
            if (f.slider->onValueChange) f.slider->onValueChange ();
    changed_ = 0;
}

EditScreen::~EditScreen () = default;

void EditScreen::openOnly (int idx) {
    for (int i = 0; i < (int)stages_.size (); ++i)
        stages_[(size_t)i].open = (i == idx) ? !stages_[(size_t)i].open : false;
    relayout ();
    repaint ();
}

void EditScreen::resized () { relayout (); }

void EditScreen::relayout () {
    auto r = getLocalBounds ();
    header_ = r.removeFromTop (78);
    footer_ = r.removeFromBottom (64);
    list_ = r.reduced (16, 4);
    doneRect_ = { header_.getRight () - 20 - 84, header_.getCentreY () - 4, 84, 34 };
    resetRect_ = { footer_.getRight () - 20 - 64, footer_.getCentreY () - 16, 64, 32 };

    int y = list_.getY ();
    for (auto& s : stages_) {
        s.headerRect = { list_.getX (), y, list_.getWidth (), headerHeight () };
        y += headerHeight ();
        if (s.open) {
            int fy = y + 6;
            for (auto& f : s.faders) {
                f.slider->setVisible (true);
                f.slider->setBounds (list_.getX () + 76, fy, list_.getWidth () - 76 - 44 - 12, 24);
                fy += 30;
            }
            if (s.lockable) {
                lockRect_ = { list_.getRight () - 12 - 108, fy + 4, 108, 32 };
                fy += 44;
            }
            y = fy + 8;
        } else {
            for (auto& f : s.faders) f.slider->setVisible (false);
        }
        y += 8;
    }
}

void EditScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds (), false);
    auto text = [&] (const juce::String& s, juce::Font f, juce::Colour c, juce::Rectangle<int> rr,
                     juce::Justification j) {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, rr, j, false);
    };

    // Header: EDITING / tone name + DONE
    auto h = header_.reduced (20, 0);
    text ("EDITING", uiFontTracked (11.0f, false), col::inkA (0.45f),
          h.removeFromTop (28).withTrimmedTop (14), juce::Justification::bottomLeft);
    text ("Plexi '68 Crunch", displayFont (26.0f), col::ink, h.removeFromTop (34),
          juce::Justification::topLeft);
    drawPill (g, doneRect_.toFloat (), col::accentA (0.0f), col::accentA (0.5f));
    text ("DONE", uiFontTracked (12.0f, true), col::accentAlt, doneRect_,
          juce::Justification::centred);

    // Stage boxes
    for (auto& s : stages_) {
        const bool isCab = s.lockable;
        juce::Rectangle<int> box = s.headerRect;
        int boxH = s.headerRect.getHeight ();
        if (s.open) {
            boxH += (int)s.faders.size () * 30 + 12 + (isCab ? 44 : 0);
            box = box.withHeight (boxH);
        }
        const auto border = isCab    ? col::accentA (0.4f)
                            : s.open ? col::inkA (0.28f)
                                     : col::inkA (0.1f);
        g.setColour (col::inkA (0.03f));
        g.fillRoundedRectangle (box.toFloat (), 14.0f);
        g.setColour (border);
        g.drawRoundedRectangle (box.toFloat ().reduced (0.5f), 14.0f, 1.0f);

        auto hr = s.headerRect.reduced (16, 0);
        const juce::String label = (isCab && cabLocked_) ? "CAB " + kLock : s.key;
        text (label, uiFontTracked (13.0f, true), isCab ? col::accentAlt : col::ink,
              hr.removeFromLeft (84), juce::Justification::centredLeft);
        text (s.open ? kChevD : kChevR, uiFont (12.0f, false), col::inkA (0.4f),
              hr.removeFromRight (16), juce::Justification::centredRight);
        if (!s.open)
            text (s.summary (), uiFont (13.0f, false), col::inkA (0.5f), hr,
                  juce::Justification::centredLeft);

        if (s.open) {
            for (auto& f : s.faders) {
                auto sb = f.slider->getBounds ();
                text (f.name, uiFont (12.0f, false), col::inkA (0.6f),
                      { list_.getX () + 12, sb.getY (), 64, sb.getHeight () },
                      juce::Justification::centredLeft);
                text (f.fmt (f.slider->getValue ()), uiFont (12.0f, true), col::accentAlt,
                      { sb.getRight () + 8, sb.getY (), 40, sb.getHeight () },
                      juce::Justification::centredRight);
            }
            if (isCab) {
                text ("Greenback 4" + kMult + "12 " + kDot + " IR", uiFont (13.0f, false), col::ink,
                      { list_.getX () + 16, lockRect_.getY (), list_.getWidth () - 140, 32 },
                      juce::Justification::centredLeft);
                drawPill (g, lockRect_.toFloat (),
                          cabLocked_ ? col::accentA (0.12f) : col::accentA (0.0f),
                          col::accentA (0.5f));
                text (cabLocked_ ? kLock + " LOCKED" : juce::String ("UNLOCKED"),
                      uiFontTracked (11.0f, true), col::accentAlt, lockRect_,
                      juce::Justification::centred);
            }
        }
    }

    // Footer
    g.setColour (col::inkA (0.08f));
    g.fillRect (footer_.getX (), footer_.getY (), footer_.getWidth (), 1);
    text ("smart defaults set " + kDot + " you changed " + juce::String (changed_) +
              (changed_ == 1 ? " fader" : " faders"),
          uiFont (12.0f, false), col::inkA (0.4f),
          footer_.withTrimmedLeft (20).withWidth (footer_.getWidth () - 100),
          juce::Justification::centredLeft);
    text ("RESET", uiFontTracked (12.0f, false), col::inkA (0.55f), resetRect_,
          juce::Justification::centred);
}

void EditScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    if (doneRect_.contains (p)) {
        if (onDone) onDone ();
        return;
    }
    if (resetRect_.contains (p)) {
        // Reset every fader to its construction default by re-running defaults.
        static const double defs[][3] = { { 28 },         { 70, 50 }, { 20, 75 },
                                          { 50, 44, 56 }, { 0, 0 },   { 25, 20 } };
        for (int i = 0; i < (int)stages_.size (); ++i)
            for (int j = 0; j < (int)stages_[(size_t)i].faders.size (); ++j)
                stages_[(size_t)i].faders[(size_t)j].slider->setValue (defs[i][j],
                                                                       juce::sendNotification);
        changed_ = 0;
        repaint ();
        return;
    }
    for (auto& s : stages_)
        if (s.open && s.lockable && lockRect_.contains (p)) {
            cabLocked_ = !cabLocked_;
            repaint ();
            return;
        }
    for (int i = 0; i < (int)stages_.size (); ++i)
        if (stages_[(size_t)i].headerRect.contains (p)) {
            openOnly (i);
            return;
        }
}
