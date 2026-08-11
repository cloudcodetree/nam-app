#include "app/SearchPanel.h"

namespace {
constexpr int kRowHeight = 28;
}

// ---- SearchPanel::ResultListModel -----------------------------------------

SearchPanel::ResultListModel::ResultListModel(SearchPanel& owner) : owner_(owner) {}

void SearchPanel::ResultListModel::setResults(std::vector<nam::ToneInfo> results) {
    results_ = std::move(results);
}

int SearchPanel::ResultListModel::getNumRows() { return (int)results_.size(); }

void SearchPanel::ResultListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                                                    int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= (int)results_.size()) return;
    const auto& t = results_[(size_t)rowNumber];

    g.fillAll(rowIsSelected ? juce::Colours::darkslateblue.withAlpha(0.5f)
                            : juce::Colours::transparentBlack);

    // Small hint: gear, format, "A2:<n>" -- whatever's non-empty/non-zero.
    juce::StringArray parts;
    if (!t.gear.empty()) parts.add(juce::String(t.gear));
    if (!t.format.empty()) parts.add(juce::String(t.format));
    parts.add("A2:" + juce::String((juce::int64)t.a2Count));
    const juce::String hint = parts.joinIntoString("  ");

    auto textArea = juce::Rectangle<int>(4, 0, width - 8, height);
    g.setColour(juce::Colours::white);
    g.setFont((float)height * 0.5f);
    g.drawText(juce::String(t.title), textArea, juce::Justification::centredLeft);

    g.setColour(juce::Colours::lightgrey.withAlpha(0.7f));
    g.setFont((float)height * 0.4f);
    g.drawText(hint, textArea, juce::Justification::centredRight);
}

void SearchPanel::ResultListModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || row >= (int)results_.size()) return;
    const auto tone = results_[(size_t)row];   // copy: a later setResults() may reallocate results_
    if (owner_.onPick) owner_.onPick(tone);
}

// ---- SearchPanel --------------------------------------------------------

SearchPanel::SearchPanel() : resultsModel_(*this) {
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(searchBox_);
    searchBox_.setTextToShowWhenEmpty("Search TONE3000...", juce::Colours::grey);
    searchBox_.onReturnKey = [this] { fireSearch(); };

    addAndMakeVisible(searchButton_);
    searchButton_.onClick = [this] { fireSearch(); };

    addAndMakeVisible(statusLabel_);

    resultsList_.setModel(&resultsModel_);
    resultsList_.setRowHeight(kRowHeight);
    resultsList_.setColour(juce::ListBox::backgroundColourId, juce::Colours::black);
    addAndMakeVisible(resultsList_);
}

SearchPanel::~SearchPanel() { resultsList_.setModel(nullptr); }

void SearchPanel::fireSearch() {
    if (onSearch) onSearch(searchBox_.getText());
}

void SearchPanel::setResults(std::vector<nam::ToneInfo> results) {
    resultsModel_.setResults(std::move(results));
    resultsList_.updateContent();
    resultsList_.repaint();
}

void SearchPanel::setStatus(juce::String status) {
    statusLabel_.setText(status, juce::dontSendNotification);
}

void SearchPanel::resized() {
    auto r = getLocalBounds();
    titleLabel_.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);

    auto row = r.removeFromTop(28);
    searchButton_.setBounds(row.removeFromRight(80));
    row.removeFromRight(8);
    searchBox_.setBounds(row);
    r.removeFromTop(4);

    statusLabel_.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);

    resultsList_.setBounds(r);
}
