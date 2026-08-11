#include "app/ui/AppShell.h"
#include "app/ui/NamLookAndFeel.h"

// Deck-item feed for PlayScreen's list/grid layouts (split out of
// AppShell.cpp per the no-god-files rule): builds DeckItem rows with cached
// thumbnails from the current deck — local library decks or TONE3000
// browse results.

namespace {

const juce::String kDot = juce::String::fromUTF8 ("\xC2\xB7");

juce::Image thumbOf (std::map<std::string, juce::Image>& cache,
                     const std::string& key, const juce::Image& full) {
    if (auto it = cache.find (key); it != cache.end() && it->second.isValid())
        return it->second;
    if (! full.isValid()) return {};
    constexpr int side = 160;
    const float s = (float) side
                    / (float) juce::jmax (1, juce::jmax (full.getWidth(), full.getHeight()));
    auto img = full.rescaled (juce::jmax (1, (int) ((float) full.getWidth() * s)),
                              juce::jmax (1, (int) ((float) full.getHeight() * s)),
                              juce::Graphics::mediumResamplingQuality);
    if (cache.size() > 128) cache.clear();   // bounded cache (house rule)
    cache[key] = img;
    return img;
}

} // namespace

void AppShell::pushDeckItems() {
    if (play_ == nullptr) return;
    std::vector<PlayScreen::DeckItem> items;

    if (deckMode_ == 2) {
        items.reserve (playDeck_.size());
        for (const auto& t : playDeck_) {
            PlayScreen::DeckItem it;
            it.title = juce::String (t.title);
            it.sub = t.gear.empty()
                         ? juce::String ("TONE3000")
                         : juce::String (t.gear).toUpperCase() + " " + kDot + " TONE3000";
            it.kept = svc_.isKept && svc_.isKept (t.id);
            it.thumb = thumbOf (thumbCache_, "t_" + t.id,
                                svc_.artworkForTone ? svc_.artworkForTone (t)
                                                    : juce::Image());
            items.push_back (std::move (it));
        }
    } else {
        for (const auto& e : favDeck()) {
            PlayScreen::DeckItem it;
            it.title = juce::String (e.displayName);
            if (e.type == nam::LibraryType::Ir) {
                it.sub = "CABINET IR";
            } else if (! e.arch.empty()) {
                const auto a = juce::String (e.arch).toLowerCase();
                it.sub = (a.contains ("slim") || a.startsWith ("2") || a.startsWith ("a2"))
                             ? "A2" : "A1";
            } else {
                it.sub = "MY TONES";
            }
            it.kept = e.favorite;
            it.thumb = thumbOf (thumbCache_, "e_" + e.id,
                                artwork_ ? artwork_ (e) : juce::Image());
            items.push_back (std::move (it));
        }
    }
    play_->setDeckItems (std::move (items));
}
