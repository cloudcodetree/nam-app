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
        setContentOwned(new AndroidAudioApp(), true);
        setVisible(true);
        setFullScreen(true);
    }
    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class NamPlayerApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "NAM Player"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override       { window_.reset(new BringUpWindow()); }
    void shutdown() override                             { window_ = nullptr; }
private:
    std::unique_ptr<BringUpWindow> window_;
};

START_JUCE_APPLICATION(NamPlayerApplication)
