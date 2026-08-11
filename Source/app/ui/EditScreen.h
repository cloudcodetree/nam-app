#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "dsp/ToneEngine.h"

// The "Edit" screen: the accordion signal chain (GATE·AMP·CAB·EQ·DELAY·REVERB),
// one stage open at a time, FADERS (not knobs) wired to the live ToneEngine.
// Cross-platform JUCE — depends only on the shared dsp::ToneEngine.
class EditScreen : public juce::Component {
public:
    explicit EditScreen (dsp::ToneEngine& engine);
    ~EditScreen() override;

    std::function<void()> onDone;   // DONE -> back to Play

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Fader {
        juce::String name;
        std::unique_ptr<juce::Slider> slider;
        std::function<juce::String (double)> fmt;   // value -> display string
    };
    struct Stage {
        juce::String key;                 // GATE / AMP / CAB / EQ / DELAY / REVERB
        bool open = false;
        bool lockable = false;            // CAB
        std::vector<Fader> faders;
        std::function<juce::String()> summary;   // collapsed summary text
        juce::Rectangle<int> headerRect;  // hit region
    };

    dsp::ToneEngine& engine_;
    std::vector<Stage> stages_;
    bool cabLocked_ = true;
    int  changed_ = 0;

    juce::Rectangle<int> header_, doneRect_, list_, footer_, resetRect_, lockRect_;

    void openOnly (int idx);
    void relayout();
    int  headerHeight() const { return 56; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditScreen)
};
