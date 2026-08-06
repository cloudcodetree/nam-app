#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "net/Tone3000Api.h"

// The "Browse" screen ("Explore Profiles & IR's", Hi-Fi design): the
// TONE3000 catalog as expandable pack rows.
//   ▶  auditions (dry DI through the model)         — left circle
//   ↓  downloads best quality locally, no autoplay  — second circle
//   ♥  KEEP marks a downloaded model into the Library (favorites)
// Expanded packs get two dropdowns: demo DI track and model variant.
// Pure presentation — data and actions are injected.
class BrowseScreen : public juce::Component, private juce::Timer {
public:
    BrowseScreen();
    ~BrowseScreen() override;

    std::function<void()> onBack;
    std::function<void (juce::String)> onQuery;
    std::function<void (int)>  onExpand;                  // fetch models for row
    std::function<void (int)>  onPlayPack;                // audition (auto variant)
    std::function<void (int, int)> onPlayModel;           // audition one variant
    std::function<void (int)>  onKeep;                    // favorite -> library
    std::function<void (int)>  onDemoTrack;               // DI track index
    std::function<void (int)>  onCab;                     // cab IR index (0 = none)

    void setResults (std::vector<nam::ToneInfo> tones);
    void setModels (int packIdx, juce::StringArray names);
    void setDefaultModel (int packIdx, int modelIdx, juce::String name);  // what "Auto" resolves to
    void setPlaying (int packIdx, int modelIdx);          // -1,-1 = stopped
    void setKept (int packIdx);
    void setStatus (juce::String s);
    void setLoading (int packIdx, float progress);        // -1 = not loading
    void setLoadingProgress (float progress);
    void setCachedFlags (std::vector<bool> cached);       // instant-audition packs
    void setDownloadedFlags (std::vector<bool> dl);       // on-device (card tint)

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

    // Overlay menu shared by the dropdowns.
    enum class Menu { None, DemoTrack, Variant, Cab };
    int menuCount() const;
    int menuRowH() const;
    juce::Rectangle<int> menuPanelRect() const;           // screen space
    std::vector<juce::Rectangle<int>> menuRowRects() const;

    std::vector<nam::ToneInfo> tones_;
    std::vector<juce::StringArray> models_;
    std::vector<bool> kept_, cached_, downloaded_;
    std::vector<int> selVariant_;             // -1 = auto (default/best)
    std::vector<int> defaultVariant_;         // index "Auto" resolves to (-1 unknown)
    std::vector<juce::String> defaultName_;
    int expanded_ = -1;
    int playingPack_ = -1, playingModel_ = -1;
    int loadingPack_ = -1;
    float loadProgress_ = 0.0f;
    int demoTrack_ = 0;
    int cab_ = 0;
    Menu menu_ = Menu::None;
    float menuScroll_ = 0.0f;
    bool menuDragging_ = false;
    int sort_ = 0;                            // 0 trending · 1 most kept
    bool filtersOpen_ = false;
    juce::StringArray selectedTags_;
    juce::String status_ { "Tuning in to TONE3000" };
    int animTicks_ = 0;

    juce::TextEditor search_;

    juce::Rectangle<int> backRect_, badgeRect_, searchBox_, filtersBtn_,
                         sortBtn_, countRect_, filterPanel_, statusRect_;
    struct Chip { juce::String label; juce::Rectangle<int> rect; };
    std::vector<Chip> filterChips_;
    struct Row {
        juce::Rectangle<int> frame, header, playBtn, badge;
        juce::Rectangle<int> diBtn, varBtn, cabBtn, keepBtn;
    };
    std::vector<Row> rows_;
    juce::Rectangle<int> listArea_;
    int contentH_ = 0;
    float scrollY_ = 0.0f;
    int pressY_ = 0; float pressScroll_ = 0.0f; bool dragged_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowseScreen)
};
