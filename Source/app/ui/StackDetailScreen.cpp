#include "app/ui/StackDetailScreen.h"
#include "app/ui/NamLookAndFeel.h"
#include "app/ui/StackWidgets.h"

using namespace nam::ui;

namespace {
const juce::String kBackGlyph = juce::String::fromUTF8 ("\xE2\x80\xB9");   // ‹
}   // namespace

StackDetailScreen::StackDetailScreen () {
    setOpaque (true);
    addAndMakeVisible (editView_);
    addChildComponent (picker_);
    addChildComponent (itemSheet_);
    wireChildren ();
}

void StackDetailScreen::wireChildren () {
    editView_.onAddGear = [this] (nam::GearType t) {
        if (onAddGear) onAddGear (t);
    };
    editView_.onRemoveStack = [this] {
        if (onRemoveStack) onRemoveStack ();
    };
    editView_.onChanged = [this] (nam::Stack st) {
        stack_ = st;
        if (onChanged) onChanged (idx_, st);
    };
    editView_.onOpenItem = [this] (juce::String uid) {
        const auto* it = findItem (uid);
        if (it != nullptr) itemSheet_.open (*it);
    };
}

const nam::ChainItem* StackDetailScreen::findItem (const juce::String& uid) const {
    for (const auto& it : stack_.chain)
        if (juce::String (it.uid) == uid) return &it;
    return nullptr;
}

bool StackDetailScreen::closeTopOverlay () {
    // Order matters: this is z-order, not declaration order. The picker
    // can be launched FROM the item sheet (AddChannel/Swap) without the
    // sheet closing itself first, so in the only reachable stacked
    // configuration the picker sits in front (both call toFront() on
    // open). Checking it first means BACK peels the frontmost overlay
    // instead of yanking the sheet out from under a picker still on top.
    if (picker_.isVisible ()) {
        picker_.close ();
        return true;
    }
    if (itemSheet_.isVisible ()) {
        itemSheet_.close ();
        return true;
    }
    return editView_.closeConfirm ();
}

void StackDetailScreen::setStack (const nam::Stack& stack, int idx) {
    stack_ = stack;
    idx_ = idx;
    editView_.setStack (stack_, idx_);
    if (itemSheet_.isVisible ()) {
        // Keep an open sheet in sync across a repush (e.g. right after a
        // bypass/channel edit) so its pills reflect the new state; if the
        // item itself was removed from under it, close instead of showing
        // stale data for a uid that no longer exists.
        const auto* it = findItem (itemSheet_.currentUid ());
        if (it != nullptr) itemSheet_.open (*it);
        else itemSheet_.close ();
    }
    repaint ();
}

void StackDetailScreen::setThumbs (std::map<std::string, juce::Image> thumbs) {
    editView_.setThumbs (std::move (thumbs));
}

void StackDetailScreen::layout () {
    // No brand header row: the functional row (back chevron / rig name) is
    // this screen's only chrome, per CLAUDE.md's one-chrome-grammar rule.
    auto b = getLocalBounds ().withTrimmedTop (12);

    auto headerRow = b.removeFromTop (52).reduced (20, 8);
    backRect_ = headerRow.removeFromLeft (28);
    nameRect_ = headerRow;

    bodyRect_ = b.reduced (20, 12);
    editView_.setBounds (bodyRect_);
    picker_.setBounds (getLocalBounds ());
    itemSheet_.setBounds (getLocalBounds ());
}

void StackDetailScreen::resized () { layout (); }

void StackDetailScreen::paint (juce::Graphics& g) {
    paintHeroBackground (g, getLocalBounds ());

    g.setFont (uiFont (20.0f, false));
    g.setColour (col::inkA (0.7f));
    g.drawText (kBackGlyph, backRect_, juce::Justification::centredLeft, false);

    g.setFont (displayFont (20.0f));
    g.setColour (col::ink);
    g.drawText (juce::String (stack_.name), nameRect_, juce::Justification::centredLeft, true);
}

void StackDetailScreen::mouseDown (const juce::MouseEvent& e) {
    const auto p = e.getPosition ();

    if (backRect_.expanded (8).contains (p)) {
        if (onBack) onBack ();
        return;
    }
}
