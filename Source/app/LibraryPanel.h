#pragma once
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/LibraryEntry.h"
#include "model/LibraryStore.h"

// A juce::Component listing the library (models + IRs) with favorites shown
// first, click-to-load, and click-on-the-star-to-toggle-favorite. Holds a
// reference to a nam::LibraryStore injected by the owner; the panel never
// owns or outlives the store. All rows are LibraryEntry COPIES pulled fresh
// from the store on refresh() -- nothing here caches a pointer into the
// store across a mutation.
class LibraryPanel : public juce::Component {
public:
    explicit LibraryPanel(nam::LibraryStore& store);
    ~LibraryPanel() override;

    void resized() override;

    // Re-query the store and refresh both lists' contents.
    void refresh();

    // Fired when the user clicks/double-clicks a row (not the star).
    std::function<void(const nam::LibraryEntry&)> onLoadEntry;

private:
    // Shared ListBoxModel for one library type (Model or Ir). Reads entries
    // from the owning LibraryStore fresh on every setEntries() call; the UI
    // never holds a raw LibraryEntry* from the store.
    class EntryListModel : public juce::ListBoxModel {
    public:
        EntryListModel(nam::LibraryStore& store, nam::LibraryType type, LibraryPanel& owner);

        void setEntries(std::vector<nam::LibraryEntry> entries);

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;

    private:
        static constexpr int kStarWidth = 20;

        nam::LibraryStore& store_;
        nam::LibraryType type_;
        LibraryPanel& owner_;
        std::vector<nam::LibraryEntry> entries_;
    };

    static std::vector<nam::LibraryEntry>
    sortedFavoritesFirst(std::vector<nam::LibraryEntry> entries);

    nam::LibraryStore& store_;

    juce::Label modelsLabel_{ {}, "Models" };
    EntryListModel modelsModel_;
    juce::ListBox modelsList_;

    juce::Label irsLabel_{ {}, "Impulse Responses" };
    EntryListModel irsModel_;
    juce::ListBox irsList_;
};
