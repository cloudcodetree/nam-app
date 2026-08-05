#include "app/ui/PlaceholderScreen.h"
#include "app/ui/NamLookAndFeel.h"

using namespace nam::ui;

PlaceholderScreen::PlaceholderScreen (juce::String title) : title_ (std::move (title)) {
    setOpaque (true);
}

void PlaceholderScreen::resized() {
    backRect_ = { 12, 24, 44, 40 };
}

void PlaceholderScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds());

    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.5f));
    g.drawText (juce::String::fromUTF8 ("\xE2\x80\xB9"), backRect_, juce::Justification::centred);

    auto b = getLocalBounds();
    g.setFont (displayFont (34.0f));
    g.setColour (col::ink);
    g.drawText (title_, b.withTrimmedBottom (60), juce::Justification::centred);

    g.setFont (uiFontTracked (12.0f, true));
    g.setColour (col::accentAlt);
    g.drawText ("COMING SOON", b.translated (0, 44), juce::Justification::centred);
}

void PlaceholderScreen::mouseDown (const juce::MouseEvent& e) {
    if (backRect_.expanded (12).contains (e.getPosition())) { if (onBack) onBack(); }
    else if (onBack) onBack();
}
