#include "app/LibraryPanel.h"

#include <algorithm>

namespace {
constexpr int kRowHeight = 24;
}

// ---- LibraryPanel::EntryListModel -----------------------------------------

LibraryPanel::EntryListModel::EntryListModel(nam::LibraryStore& store, nam::LibraryType type,
                                              LibraryPanel& owner)
    : store_(store), type_(type), owner_(owner) {}

void LibraryPanel::EntryListModel::setEntries(std::vector<nam::LibraryEntry> entries) {
    entries_ = std::move(entries);
}

int LibraryPanel::EntryListModel::getNumRows() { return (int) entries_.size(); }

void LibraryPanel::EntryListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                                                      int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= (int) entries_.size()) return;
    const auto& e = entries_[(size_t) rowNumber];

    g.fillAll(rowIsSelected ? juce::Colours::darkslateblue.withAlpha(0.5f)
                             : juce::Colours::transparentBlack);

    // Star (favorite toggle) in the left kStarWidth px.
    auto starArea = juce::Rectangle<int>(0, 0, kStarWidth, height);
    g.setColour(e.favorite ? juce::Colours::yellow : juce::Colours::grey);
    g.setFont((float) height * 0.7f);
    g.drawText(e.favorite ? juce::CharPointer_UTF8("\xE2\x98\x85")   // filled star
                           : juce::CharPointer_UTF8("\xE2\x98\x86"), // outline star
               starArea, juce::Justification::centred);

    // Name + small hint (arch for models, frames/sampleRate for IRs).
    juce::String hint;
    if (type_ == nam::LibraryType::Model) {
        if (! e.arch.empty()) hint = juce::String(e.arch);
    } else {
        if (e.frames > 0) hint = juce::String(e.frames) + " smp";
        if (e.sampleRate > 0) hint += (hint.isEmpty() ? juce::String() : juce::String(" @ "))
                                       + juce::String(e.sampleRate) + " Hz";
    }

    auto textArea = juce::Rectangle<int>(kStarWidth + 4, 0, width - kStarWidth - 8, height);
    g.setColour(juce::Colours::white);
    g.setFont((float) height * 0.55f);
    g.drawText(juce::String(e.displayName), textArea, juce::Justification::centredLeft);

    if (hint.isNotEmpty()) {
        g.setColour(juce::Colours::lightgrey.withAlpha(0.7f));
        g.setFont((float) height * 0.45f);
        g.drawText(hint, textArea, juce::Justification::centredRight);
    }
}

void LibraryPanel::EntryListModel::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= (int) entries_.size()) return;
    const auto entry = entries_[(size_t) row];  // copy: refresh() below may reallocate entries_
    if (e.x < kStarWidth) {
        store_.setFavorite(entry.id, ! entry.favorite);
        store_.save();
        owner_.refresh();
    } else if (owner_.onLoadEntry) {
        owner_.onLoadEntry(entry);
    }
}

void LibraryPanel::EntryListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) {
    // Single click on the row body already fires onLoadEntry above; this
    // handler just prevents JUCE's default double-click behavior from
    // double-firing anything unexpected (no-op beyond the single click).
    juce::ignoreUnused(row, e);
}

// ---- LibraryPanel -----------------------------------------------------

LibraryPanel::LibraryPanel(nam::LibraryStore& store)
    : store_(store),
      modelsModel_(store, nam::LibraryType::Model, *this),
      irsModel_(store, nam::LibraryType::Ir, *this) {
    addAndMakeVisible(modelsLabel_);
    modelsList_.setModel(&modelsModel_);
    modelsList_.setRowHeight(kRowHeight);
    modelsList_.setColour(juce::ListBox::backgroundColourId, juce::Colours::black);
    addAndMakeVisible(modelsList_);

    addAndMakeVisible(irsLabel_);
    irsList_.setModel(&irsModel_);
    irsList_.setRowHeight(kRowHeight);
    irsList_.setColour(juce::ListBox::backgroundColourId, juce::Colours::black);
    addAndMakeVisible(irsList_);
}

LibraryPanel::~LibraryPanel() {
    modelsList_.setModel(nullptr);
    irsList_.setModel(nullptr);
}

std::vector<nam::LibraryEntry> LibraryPanel::sortedFavoritesFirst(
    std::vector<nam::LibraryEntry> entries) {
    // Order: favorites first, then most-recently-used, then alphabetical.
    // Entries never used have lastUsedAt == 0 and naturally sort last within
    // their favorite group; displayName (case-insensitive) is the stable
    // tiebreaker for entries with equal/zero lastUsedAt.
    std::stable_sort(entries.begin(), entries.end(),
                      [](const nam::LibraryEntry& a, const nam::LibraryEntry& b) {
                          if (a.favorite != b.favorite) return a.favorite;
                          if (a.lastUsedAt != b.lastUsedAt) return a.lastUsedAt > b.lastUsedAt;
                          return juce::String(a.displayName).compareIgnoreCase(
                                     juce::String(b.displayName)) < 0;
                      });
    return entries;
}

void LibraryPanel::refresh() {
    modelsModel_.setEntries(sortedFavoritesFirst(store_.all(nam::LibraryType::Model)));
    irsModel_.setEntries(sortedFavoritesFirst(store_.all(nam::LibraryType::Ir)));
    modelsList_.updateContent();
    irsList_.updateContent();
    modelsList_.repaint();
    irsList_.repaint();
}

void LibraryPanel::resized() {
    auto r = getLocalBounds();
    const int half = r.getHeight() / 2;

    auto modelsArea = r.removeFromTop(half);
    modelsLabel_.setBounds(modelsArea.removeFromTop(20));
    modelsList_.setBounds(modelsArea);

    auto irsArea = r;
    irsLabel_.setBounds(irsArea.removeFromTop(20));
    irsList_.setBounds(irsArea);
}
