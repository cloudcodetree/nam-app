#include "app/ui/StackWidgets.h"
#include "app/ui/NamLookAndFeel.h"

namespace nam::ui {

namespace {

// Self-deleting toast body: a Timer that fires once and deletes its own
// component. No other code owns this pointer.
class ToastView : public juce::Component, private juce::Timer {
public:
    explicit ToastView (juce::String msg) : msg_ (std::move (msg)) {
        setInterceptsMouseClicks (false, false);
    }

    void start () { startTimer (2200); }

    void paint (juce::Graphics& g) override {
        const auto r = getLocalBounds ().toFloat ();
        drawPill (g, r, col::bg.withAlpha (0.96f), col::accentA (0.7f), 1.5f);
        g.setFont (uiFont (12.0f, true));
        g.setColour (col::ink);
        g.drawText (msg_, getLocalBounds ().reduced (16, 0), juce::Justification::centred, true);
    }

private:
    void timerCallback () override {
        stopTimer ();
        delete this;
    }

    juce::String msg_;
};

}   // namespace

void showToast (juce::Component& parent, juce::String msg) {
    auto* toast = new ToastView (std::move (msg));
    parent.addAndMakeVisible (toast);
    const int w = juce::jmin (320, juce::jmax (120, parent.getWidth () - 48));
    const int h = 40;
    // Bottom-anchored above the global nav bar (house overlay convention);
    // 96px clears the nav on every host that wires this in.
    toast->setBounds (parent.getWidth () / 2 - w / 2, parent.getHeight () - h - 96, w, h);
    toast->toFront (false);
    toast->start ();
}

namespace {
// One gear-type mark, drawn centred in `r` -- the placeholder's payload
// when no thumbnail is available yet. Kept distinct per type so an amp slot
// and a cab slot still read differently even both empty.
void drawGearGlyph (juce::Graphics& g, juce::Rectangle<float> r, nam::GearType type) {
    g.setColour (col::inkA (0.4f));
    switch (type) {
        case nam::GearType::Cab: {
            const float d = juce::jmin (r.getWidth (), r.getHeight ());
            const auto ring = r.withSizeKeepingCentre (d, d);
            g.drawEllipse (ring, 1.4f);
            g.drawEllipse (ring.reduced (d * 0.3f), 1.2f);
            g.fillEllipse (ring.withSizeKeepingCentre (d * 0.14f, d * 0.14f));
            break;
        }
        case nam::GearType::Amp: {
            // Three short "grille" slats.
            const float bw = r.getWidth () * 0.7f;
            const float x = r.getCentreX () - bw * 0.5f;
            for (int i = 0; i < 3; ++i) {
                const float y = r.getY () + r.getHeight () * (0.22f + (float)i * 0.28f);
                g.drawLine (x, y, x + bw, y, 1.4f);
            }
            break;
        }
        case nam::GearType::Pedal: {
            // Stomp-switch: a ring with a small raised cap.
            const float d = juce::jmin (r.getWidth (), r.getHeight ()) * 0.7f;
            const auto ring = r.withSizeKeepingCentre (d, d);
            g.drawEllipse (ring, 1.4f);
            g.fillRoundedRectangle (ring.getCentreX () - 3.0f, ring.getY () - 3.0f, 6.0f, 6.0f,
                                    1.5f);
            break;
        }
        case nam::GearType::Post:
        default: {
            // Knob: a ring with a single indicator line.
            const float d = juce::jmin (r.getWidth (), r.getHeight ()) * 0.72f;
            const auto knob = r.withSizeKeepingCentre (d, d);
            g.drawEllipse (knob, 1.4f);
            g.drawLine (knob.getCentreX (), knob.getCentreY (), knob.getCentreX (),
                        knob.getY () + 2.0f, 1.4f);
            break;
        }
    }
}
}   // namespace

void drawGearThumb (juce::Graphics& g, juce::Rectangle<int> r, const juce::Image& img,
                    nam::GearType type, float cornerRadius) {
    const auto rf = r.toFloat ();
    juce::Path clip;
    clip.addRoundedRectangle (rf, cornerRadius);

    if (img.isValid ()) {
        g.saveState ();
        g.reduceClipRegion (clip);
        g.drawImageWithin (img, r.getX (), r.getY (), r.getWidth (), r.getHeight (),
                           juce::RectanglePlacement::fillDestination |
                               juce::RectanglePlacement::centred);
        g.restoreState ();
        g.setColour (col::inkA (0.14f));
        g.drawRoundedRectangle (rf.reduced (0.5f), cornerRadius, 1.0f);
        return;
    }

    // Deliberate empty-slot placeholder -- never a bare hole or a
    // broken-image look (offline, a template rig's empty toneIds, or the
    // fetch just hasn't landed yet).
    g.setColour (col::inkA (0.05f));
    g.fillRoundedRectangle (rf, cornerRadius);
    g.setColour (col::inkA (0.14f));
    g.drawRoundedRectangle (rf.reduced (0.5f), cornerRadius, 1.0f);
    g.saveState ();
    g.reduceClipRegion (clip);
    const float inset = juce::jmin (rf.getWidth (), rf.getHeight ()) * 0.26f;
    drawGearGlyph (g, rf.reduced (inset), type);
    g.restoreState ();
}

}   // namespace nam::ui
