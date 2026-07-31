#include <juce_gui_extra/juce_gui_extra.h>
#include "app/MainComponent.h"

class NamPlayerApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "NAM Player"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
        window_ = std::make_unique<Window>();
    }
    void shutdown() override { window_ = nullptr; }
private:
    struct Window : juce::DocumentWindow {
        Window() : juce::DocumentWindow("NAM Player",
            juce::Colours::black, juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(560, 640);
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<Window> window_;
};

START_JUCE_APPLICATION(NamPlayerApp)
