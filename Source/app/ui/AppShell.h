#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "dsp/ToneEngine.h"
#include "app/ui/PlayScreen.h"
#include "app/ui/EditScreen.h"
#include "app/ui/PlaceholderScreen.h"

// Cross-platform app shell: owns every screen and swaps the visible one on
// navigation. Holds the shared dsp::ToneEngine so screens (Edit) can drive it.
// The Android/desktop/iOS shells each just host one AppShell.
class AppShell : public juce::Component {
public:
    explicit AppShell (dsp::ToneEngine& engine);

    void setLevels (float in, float out);   // forwarded to the Play meters
    void resized() override;

private:
    enum class Screen { Play, Edit, Library, Radio, Live };
    void show (Screen s);

    dsp::ToneEngine& engine_;
    std::unique_ptr<PlayScreen>        play_;
    std::unique_ptr<EditScreen>        edit_;
    std::unique_ptr<PlaceholderScreen> library_, radio_, live_;
    juce::Component* current_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
