#pragma once
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

// DI test-track tray: slides DOWN from the top over whatever screen is up, so
// a DI loop can be played through the current tone from anywhere in the app.
//
// Three states, because a permanently-docked bar would give every screen a
// SECOND header row and CLAUDE.md allows only one:
//   Idle       - just a grab tab. No chrome taken from the host screen.
//   Collapsed  - a slim status strip, shown ONLY while audio is playing; at
//                that moment it IS the functional control for the sound.
//   Expanded   - the full picker, height-capped and scrolling.
//
// Overlay rules apply: the owner paints this LAST and it never covers the
// bottom nav.
class DiTrayPanel : public juce::Component {
public:
    DiTrayPanel ();

    struct Track {
        juce::String name;
        bool bundled = false;   // in the APK; others fetch once and cache
        bool isBass = false;
    };

    void setTracks (std::vector<Track>);
    // Selection + transport state pushed by the owner, never inferred here.
    void setCurrent (int index);
    void setPlaying (bool);
    void setBusy (bool);   // fetching a non-bundled track
    int currentIndex () const { return current_; }
    bool isPlaying () const { return playing_; }

    // Height this tray wants to occupy at the top of the host screen. 0 when
    // idle/expanded (expanded floats over content); non-zero while collapsed
    // so the owner can inset the screen rather than hide its back chevron.
    int contentInset () const;
    bool isExpanded () const { return expanded_; }
    void collapse ();
    // True if it consumed the back press (closed itself).
    bool handleBack ();

    std::function<void (int)> onSelect;       // play this index
    std::function<void (bool)> onPlayPause;   // desired playing state
    std::function<void (int)> onStep;         // -1 / +1 through the list
    std::function<void ()> onLayoutChanged;   // inset changed -> owner relayouts

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void paintIdle (juce::Graphics&);
    void paintCollapsed (juce::Graphics&);
    void paintExpanded (juce::Graphics&);
    void paintRow (juce::Graphics&, juce::Rectangle<int>, const Track&, bool active);
    void handleExpandedTap (juce::Point<int>);
    std::vector<int> visibleIndices () const;

    std::vector<Track> tracks_;
    int current_ = 0;
    bool playing_ = false, busy_ = false, expanded_ = false;
    int filter_ = 0;   // 0 all, 1 guitar, 2 bass

    juce::Rectangle<int> panel_, tabRect_, playRect_, prevRect_, nextRect_, loopRect_, handleRect_;
    juce::Rectangle<int> listArea_;
    std::vector<juce::Rectangle<int>> filterRects_;
    int rowsTop_ = 0, contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiTrayPanel)
};
