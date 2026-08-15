#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "model/LibraryEntry.h"
#include "model/StackModel.h"
#include "app/ui/StackTemplates.h"

// Stack creation wizard: step-0 template gallery ("start from a rig" /
// "start empty") + a 4-step guided build (amp channels / pedals / cab /
// footswitch map), per the SDD notes doc's "Stack creation flow". Hosted as
// a screen-level child of StacksHomeScreen (like StackDetailScreen's
// picker_/itemSheet_), not a height-capped overlay -- it fills the whole
// screen while open. Presentation + local draft state only: nothing is
// persisted or applied to the engine until SAVE (or a template pick) fires
// onSave; the owner (AppShellStacks.cpp) does the actual append/persist/
// navigate/toast, same division of labour as every other Stacks screen.
class StackCreateWizard : public juce::Component {
public:
    StackCreateWizard ();

    // Resets all wizard state to a fresh session and opens on the gallery.
    // `existingStackCount` seeds the default name for the "start empty"
    // path ("Stack {n+1}") -- templates keep their own name instead.
    void open (int existingStackCount);
    void close ();
    bool isOpen () const { return isVisible (); }

    // Synchronous local-library reads (models for step 1/2, IRs for step
    // 3) -- see AppShell::getModels_/getIrs_. Not a live TONE3000 fetch;
    // the library carries no gear-type tags today, so steps 1 and 2 both
    // list every kept model (see StackCreateWizard.cpp's file header note).
    std::function<std::vector<nam::LibraryEntry> ()> onFetchModels;
    std::function<std::vector<nam::LibraryEntry> ()> onFetchIrs;
    // `toast` is true only for the step-4 guided save; a template pick
    // jumps straight to Detail EDIT with no toast, per the spec.
    std::function<void (nam::Stack, bool toast)> onSave;
    std::function<void ()> onCancel;   // informational -- close() already ran

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Step { Gallery, Amp, Pedals, Cab, Foot };
    static constexpr int kStepCount = 4;   // Amp..Foot; Gallery has no pill

    void layout ();
    void layoutContent ();   // dispatches to the current step's content rects
    void goToStep (Step s);

    void addAmpChannel (const nam::LibraryEntry&);
    void removeAmpChannel (int channelIdx);
    void togglePedal (const nam::LibraryEntry&);
    void pickCab (const nam::LibraryEntry&);

    struct ActionRow {
        juce::String uid;   // bound chain item uid; empty => Tap tempo
        juce::String label;
    };
    std::vector<ActionRow> buildActions () const;
    void autoMapIfNeeded ();
    void pruneStaleAssignments ();   // drops switch bindings to since-removed gear
    void armSwitch (int idx);
    void assignArmedTo (const ActionRow&);
    void clearArmed ();
    juce::String warningText () const;   // "" if nothing unmapped
    void syncFsIntoChain ();             // switches_ -> draft_.chain[*].fs

    void pickTemplate (int idx);
    void doSave ();

    void handleContentTap (juce::Point<int> contentLocal);
    void handleFooterTap (juce::Point<int> p);

    // Small draft_ lookups shared by layout/mutation (StackCreateWizard.cpp)
    // and painting (StackCreateWizardPaint.cpp) -- both are member functions
    // of this class, so both can see them despite Step being private.
    const nam::ChainItem* ampItem () const;
    const nam::ChainItem* cabItem () const;
    bool pedalIncluded (const std::string& toneId) const;
    static const char* stepTitle (Step);
    static const char* stepSubtitle (Step);
    static const char* nextStepLabel (Step);   // footer "NEXT: {label} ->"

    // Painting lives in StackCreateWizardPaint.cpp (no-god-files split,
    // same shape as StackEditView/StackEditViewPaint).
    void paintHeader (juce::Graphics&) const;
    void paintStepPills (juce::Graphics&) const;
    void paintContent (juce::Graphics&, int dy) const;
    void paintGallery (juce::Graphics&, int dy) const;
    void paintAmpStep (juce::Graphics&, int dy) const;
    void paintPedalsStep (juce::Graphics&, int dy) const;
    void paintCabStep (juce::Graphics&, int dy) const;
    void paintFootStep (juce::Graphics&, int dy) const;
    void paintFooter (juce::Graphics&) const;

    Step step_ = Step::Gallery;
    nam::Stack draft_;
    std::vector<nam::templates::Template> galleryTemplates_;
    std::vector<nam::LibraryEntry> models_, irs_;
    std::array<bool, kStepCount> stepVisited_{};
    bool autoMapped_ = false;

    struct SwitchAssign {
        bool tapTempo = false;
        juce::String uid;   // empty + !tapTempo => unassigned
    };
    std::array<SwitchAssign, 4> switches_;   // A..D
    int armedSwitch_ = -1;

    juce::Rectangle<int> headerRect_, backRect_, titleRect_, subtitleRect_;
    juce::Rectangle<int> pillsRowRect_;
    std::array<juce::Rectangle<int>, kStepCount> pillRects_;
    juce::Rectangle<int> contentArea_, footerRect_, footerBtnRect_;
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;

    // Gallery, content-local.
    std::vector<juce::Rectangle<int>> templateCardRects_;
    juce::Rectangle<int> emptyBtnRect_;

    // Step 1 (Amp), content-local.
    juce::Rectangle<int> channelsLabelRect_, libraryLabelRect_;
    struct ChannelRowRect {
        juce::Rectangle<int> body, remove;
        int channelIdx = 0;
    };
    std::vector<ChannelRowRect> channelRowRects_;
    std::vector<juce::Rectangle<int>> libraryRowRects_;   // index into models_

    // Step 2 (Pedals), content-local: 2-col grid, index into models_.
    std::vector<juce::Rectangle<int>> pedalCardRects_;

    // Step 3 (Cab), content-local: index into irs_.
    std::vector<juce::Rectangle<int>> cabRowRects_;

    // Step 4 (Foot), content-local.
    juce::Rectangle<int> chocolatePanelRect_;
    std::array<juce::Rectangle<int>, 4> switchRects_;
    juce::Rectangle<int> clearRowRect_;                  // "-- nothing --", clears the armed switch
    std::vector<juce::Rectangle<int>> actionRowRects_;   // index into buildActions()
    juce::Rectangle<int> warningRect_;

    bool pressedInContent_ = false;
    juce::Point<int> pressPos_;
    bool moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackCreateWizard)
};
