#include <juce_gui_extra/juce_gui_extra.h>

// Minimal JUCE app for Android bring-up (Phase 5a, Task 3): proves the
// Gradle -> CMake -> APK -> launch path works before wiring real audio
// (Task 4 swaps in AndroidAudioApp). Deliberately uses no core/DSP code so a
// failure here is unambiguously a build/shell problem, not app logic.
class BringUpComponent : public juce::Component {
public:
    BringUpComponent() { setSize(400, 200); }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::limegreen);
        g.setFont(24.0f);
        g.drawText("NAM Player - Android bring-up OK",
                   getLocalBounds(), juce::Justification::centred);
    }
};

class BringUpWindow : public juce::DocumentWindow {
public:
    BringUpWindow()
        : DocumentWindow("NAM Player", juce::Colours::black, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new BringUpComponent(), true);
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
