#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "net/Tone3000Api.h"

// The "Browse" screen ("Explore Profiles & IR's", Hi-Fi design): the
// TONE3000 catalog as expandable pack rows. Search field + filter chips +
// sort cycle; each pack expands to its model variants, a demo-riff picker,
// and a KEEP action. ▶ auditions (host renders the demo riff through the
// model). Pure presentation — data and actions are injected.
class BrowseScreen : public juce::Component, private juce::Timer {
public:
    BrowseScreen();
    ~BrowseScreen() override;

    std::function<void()> onBack;
    std::function<void (juce::String)> onQuery;           // run a search
    std::function<void (int)>  onExpand;                  // fetch models for row
    std::function<void (int)>  onPlayPack;                // audition (auto variant)
    std::function<void (int, int)> onPlayModel;           // audition one variant
    std::function<void (int)>  onKeep;                    // download + import
    std::function<void (int)>  onDemoTrack;               // 0 chords 1 lead 2 chugs

    void setResults (std::vector<nam::ToneInfo> tones);
    void setModels (int packIdx, juce::StringArray names);
    void setPlaying (int packIdx, int modelIdx);          // -1,-1 = stopped
    void setKept (int packIdx);
    void setStatus (juce::String s);

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
    void runSearch();
    juce::String composedQuery() const;

    std::vector<nam::ToneInfo> tones_;
    std::vector<juce::StringArray> models_;   // per row (empty until loaded)
    std::vector<bool> kept_;
    int expanded_ = -1;
    int playingPack_ = -1, playingModel_ = -1;
    int demoTrack_ = 0;
    int sort_ = 0;                            // 0 trending · 1 most kept
    bool filtersOpen_ = false;
    juce::StringArray selectedTags_;          // active filter chips
    juce::String status_ { "Tuning in to TONE3000" };
    int animTicks_ = 0;

    juce::TextEditor search_;

    // Fixed chrome rects.
    juce::Rectangle<int> backRect_, badgeRect_, searchBox_, filtersBtn_,
                         sortBtn_, countRect_, filterPanel_, statusRect_;
    struct Chip { juce::String label; juce::Rectangle<int> rect; };
    std::vector<Chip> filterChips_;
    // Content-space rects (scrolling list).
    struct Row {
        juce::Rectangle<int> frame, header, playBtn, badge;
        std::vector<juce::Rectangle<int>> modelRects;
        std::vector<juce::Rectangle<int>> trackChips;
        juce::Rectangle<int> keepBtn;
    };
    std::vector<Row> rows_;
    juce::Rectangle<int> listArea_;
    int contentH_ = 0;
    float scrollY_ = 0.0f;
    int pressY_ = 0; float pressScroll_ = 0.0f; bool dragged_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowseScreen)
};
