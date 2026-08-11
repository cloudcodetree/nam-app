#include <juce_gui_extra/juce_gui_extra.h>
#include "app/android/AndroidAudioApp.h"

// Android app entry (Phase 5a). Task 3 proved the Gradle -> CMake -> APK ->
// render path with a trivial component; Task 4 hosts the real AndroidAudioApp
// (guitar in -> ToneEngine -> out + status/latency/gain UI).
class BringUpWindow : public juce::DocumentWindow {
public:
    BringUpWindow()
        : DocumentWindow("NAM Player", juce::Colours::black, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        audioApp_ = new AndroidAudioApp();
        setContentOwned(audioApp_, true);   // window takes ownership
        setVisible(true);
        setFullScreen(true);   // fill the screen; JuceActivity insets our content
    }
    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
    AndroidAudioApp* audioApp() const { return audioApp_; }
private:
    AndroidAudioApp* audioApp_ = nullptr;   // owned by the DocumentWindow content
};

class NamPlayerApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "NAM Player"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override       { window_.reset(new BringUpWindow()); }
    void shutdown() override                             { window_ = nullptr; }

    // Android hardware/gesture back: navigate our own screen stack (pop to
    // Play) instead of finishing the activity. Returning true tells JUCE the
    // press was handled so the app stays open; on Play we return false so the
    // OS default (leave the app) applies.
    bool backButtonPressed() override {
        if (window_ != nullptr && window_->audioApp() != nullptr)
            return window_->audioApp()->handleBackButton();
        return false;
    }
private:
    std::unique_ptr<BringUpWindow> window_;
};

START_JUCE_APPLICATION(NamPlayerApplication)
