#include "app/ui/AppShell.h"

AppShell::AppShell (dsp::ToneEngine& engine) : engine_ (engine) {
    play_    = std::make_unique<PlayScreen>();
    edit_    = std::make_unique<EditScreen> (engine_);
    library_ = std::make_unique<PlaceholderScreen> ("Library");
    radio_   = std::make_unique<PlaceholderScreen> ("Radio");
    live_    = std::make_unique<PlaceholderScreen> ("Live");

    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) library_.get(), (juce::Component*) radio_.get(),
                                (juce::Component*) live_.get() })
        addChildComponent (*c);

    play_->onNav = [this] (int tab) {
        switch (tab) { case 1: show (Screen::Edit);  break;
                       case 2: show (Screen::Radio); break;
                       case 3: show (Screen::Live);  break;
                       default: show (Screen::Play); break; }
    };
    play_->onLibrary = [this] { show (Screen::Library); };
    edit_->onDone    = [this] { show (Screen::Play); };
    library_->onBack = [this] { show (Screen::Play); };
    radio_->onBack   = [this] { show (Screen::Play); };
    live_->onBack    = [this] { show (Screen::Play); };

    show (Screen::Play);
}

void AppShell::show (Screen s) {
    juce::Component* target = play_.get();
    switch (s) {
        case Screen::Edit:    target = edit_.get();    break;
        case Screen::Library: target = library_.get(); break;
        case Screen::Radio:   target = radio_.get();   break;
        case Screen::Live:    target = live_.get();    break;
        case Screen::Play:    default: target = play_.get(); break;
    }
    if (current_ == target) return;
    if (current_ != nullptr) current_->setVisible (false);
    current_ = target;
    current_->setBounds (getLocalBounds());
    current_->setVisible (true);
    current_->toFront (false);
}

void AppShell::setLevels (float in, float out) {
    if (play_ != nullptr) play_->setLevels (in, out);
}

void AppShell::resized() {
    auto b = getLocalBounds();
    for (juce::Component* c : { (juce::Component*) play_.get(), (juce::Component*) edit_.get(),
                                (juce::Component*) library_.get(), (juce::Component*) radio_.get(),
                                (juce::Component*) live_.get() })
        if (c != nullptr) c->setBounds (b);
}
