#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <array>

// The "Play" screen (resting state) from the Hi-Fi design: hero tone card,
// transport, tuner + live IN/OUT meters, and the PLAY/EDIT/RADIO/LIVE nav bar.
// Pure presentation — the owner feeds live meter levels and handles nav.
class PlayScreen : public juce::Component {
public:
    PlayScreen();

    // Live meter levels, 0..1 (owner's timer pushes these from engine telemetry).
    void setLevels (float in, float out);

    // The tone the engine is ACTUALLY running (owner keeps this current).
    void setNowPlaying (juce::String name, juce::String family, juce::String author);
    // Live tuner state (owner runs pitch detection on the raw input).
    void setTuner (juce::String note, float cents, bool active);
    void setPosition (int index, int count);   // place in the collection (-1 = not in it)

    std::function<void()>     onLibrary;
    std::function<void()>     onSettings; // I/O pill -> audio device picker
    std::function<void()>     onPrev, onNext;   // step through the collection
    std::function<void()>     onTuner;          // tuner panel -> strobe tuner

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String name_ { "Bundled Tone" }, family_ { "NAM PLAYER" }, author_;
    int   index_    = -1;
    int   count_    = 0;
    float inLevel_  = 0.0f;
    float outLevel_ = 0.0f;
    juce::String tunerNote_;
    float tunerCents_ = 0.0f;
    bool  tunerActive_ = false;

    // Hit / layout rects, computed in resized(). (Nav lives in AppShell.)
    juce::Rectangle<int> topBar_, hero_, artRect_, textRect_, transportRect_,
                         metersRow_;
    juce::Rectangle<int> libRect_, ioRect_, prevRect_, nextRect_, progressRect_, tunerRect_;

    void layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayScreen)
};
