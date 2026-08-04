#pragma once
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "net/Tone3000Api.h"

// A juce::Component for searching TONE3000's tone catalog: a query box, a
// Search button, and a results ListBox. Results are ToneInfo COPIES pulled
// from setResults() -- nothing here caches a pointer into caller-owned data,
// mirroring LibraryPanel's discipline.
class SearchPanel : public juce::Component {
public:
    SearchPanel();
    ~SearchPanel() override;

    void resized() override;

    // Replaces the result list contents and refreshes the ListBox.
    void setResults(std::vector<nam::ToneInfo> results);

    // Sets the small status label (e.g. "Searching...", "12 results",
    // "Connect first", or an error message).
    void setStatus(juce::String status);

    // Fired when the user clicks/enters a query (Search button or the
    // TextEditor's return key).
    std::function<void(const juce::String& query)> onSearch;

    // Fired when the user clicks a result row. The row is copied to a stack
    // local before firing (see ResultListModel::listBoxItemClicked), so this
    // callback never observes a dangling reference across a later
    // setResults() call.
    std::function<void(const nam::ToneInfo&)> onPick;

private:
    // ListBoxModel backing the results list. Holds ToneInfo COPIES set via
    // setResults(); never a pointer/reference into caller-owned storage.
    class ResultListModel : public juce::ListBoxModel {
    public:
        explicit ResultListModel(SearchPanel& owner);

        void setResults(std::vector<nam::ToneInfo> results);

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                               bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

    private:
        SearchPanel& owner_;
        std::vector<nam::ToneInfo> results_;
    };

    void fireSearch();

    juce::Label       titleLabel_ { {}, "Search TONE3000" };
    juce::TextEditor   searchBox_;
    juce::TextButton   searchButton_ { "Search" };
    juce::Label        statusLabel_;
    ResultListModel     resultsModel_;
    juce::ListBox       resultsList_;
};
