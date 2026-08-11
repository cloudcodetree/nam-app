#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>

// STACKS: user-built rigs, one slot per TONE3000 gear type. Each slot is
// picked from a live TONE3000 list (fetched on demand — nothing has to be
// downloaded beforehand; applying a stack fetches what it needs).
// Presentation only: data and actions are injected by the owner.
class StacksScreen : public juce::Component {
public:
    StacksScreen ();

    static constexpr int kNumSlots = 6;
    struct SlotDef {
        const char* label;
        const char* gearApi;
    };
    static const std::array<SlotDef, kNumSlots>& slotDefs ();

    struct Stack {
        juce::String name;
        std::array<juce::String, kNumSlots> toneIds;   // "" = empty slot
        std::array<juce::String, kNumSlots> titles;
        std::array<juce::String, kNumSlots> formats;   // "nam" / "ir" per pick
    };

    void setStacks (std::vector<Stack> stacks, int selected);

    std::function<void ()> onCreate;
    std::function<void (int)> onDelete;   // stack index
    std::function<void (int)> onSelect;   // expand a stack
    std::function<void (int)> onApply;    // load into the engine
    // Fetch the live TONE3000 list for a slot's gear type. The callback
    // delivers parallel ids/titles.
    std::function<void (int /*slot*/, std::function<void (juce::StringArray, juce::StringArray,
                                                          juce::StringArray)>)>
        onFetchSlotOptions;   // ids/titles/formats
    std::function<void (int /*stack*/, int /*slot*/, juce::String /*toneId*/,
                        juce::String /*title*/, juce::String /*format*/)>
        onSlotPicked;

    void paint (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void layout ();
    void openPicker (int slot);

    std::vector<Stack> stacks_;
    int selected_ = -1;

    // Layout rects.
    juce::Rectangle<int> newBtnRect_;
    struct StackRow {
        juce::Rectangle<int> header, applyBtn, deleteBtn;
    };
    std::vector<StackRow> rows_;
    std::array<juce::Rectangle<int>, kNumSlots> slotRects_;
    juce::Rectangle<int> listArea_;
    int contentH_ = 0;
    float scrollY_ = 0.0f, pressScrollY_ = 0.0f;
    bool listPressed_ = false, listMoved_ = false;
    juce::Point<int> pressPos_;

    // Slot picker overlay (themed, height-capped, scrollable — house rule).
    int pickerSlot_ = -1;
    bool pickerLoading_ = false;
    juce::StringArray pickerIds_, pickerTitles_, pickerFormats_;
    juce::Rectangle<int> pickerRect_;
    float pickerScroll_ = 0.0f, pickerPressScroll_ = 0.0f;
    int pickerContentH_ = 0;
    bool pickerPressed_ = false, pickerMoved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StacksScreen)
};
