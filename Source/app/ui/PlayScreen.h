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
    void setKept (bool kept);                 // heart state of the current card
    void setSaved (bool saved);               // on-device state (download button)
    void setDemoPlaying (bool on);            // card-back demo transport state
    // Pairing dropdown on the card back — complements the card's gear type
    // (amp cards pair a cab; cab cards pair an amp; etc).
    void setPairChoices (juce::String label, juce::StringArray names, int sel);
    void setDeckView (int v);                 // 0 favorites · 1 browse (reflect)

    // Deck contents for the non-card layouts (detail list / grids). The
    // owner pushes the whole visible deck; `active` highlights the tone the
    // engine is running. Layout choice itself lives in the strip's view menu.
    struct DeckItem {
        juce::String title, sub;
        juce::Image  thumb;
        bool kept = false;
    };
    void setDeckItems (std::vector<DeckItem> items);
    void setActiveDeckIndex (int index);
    std::function<void (int)> onSelectAbsolute;   // list/grid tap -> deck index
    // Cab/IR cards: no amp sliders, no PAIR row (a cab can't pair a cab).
    void setCabCard (bool isCab);

    // The tuner panel rect (local coords) — the expanding tuner overlay
    // grows upward from here and leaves it exposed as the collapse handle.
    juce::Rectangle<int> tunerPanelBounds() const { return tunerRect_; }

    std::function<void()>     onLibrary;
    std::function<void()>     onPrev, onNext;   // step through the collection
    std::function<void (int)> onSelectIndex;    // dots pagination jump
    std::function<void (int)> onPageDelta;      // dots-row ‹ › page arrows (±1)
    void setPageNav (bool canPrev, bool canNext);   // show/hide the arrows
    std::function<void()>     onSettings;       // top-bar gear -> audio settings
    std::function<void()>     onEdit, onLive;   // top-bar shortcuts
    std::function<void()>     onTuner;          // tuner panel -> strobe tuner
    std::function<void (int)> onViewChange;     // 0 favorites · 1 downloaded · 2 browse
    std::function<void()>     onKeepToggle;     // heart tap / swipe-down keep
    std::function<void()>     onSaveToggle;     // download button (save / remove)
    // Gear dropdown (browse strip): owner supplies choices (index 0 = all);
    // selection index is reported back.
    void setGearChoices (juce::StringArray names);
    std::function<void (int)> onGearSelect;

    // Generic filter groups (owner defines them per view; radio groups keep
    // exactly one selection). The flyout renders them with separators.
    struct FilterGroup {
        juce::String title;
        juce::StringArray options;
        juce::StringArray selected;
        bool radio = false;
    };
    void setFilterGroups (std::vector<FilterGroup> groups);
    std::function<void (const std::vector<FilterGroup>&)> onFilterGroupsChanged;
    std::function<void (int)> onSelectPair;     // PAIR pick (into setPairChoices)
    std::function<void (int)> onSelectDemoTrack;
    std::function<void()>     onToggleDemo;     // play/stop demo of current card
    // Card-back slider moved: (param index, normalised 0..1). The owner maps
    // to engine ranges (mirrors the EDIT screen mappings).
    std::function<void (int, float)> onToneParam;
    static constexpr int kNumToneParams = 5;    // GAIN BASS MID TREBLE GATE

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void toggleFlip();
    void applyParamFromX (int idx, int x);
    void notifyFilters();

    juce::String name_ { "Bundled Tone" }, family_ { "NAM PLAYER" }, author_;
    juce::Image  art_;
    juce::Image  artBlur_;           // tiny copy, upscaled as the blurred fill
    bool  kept_ = false;
    bool  saved_ = false;
    bool  demoPlaying_ = false;
    bool  cabCard_ = false;          // current card is a cab/IR
    int   view_ = 0;                 // 0 favorites · 1 downloaded · 2 browse
    juce::String pairLabel_ { "PAIR CAB" };
    juce::StringArray pairNames_;
    int   pairSel_ = 0;
    int   demoSel_ = 0;
    float burst_ = 0.0f;             // heart-pop overlay (1 -> 0)
    bool  flyOpen_ = false;          // filters flyout
    juce::StringArray gearNames_;    // dropdown choices (0 = all)
    int   gearSel_ = 0;
    std::vector<FilterGroup> filterGroups_;
    int  activeFilterCount() const;
    // Card flip: 0 = artwork front, 1 = settings back.
    bool  flipped_ = false;
    float flip_ = 0.0f;
    int   dragParam_ = -1;
    struct ToneParam { const char* label; float v; };
    std::array<ToneParam, (size_t) kNumToneParams> params_ { {
        { "GAIN", 0.5f }, { "BASS", 0.5f }, { "MID", 0.5f }, { "TREBLE", 0.5f },
        { "GATE", 0.2f } } };
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
    bool pagePrev_ = false, pageNext_ = false;   // dots-row page arrows
    juce::Rectangle<int> pagePrevRect_, pageNextRect_;
    // Deck layout: 0 swipe cards · 1 detail list · 2 two-col grid · 3 four-col.
    int   layoutMode_ = 0;
    std::vector<DeckItem> deckItems_;
    int   activeIdx_ = -1;
    juce::Rectangle<int> viewBtnRect_;           // strip: layout picker button
    float deckScroll_ = 0.0f, deckPressScroll_ = 0.0f;
    bool  deckPressed_ = false, deckMoved_ = false;
    juce::Rectangle<int> deckItemRect (int i) const;   // list/grid geometry
    int   deckContentHeight() const;
    void  paintDeckPanel (juce::Graphics& g);
    juce::Rectangle<int> libRect_, prevRect_, nextRect_, dotsRect_, tunerRect_,
                         gearRect_, editTopRect_, liveTopRect_,
                         backBtnRect_,   // card-back return button
                         cardEqRect_,    // card-front flip affordance (mixer icon)
                         heartRect_,     // card-footer keep toggle
                         saveRect_,      // card-footer download/save toggle
                         viewRow_, favViewRect_, savedViewRect_, browseViewRect_,
                         viewTitleRect_,   // current-view title (strip, left)
                         filterBtnRect_,
                         flyRect_,       // filters flyout panel
                         pairRowRect_, demoRowRect_, demoPlayRect_;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> flyChips_;
    std::vector<int> flyChipGroup_;                                          // chip -> group index
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> flyLabels_;   // group headers
    // Flyout scrolling (content can exceed the panel height).
    float flyScroll_ = 0.0f, flyPressScroll_ = 0.0f;
    int   flyContentH_ = 0;
    bool  flyPressed_ = false, flyMoved_ = false;
    juce::Point<int> flyPressPos_;

    // In-screen dropdown overlay (gear / pair-cab / demo-track pickers) —
    // themed like the filters flyout, anchored to its field, scrollable.
    enum class Menu { None, Gear, Pair, Demo, ViewType };
    Menu  menu_ = Menu::None;
    juce::Rectangle<int> menuRect_;
    juce::StringArray menuOptions_;
    int   menuSelected_ = 0;
    float menuScroll_ = 0.0f, menuPressScroll_ = 0.0f;
    int   menuContentH_ = 0;
    bool  menuPressed_ = false, menuMoved_ = false;
    juce::Point<int> menuPressPos_;
    void openMenu (Menu which, juce::Rectangle<int> anchor,
                   juce::StringArray options, int selected);
    void closeMenu();
    juce::Rectangle<int> gearDdRect_;   // gear-type dropdown (browse strip)
    std::array<juce::Rectangle<int>, 25> dotRects_;   // pagination hits (one page)

    void layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayScreen)
};
