#include "app/ui/StackGearPicker.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

using namespace nam::ui;

namespace {
const juce::String kDotSep = juce::String::fromUTF8 ("\xC2\xB7");   // ·
const juce::String kCaption = "live from TONE3000 " + kDotSep + " downloads on add";

const char* tabLabel (nam::GearType t) {
    switch (t) {
        case nam::GearType::Amp: return "AMP";
        case nam::GearType::Cab: return "CAB";
        case nam::GearType::Post: return "POST";
        case nam::GearType::Pedal:
        default: return "PEDAL";
    }
}
}   // namespace

StackGearPicker::StackGearPicker () {
    setInterceptsMouseClicks (true, true);
    setVisible (false);
}

bool StackGearPicker::tabDisabled (nam::GearType t) const { return disabledTabs_[(size_t)t]; }

void StackGearPicker::open (nam::GearType initialTab, std::array<bool, 4> disabledTabs,
                            juce::String hint) {
    disabledTabs_ = disabledTabs;
    hint_ = std::move (hint);
    if (tabDisabled (initialTab)) {
        tab_ = initialTab;   // fallback: first enabled tab, if any
        for (int i = 0; i < 4; ++i)
            if (!disabledTabs_[(size_t)i]) {
                tab_ = (nam::GearType)i;
                break;
            }
    } else tab_ = initialTab;
    if (auto* parent = getParentComponent ()) setBounds (parent->getLocalBounds ());
    setVisible (true);
    toFront (false);
    layout ();
    fetchTab ();
}

void StackGearPicker::close () { setVisible (false); }

void StackGearPicker::setThumbs (std::map<std::string, juce::Image> thumbs) {
    thumbs_ = std::move (thumbs);
    repaint ();
}

juce::Image StackGearPicker::thumbFor (const nam::ToneInfo& t) const {
    if (t.id.empty ()) return {};
    auto it = thumbs_.find (t.id);
    return it != thumbs_.end () ? it->second : juce::Image ();
}

void StackGearPicker::selectTab (nam::GearType t) {
    if (tabDisabled (t) || t == tab_) return;
    tab_ = t;
    layout ();
    fetchTab ();
}

void StackGearPicker::fetchTab () {
    results_.clear ();
    loading_ = true;
    fetchError_ = false;
    scrollY_ = 0.0f;
    ++fetchGen_;
    const int gen = fetchGen_;
    const auto tabAtFetch = tab_;
    layout ();
    repaint ();

    if (!onFetch) {
        loading_ = false;
        fetchError_ = true;
        repaint ();
        return;
    }
    juce::Component::SafePointer<StackGearPicker> self (this);
    onFetch (tab_, [self, gen, tabAtFetch] (bool ok, std::vector<nam::ToneInfo> tones) {
        if (self == nullptr) return;   // picker (its owner screen) died first
        if (self->fetchGen_ != gen || self->tab_ != tabAtFetch) return;   // stale: tab switched
        self->loading_ = false;
        self->fetchError_ = !ok;
        self->results_ = ok ? std::move (tones) : std::vector<nam::ToneInfo> ();
        self->layout ();
        self->repaint ();
        // Past the staleness check above -- this reply IS what the picker
        // is showing right now, so a thumb push for it can't stomp a
        // newer tab's map.
        if (ok && self->onResults) self->onResults (self->results_);
    });
}

void StackGearPicker::layout () {
    auto full = getLocalBounds ();
    if (full.isEmpty ()) return;

    const int h = juce::jmin (560, full.getHeight () * 72 / 100);   // overlay rule: height-capped
    sheetRect_ = full.removeFromBottom (h);

    auto in = sheetRect_.reduced (18, 14);
    tabsRect_ = in.removeFromTop (36);
    in.removeFromTop (6);
    captionRect_ = in.removeFromTop (16);
    if (hint_.isNotEmpty ()) {
        hintRect_ = in.removeFromTop (16);
        in.removeFromTop (4);
    } else hintRect_ = {};
    listRect_ = in;

    const int tabW = tabsRect_.getWidth () / 4;
    for (int i = 0; i < 4; ++i)
        tabRects_[(size_t)i] = { tabsRect_.getX () + i * tabW, tabsRect_.getY (), tabW - 6,
                                 tabsRect_.getHeight () };

    rowRects_.clear ();
    constexpr int rowH = 52;
    int y = 0;
    for (size_t i = 0; i < results_.size (); ++i) {
        rowRects_.push_back ({ 0, y, listRect_.getWidth (), rowH });
        y += rowH + 4;
    }
    contentH_ = y;
    scrollY_ =
        juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listRect_.getHeight ()), scrollY_);
}

void StackGearPicker::resized () { layout (); }

void StackGearPicker::paint (juce::Graphics& g) {
    if (!isVisible ()) return;
    g.fillAll (col::scrim);   // scrim over the whole screen

    g.setColour (col::sheetBg);
    g.fillRoundedRectangle (sheetRect_.toFloat (), 16.0f);
    g.setColour (col::inkA (0.16f));
    g.drawRoundedRectangle (sheetRect_.toFloat ().reduced (0.5f), 16.0f, 1.0f);

    for (int i = 0; i < 4; ++i) {
        const auto t = (nam::GearType)i;
        const bool active = t == tab_;
        const bool disabled = tabDisabled (t);
        drawPill (g, tabRects_[(size_t)i].toFloat (),
                  active ? col::accent : juce::Colours::transparentBlack,
                  active ? col::accent : col::inkA (disabled ? 0.08f : 0.2f));
        g.setFont (uiFontTracked (9.0f, true));
        g.setColour (active ? col::inkOnAccent : col::inkA (disabled ? 0.22f : 0.6f));
        g.drawText (tabLabel (t), tabRects_[(size_t)i], juce::Justification::centred, false);
    }

    g.setFont (uiFont (10.0f, false));
    g.setColour (col::inkA (0.4f));
    g.drawText (kCaption, captionRect_, juce::Justification::centredLeft, false);
    if (!hintRect_.isEmpty ()) {
        g.setFont (uiFont (10.0f, false));
        g.setColour (col::accentAlt.withAlpha (0.75f));
        g.drawText (hint_, hintRect_, juce::Justification::centredLeft, false);
    }

    g.saveState ();
    g.reduceClipRegion (listRect_);
    if (loading_) {
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::inkA (0.45f));
        g.drawText ("loading live results" + kDotSep + kDotSep + kDotSep, listRect_,
                    juce::Justification::centred, false);
    } else if (fetchError_) {
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::inkA (0.5f));
        g.drawText ("couldn't reach TONE3000 " + kDotSep + " check connection",
                    listRect_.reduced (16, 0), juce::Justification::centred, true);
    } else if (results_.empty ()) {
        g.setFont (uiFont (12.0f, false));
        g.setColour (col::inkA (0.4f));
        g.drawText ("no results", listRect_, juce::Justification::centred, false);
    } else {
        const int dy = listRect_.getY () - (int)scrollY_;
        for (size_t i = 0; i < results_.size (); ++i) {
            auto row = rowRects_[i].translated (listRect_.getX (), dy);
            if (row.getBottom () < listRect_.getY () || row.getY () > listRect_.getBottom ())
                continue;
            g.setColour (col::inkA (0.03f));
            g.fillRoundedRectangle (row.toFloat (), 10.0f);
            auto rin = row.reduced (14, 8);
            auto tag = rin.removeFromRight (44);
            drawGearThumb (g, rin.removeFromLeft (rin.getHeight ()), thumbFor (results_[i]), tab_,
                           8.0f);
            rin.removeFromLeft (10);
            g.setFont (uiFont (13.0f, false));
            g.setColour (col::ink);
            g.drawText (juce::String (results_[i].title), rin, juce::Justification::centredLeft,
                        true);
            drawPill (g, tag.withSizeKeepingCentre (tag.getWidth (), 18).toFloat (),
                      juce::Colours::transparentBlack, col::inkA (0.2f));
            g.setFont (uiFontTracked (8.0f, true));
            g.setColour (col::inkA (0.5f));
            g.drawText (juce::String (results_[i].format).toUpperCase (), tag,
                        juce::Justification::centred, false);
        }
    }
    g.restoreState ();

    if (contentH_ > listRect_.getHeight ()) {
        const float frac = (float)listRect_.getHeight () / (float)contentH_;
        const float thumbH = juce::jmax (24.0f, listRect_.getHeight () * frac);
        const float travel = (float)listRect_.getHeight () - thumbH - 8.0f;
        const float pos = scrollY_ / (float)juce::jmax (1, contentH_ - listRect_.getHeight ());
        g.setColour (col::inkA (0.22f));
        g.fillRoundedRectangle ((float)listRect_.getRight () - 7.0f,
                                (float)listRect_.getY () + 4.0f + travel * pos, 3.0f, thumbH, 1.5f);
    }
}

void StackGearPicker::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    pressPos_ = p;
    moved_ = false;
    pressed_ = false;

    if (!sheetRect_.contains (p)) {
        close ();
        if (onDismiss) onDismiss ();
        return;
    }
    for (int i = 0; i < 4; ++i)
        if (tabRects_[(size_t)i].contains (p)) return;   // handled on mouseUp (tap, not drag)
    if (listRect_.contains (p)) {
        pressed_ = true;
        pressScrollY_ = scrollY_;
    }
}

void StackGearPicker::mouseDrag (const juce::MouseEvent& e) {
    if (!pressed_) return;
    const auto p = e.getPosition ();
    const int dy = p.y - pressPos_.y;
    if (std::abs (dy) > 8) moved_ = true;
    if (moved_) {
        scrollY_ = juce::jlimit (0.0f, (float)juce::jmax (0, contentH_ - listRect_.getHeight ()),
                                 pressScrollY_ - (float)dy);
        repaint (listRect_);
    }
}

void StackGearPicker::mouseUp (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();
    const bool tap = !moved_;
    pressed_ = false;
    if (!tap || !sheetRect_.contains (p)) return;

    for (int i = 0; i < 4; ++i)
        if (tabRects_[(size_t)i].contains (p)) {
            selectTab ((nam::GearType)i);
            return;
        }
    if (!listRect_.contains (p) || loading_ || fetchError_) return;
    const juce::Point<int> cp{ p.x - listRect_.getX (), p.y - listRect_.getY () + (int)scrollY_ };
    for (size_t i = 0; i < rowRects_.size (); ++i)
        if (rowRects_[i].contains (cp)) {
            const auto picked = results_[i];
            const auto pickedTab = tab_;
            close ();
            if (onPicked) onPicked (pickedTab, picked);
            return;
        }
}
