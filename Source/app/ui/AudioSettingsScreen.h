#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// Everything the Audio settings screen shows, supplied by the host app.
struct AudioSettingsState {
    juce::StringArray inputs, outputs;
    juce::String currentInput, currentOutput;
    juce::StringArray rates, buffers;        // chip labels, e.g. "48k" / "192"
    juce::String currentRate, currentBuffer;
    double latencyMs = 0.0;                  // round-trip estimate
    bool   running = false;
};

// The "Audio" settings screen (Hi-Fi design): DEVICE rows (input + output),
// ENGINE chips (sample rate / buffer + latency readout), LEVELS meters with
// a manual "re-run auto gain-stage" action. Replaces the old first-launch
// Setup screen — calibration is a button here, not a gate.
class AudioSettingsScreen : public juce::Component, private juce::Timer {
public:
    AudioSettingsScreen();
    ~AudioSettingsScreen() override;

    std::function<void()> onBack;
    std::function<void()> onRescan;
    std::function<void (juce::String)> onSelectInput;
    std::function<void (juce::String)> onSelectOutput;
    std::function<void (juce::String)> onSelectRate;
    std::function<void (juce::String)> onSelectBuffer;
    std::function<void (float)> onSetInputDb;   // auto gain-stage result

    void setState (AudioSettingsState s);
    void setLevels (float inPeak, float outPeak);   // owner's timer feeds these

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void relayout();
    void clampScroll();
    juce::Point<int> toContent (juce::Point<int> p) const;

    AudioSettingsState st_;
    float inPeak_ = 0.0f, outPeak_ = 0.0f;

    // Auto gain-stage (manual trigger): listen ~4 s, aim peaks at -12 dBFS.
    enum class Gain { Idle, Listening, Applied };
    Gain  gain_ = Gain::Idle;
    int   gainTicks_ = 0;
    float gainMax_ = 0.0f;
    float appliedDb_ = 0.0f;

    // Fixed header; content scrolls beneath it.
    juce::Rectangle<int> topBar_, backRect_, statusRect_;
    // Content-space rects.
    std::vector<juce::Rectangle<int>> inRects_, outRects_, rateRects_, bufRects_;
    std::vector<juce::Rectangle<int>> cards_;
    juce::Rectangle<int> inHeader_, outHeader_, rescanRect_, engineHeader_,
                         rateRow_, bufRow_, latencyRow_, levelsHeader_,
                         meterInRow_, meterOutRow_, gainBtn_;
    int contentTop_ = 0, contentH_ = 0;
    float scrollY_ = 0.0f;
    int  pressY_ = 0; float pressScroll_ = 0.0f; bool dragged_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsScreen)
};
