#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <array>

// The "Play" screen (resting state) from the Hi-Fi design: hero tone card,
// transport, tuner + live IN/OUT meters, and the PLAY/EDIT/RADIO/LIVE nav bar.
// Pure presentation — the owner feeds live meter levels and handles nav.
// Tapping the art card flips it to a per-tone settings face (quick sliders);
// tapping again flips back.
class PlayScreen : public juce::Component, private juce::Timer {
public:
    PlayScreen();

    // Live meter levels, 0..1 (owner's timer pushes these from engine telemetry).
    void setLevels (float in, float out);

    // The tone the engine is ACTUALLY running (owner keeps this current).
    void setNowPlaying (juce::String name, juce::String family, juce::String author);
    // Live tuner state (owner runs pitch detection on the raw input).
    void setTuner (juce::String note, float cents, bool active);
    void setPosition (int index, int count);   // place in the collection (-1 = not in it)
    // TONE3000 artwork for the current tone (invalid image = initial-letter art).
    void setArtwork (juce::Image art);

    // The tuner panel rect (local coords) — the expanding tuner overlay
    // grows upward from here and leaves it exposed as the collapse handle.
    juce::Rectangle<int> tunerPanelBounds() const { return tunerRect_; }

    std::function<void()>     onLibrary;
    std::function<void()>     onPrev, onNext;   // step through the collection
    std::function<void()>     onTuner;          // tuner panel -> strobe tuner
    // Card-back slider moved: (param index, normalised 0..1). The owner maps
    // to engine ranges (mirrors the EDIT screen mappings).
    std::function<void (int, float)> onToneParam;
    static constexpr int kNumToneParams = 7;    // GAIN BASS MID TREBLE GATE DLY VERB

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void toggleFlip();
    void applyParamFromX (int idx, int x);

    juce::String name_ { "Bundled Tone" }, family_ { "NAM PLAYER" }, author_;
    juce::Image  art_;
    // Card flip: 0 = artwork front, 1 = settings back.
    bool  flipped_ = false;
    float flip_ = 0.0f;
    int   dragParam_ = -1;
    struct ToneParam { const char* label; float v; };
    std::array<ToneParam, (size_t) kNumToneParams> params_ { {
        { "GAIN", 0.5f }, { "BASS", 0.5f }, { "MID", 0.5f }, { "TREBLE", 0.5f },
        { "GATE", 0.2f }, { "DELAY", 0.0f }, { "REVERB", 0.0f } } };
    std::array<juce::Rectangle<int>, (size_t) kNumToneParams> paramRows_;
    int   index_    = -1;
    int   count_    = 0;
    float inLevel_  = 0.0f;
    float outLevel_ = 0.0f;
    juce::String tunerNote_;
    float tunerCents_ = 0.0f;
    bool  tunerActive_ = false;
    juce::Point<int> pressPos_;

    // Hit / layout rects, computed in resized(). (Nav lives in AppShell.)
    juce::Rectangle<int> topBar_, hero_, artRect_, textRect_, transportRect_,
                         metersRow_;
    juce::Rectangle<int> libRect_, prevRect_, nextRect_, progressRect_, tunerRect_;

    void layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayScreen)
};
